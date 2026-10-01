#pragma once
// Included inside namespace Mayap, after the legacy schema decoder.
struct __attribute__((packed)) CriticalSnapshot {
  uint16_t configSchema;
  uint16_t batchSchema;
  uint8_t hasConfig, hasBatch;
  PackedMachineConfigV1 config;
  PackedBatchV1 batch;
};
using CriticalJournal = MayapStorage::Journal<CriticalSnapshot, ExternalEeprom24xx>;
static_assert(sizeof(CriticalJournal::Record) < STORAGE_SLOT_BYTES, "critical journal overflow");
static_assert(STORAGE_BACKUP_BASE + STORAGE_BACKUP_SLOTS * STORAGE_SLOT_BYTES == 4096U, "backup overflow");

// Normal PRIMARY checkpoints never request standby writes. No periodic safety
// timer: runtime counters/turn scheduling belong to the C512 journal only.
enum class BackupSyncReason : uint8_t {
  NormalCheckpoint, BatchStart, BatchStop, CriticalConfig, ResumeState,
  Failover, Migration
};

class DualStorageBackend {
 public:
  DualStorageBackend() : primary_({EEPROM_PRIMARY_CAPACITY, EEPROM_PRIMARY_PAGE, EEPROM_PRIMARY_ADDRESS}),
      backup_({EEPROM_CAPACITY_BYTES, EEPROM_PAGE_SIZE, EEPROM_BACKUP_ADDRESS}),
      mainJournal_(primary_, {STORAGE_JOURNAL_BASE, STORAGE_JOURNAL_SLOTS, STORAGE_SLOT_BYTES}),
      backupJournal_(backup_, {STORAGE_BACKUP_BASE, STORAGE_BACKUP_SLOTS, STORAGE_SLOT_BYTES}) {
    state_.configSchema = CONFIG_SCHEMA; state_.batchSchema = BATCH_SCHEMA;
  }
  bool begin() {
    const bool primaryAck = primary_.begin(), backupAck = backup_.begin();
    mainReadable_ = primaryAck && mainJournal_.scan();
    backupReadable_ = backupAck && backupJournal_.scan();
    logBootDevice("PRIMARY C512", EEPROM_PRIMARY_ADDRESS, primaryAck, mainReadable_, mainJournal_);
    logBootDevice("BACKUP  C32 ", EEPROM_BACKUP_ADDRESS, backupAck, backupReadable_, backupJournal_);
    if ((mainReadable_ && mainJournal_.found && !compatible(mainJournal_)) ||
        (backupReadable_ && backupJournal_.found && !compatible(backupJournal_))) {
      incompatible_ = true; publish(); return false;
    }
    if (mainReadable_ && mainJournal_.found) adopt(mainJournal_);
    if (backupReadable_ && backupJournal_.found &&
        (!have_ || backupJournal_.latest.generation > generation_)) adopt(backupJournal_);
    if (!have_ && (migrationMarked(primary_) || migrationMarked(backup_))) {
      // Never resurrect pre-migration wasRunning after both journals are lost.
      incompatible_ = true; publish(); return false;
    }
    if (!have_) {
      // Read-only migration. Keep BOTH legacy critical copies untouched,
      // including if power disappears during first journal publication.
      LegacyPersistentStore legacy;
      if (legacy.begin()) {
        MachineConfig config{}; PackedBatchV1 batch{};
        state_.hasConfig = legacy.loadConfig(config);
        state_.hasBatch = legacy.loadBatch(batch);
        if (state_.hasConfig) state_.config = packConfig(config);
        if (state_.hasBatch) state_.batch = batch;
        have_ = state_.hasConfig || state_.hasBatch;

      }
    }
    primaryActive_ = mainReadable_;
    if (have_ && mainReadable_) {
      if (!mainJournal_.found || mainJournal_.latest.generation != generation_) {
        primaryActive_ = mainJournal_.append(state_, ++generation_);
      }
    }
    if (!primaryActive_ && have_ && backupReadable_ &&
        (!backupJournal_.found || backupJournal_.latest.generation != generation_)) {
      backupReadable_ = backupJournal_.append(state_, ++generation_);
    }
    if (primaryActive_ && have_) repairStandby(BackupSyncReason::Migration);
    if (primaryActive_) migrateOptional();
    if (have_ && (primaryActive_ || backupReadable_)) {
      if (primaryActive_) markMigrated(primary_);
      if (backupReadable_) markMigrated(backup_);
    }
    publish();
    return ready();
  }
  bool ready() const { return !incompatible_ && (primaryActive_ ? (!writeSuspect_ && health_.failures < 3) : backupReadable_); }
  bool primary() const { return primaryActive_; }
  bool takeOptionalChanged() { const bool value = optionalChanged_; optionalChanged_ = false; return value; }
  bool loadConfig(MachineConfig &out) const {
    if (!state_.hasConfig) return false;
    out = unpackConfig(state_.config); return true;
  }
  bool loadBatch(PackedBatchV1 &out) const {
    if (!state_.hasBatch) return false;
    out = state_.batch; return true;
  }
  bool saveConfig(const MachineConfig &input, MachineConfig &out) {
    MachineConfig clean = input; sanitizeMachineConfig(clean);
    CriticalSnapshot next = state_; next.config = packConfig(clean); next.hasConfig = 1;
    if (!commit(next, sameCritical(next, state_) ? BackupSyncReason::NormalCheckpoint :
                                                    BackupSyncReason::CriticalConfig)) return false;
    out = clean; return true;
  }
  bool saveBatch(const PackedBatchV1 &batch, BackupSyncReason reason = BackupSyncReason::NormalCheckpoint) {
    CriticalSnapshot next = state_; next.batch = batch; next.hasBatch = 1;
    // Classify persistent lifecycle transitions even for older callers. Ordinary
    // elapsed/epoch/turn-counter changes are explicitly excluded.
    if (!batch.wasRunning) reason = BackupSyncReason::BatchStop;
    else if (reason == BackupSyncReason::NormalCheckpoint &&
             (!state_.hasBatch || !state_.batch.wasRunning ||
              batch.batchStartEpoch != state_.batch.batchStartEpoch))
      reason = BackupSyncReason::BatchStart;
    if (!commit(next, reason)) return false;
    // A stopped primary alone cannot invalidate an older backup after reset.
    // Keep the existing NVS stop tombstone until backup also verifies STOP.
    if (batch.wasRunning) return true;
    if (!primaryActive_ || !backupReadable_ || !stopped(mainJournal_) || !stopped(backupJournal_)) return false;
    // Cached records alone cannot acknowledge STOP: a chip may have been
    // unplugged/replaced since its last probe. This runs only in the worker.
    if (!mainJournal_.verifyLatest()) { writeSuspect_ = true; publish(); return false; }
    if (!backupJournal_.verifyLatest()) { backupReadable_ = false; publish(); return false; }
    return true;
  }
  bool loadReminders(ReminderSet &out) {
    if (!primaryActive_) { out = ReminderSet{}; return false; }
    primaryLegacy_.begin(); return primaryLegacy_.loadReminders(out);
  }
  bool saveReminders(const ReminderSet &input, ReminderSet &out) {
    if (!primaryActive_) return false;
    primaryLegacy_.begin(); return primaryLegacy_.saveReminders(input, out);
  }
  uint32_t retries() { return primary_.takeSoftRetryEvents() + backup_.takeSoftRetryEvents(); }
  bool syncCriticalBackup(BackupSyncReason reason) {
    if (reason == BackupSyncReason::NormalCheckpoint || !have_ || incompatible_) return false;
    backupSyncPending_ = true;
    pendingReason_ = reason;
    if (!backupReadable_) return false;
    // Retry/reattach must not rewrite a snapshot already verified in full.
    if (backupJournal_.found &&
        memcmp(&backupJournal_.latest.payload, &state_, sizeof(state_)) == 0) {
      if (!backupJournal_.verifyLatest()) { backupReadable_ = false; return false; }
      backupSyncPending_ = false; return true;
    }
    // Replacement/repair can observe a generation newer than PRIMARY. Never
    // publish a different payload with the same generation as an existing slot.
    if (backupJournal_.found && backupJournal_.latest.generation >= generation_)
      generation_ = backupJournal_.latest.generation + 1U;
    if (!backupJournal_.append(state_, generation_)) {
      backupReadable_ = false; return false;
    }
    backupSyncPending_ = false; return true;
  }
  void service(uint32_t now) {
    if (incompatible_ || uint32_t(now - lastService_) < STORAGE_SERVICE_MS) return;
    lastService_ = now;
    if (mayapI2cBusFault()) { health_.failed(now, true); publish(); return; }
    if (primaryActive_) {
      // After a failed write, ACK alone cannot clear verify-failure evidence.
      const bool ok = writeSuspect_ ? mainJournal_.append(state_, ++generation_) : (primary_.begin() && mainJournal_.verifyLatest());
      if (ok) {
        health_.good(); writeSuspect_ = false;
        refreshBackup();
        repairStandby(BackupSyncReason::ResumeState);
      }
      else if (health_.failed(now, mayapI2cBusFault())) {
        refreshBackup();
        // Routing changes ONLY after current authoritative RAM is published and
        // verified. ACK alone or a readable but stale standby is insufficient.
        if (have_ && backupReadable_) ++generation_;
        if (have_ && syncCriticalBackup(BackupSyncReason::Failover)) {
          primaryActive_ = false; health_.stable = 0;
        }
      }
    } else {
      // Keep backup alive independently; no writes to optional data here.
      refreshBackup();
      if (!primary_.begin()) { health_.stable = 0; publish(); return; }
      if (!health_.stable && !mainJournal_.scan()) { publish(); return; }
      if (!mainJournal_.verifyLatest()) { health_.stable = 0; publish(); return; }
      if (!health_.recovered()) { publish(); return; }
      // Re-scan generation after stability window. RAM is authoritative while
      // running; a reinserted chip can raise the counter, never replace RAM.
      if (!mainJournal_.scan()) { health_.stable = 0; publish(); return; }
      if (mainJournal_.found && !compatible(mainJournal_)) { incompatible_ = true; publish(); return; }
      if (!have_) {
        if (mainJournal_.found) adopt(mainJournal_);
        if (backupReadable_ && backupJournal_.found && compatible(backupJournal_) &&
            (!have_ || backupJournal_.latest.generation > generation_)) adopt(backupJournal_);
      }
      if (mainJournal_.found && mainJournal_.latest.generation > generation_)
        generation_ = mainJournal_.latest.generation;
      if (have_ && !mainJournal_.append(state_, ++generation_)) {
        health_.stable = 0; publish(); return;
      }
      primaryActive_ = true; health_.good(); writeSuspect_ = false;
      // C32 is already the active verified source. Normal progress accumulated
      // during recovery does not authorize a standby mirror after failback.
      repairStandby(BackupSyncReason::ResumeState);
      if (have_) { markMigrated(primary_); if (backupReadable_) markMigrated(backup_); }
    }
    if (primaryActive_ && !optionalMigrationDone_) migrateOptional();
    publish();
  }
 private:
  static void logBootDevice(const char *name, uint8_t address, bool ack, bool scanned, const CriticalJournal &journal) {
    const char *status = !ack ? "NO ACK" : !scanned ? "SCAN FAILED" :
        (journal.found && !compatible(journal)) ? "SCHEMA UNSUPPORTED" : "OK";
    mayapSerialPrintf(false, "[STORAGE] %s 0x%02X: %s (configured geometry)\n", name, address, status);
  }
  static bool sameCritical(const CriticalSnapshot &a, const CriticalSnapshot &b) {
    return a.configSchema == b.configSchema && a.batchSchema == b.batchSchema &&
        a.hasConfig == b.hasConfig && a.hasBatch == b.hasBatch &&
        (!a.hasConfig || memcmp(&a.config, &b.config, sizeof(a.config)) == 0) &&
        (!a.hasBatch || (a.batch.wasRunning == b.batch.wasRunning &&
                        a.batch.batchStartEpoch == b.batch.batchStartEpoch));
  }
  void repairStandby(BackupSyncReason reason) {
    if (!have_ || !backupReadable_) return;
    if (backupSyncPending_ || !backupJournal_.found ||
        !sameCritical(backupJournal_.latest.payload, state_))
      (void)syncCriticalBackup(backupSyncPending_ ? pendingReason_ : reason);
  }
  void migrateOptional() {
    ReminderSet data{}, readback{}; bool present = false;
    if (!primaryLegacy_.begin() || !primaryLegacy_.loadRemindersForMigration(data, present)) return;
    if (present) { optionalMigrationDone_ = true; return; }
    LegacyPersistentStore legacy;
    if (!legacy.begin() || !legacy.loadRemindersForMigration(data, present)) return;
    if (!present) { optionalMigrationDone_ = true; return; }
    if (primaryLegacy_.saveReminders(data, readback)) {
      optionalMigrationDone_ = true;
      optionalChanged_ = true;
    }
  }
  static bool stopped(const CriticalJournal &journal) {
    return journal.found && journal.latest.payload.hasBatch &&
           journal.latest.payload.batch.wasRunning == 0;
  }
  void refreshBackup() {
    if (backupReadable_ && backup_.begin() && backupJournal_.verifyLatest()) return;
    backupReadable_ = backup_.begin() && backupJournal_.scan();
    if (!backupReadable_) return;
    if (backupJournal_.found && !compatible(backupJournal_)) {
      backupReadable_ = false;
      return; // Preserve an unknown schema; it cannot become a write target.
    }
    if (backupJournal_.found && backupJournal_.latest.generation > generation_) {
      generation_ = backupJournal_.latest.generation;
      if (have_) { backupSyncPending_ = true; pendingReason_ = BackupSyncReason::ResumeState; }
    }
    if (!have_) {
      if (backupJournal_.found) adopt(backupJournal_);
      else if (migrationMarked(backup_)) backupReadable_ = false;
      return;
    }
    // Standby age in elapsed/turn counters is intentional. Only lifecycle/config
    // differences or an outstanding critical request authorize a repair write.
    // In BACKUP mode every admitted replacement must receive current RAM first.
    if (!primaryActive_) {
      ++generation_;
      (void)syncCriticalBackup(BackupSyncReason::Failover);
    }
  }
  // One-time migration fence, NOT a current journal pointer. Located outside
  // legacy config/batch/reminders and outside both new journals.
  struct MigrationFence { uint32_t magic, inverse; };
  static bool migrationMarked(ExternalEeprom24xx &io) {
    MigrationFence marker{};
    return io.readBytes(io.deviceAddress() == EEPROM_PRIMARY_ADDRESS ? STORAGE_PRIMARY_FENCE : STORAGE_BACKUP_FENCE, &marker, sizeof(marker)) &&
        marker.magic == 0x4d494752U && marker.inverse == ~marker.magic;
  }
  static void markMigrated(ExternalEeprom24xx &io) {
    if (migrationMarked(io)) return;
    const MigrationFence marker{0x4d494752U, ~0x4d494752U};
    (void)io.writeBytes(io.deviceAddress() == EEPROM_PRIMARY_ADDRESS ? STORAGE_PRIMARY_FENCE : STORAGE_BACKUP_FENCE, &marker, sizeof(marker));
  }
  static bool compatible(const CriticalJournal &j) {
    return j.latest.payload.configSchema == CONFIG_SCHEMA && j.latest.payload.batchSchema == BATCH_SCHEMA;
  }
  void adopt(const CriticalJournal &j) {
    // Unknown payload versions must not be silently treated as legacy data.
    if (j.latest.payload.configSchema != CONFIG_SCHEMA || j.latest.payload.batchSchema != BATCH_SCHEMA) return;
    state_ = j.latest.payload; generation_ = j.latest.generation; have_ = true;
  }
  void retainFailedBatch(const CriticalSnapshot &next, BackupSyncReason reason) {
    // Config/START are applied by the controller only after durable ACK. A
    // checkpoint of an already-running batch, or STOP protected by NVS, is
    // authoritative in RAM even if the current EEPROM write cannot complete.
    if (reason == BackupSyncReason::NormalCheckpoint || reason == BackupSyncReason::ResumeState ||
        reason == BackupSyncReason::BatchStop) {
      state_ = next; have_ = true;
      if (reason != BackupSyncReason::NormalCheckpoint) {
        backupSyncPending_ = true; pendingReason_ = reason;
      }
    }
  }
  bool commit(const CriticalSnapshot &next, BackupSyncReason reason) {
    if (!ready()) { retainFailedBatch(next, reason); return false; }
    if (have_ && memcmp(&next, &state_, sizeof(next)) == 0) {
      if (primaryActive_ && reason != BackupSyncReason::NormalCheckpoint)
        (void)syncCriticalBackup(reason);
      publish();
      return true;
    }
    const bool ok = primaryActive_ ? mainJournal_.append(next, ++generation_) :
                                    backupJournal_.append(next, ++generation_);
    if (!ok) {
      if (primaryActive_) writeSuspect_ = true;
      else backupReadable_ = false;
      retainFailedBatch(next, reason);
      publish(); return false;
    }
    state_ = next; have_ = true;
    if (primaryActive_) {
      health_.good(); writeSuspect_ = false;
      if (reason != BackupSyncReason::NormalCheckpoint) (void)syncCriticalBackup(reason);
    } else backupSyncPending_ = false; // This commit already published active RAM on C32.
    publish(); return true;
  }
  void publish() {
    mayapStorageSetBackupOnline(backupReadable_ && !incompatible_ && !backupSyncPending_ &&
        (!have_ || (backupJournal_.found && sameCritical(backupJournal_.latest.payload, state_))));
    mayapStorageSetPrimaryOnline(primaryActive_ && !writeSuspect_ && !incompatible_ && health_.failures == 0);
  }
  ExternalEeprom24xx primary_, backup_;
  CriticalJournal mainJournal_, backupJournal_;
  LegacyPersistentStore primaryLegacy_{true};
  CriticalSnapshot state_{};
  uint64_t generation_ = 0;
  uint32_t lastService_ = 0;
  MayapStorage::Health health_{};
  bool have_ = false, primaryActive_ = false, mainReadable_ = false;
  bool backupReadable_ = false, writeSuspect_ = false, incompatible_ = false;
  bool optionalMigrationDone_ = false, optionalChanged_ = false;
  bool backupSyncPending_ = false;
  BackupSyncReason pendingReason_ = BackupSyncReason::Migration;
};

// One bounded mailbox per kind, SPSC with release/acquire publication. The
// control task only copies RAM. No EEPROM wait, I2C lock or retry in that task.
class PersistentStore {
 public:
  bool begin() {
    const bool ok = backend_.begin();
    configValid_ = backend_.loadConfig(config_);
    batchValid_ = backend_.loadBatch(batch_);
    backend_.loadReminders(reminders_);
    __atomic_store_n(&ready_, ok, __ATOMIC_RELEASE);
    return ok;
  }
  bool startWorker() {
    asyncMode_ = true;
    worker_ = xTaskCreateStaticPinnedToCore(run, "mayap_store", sizeof(stack_), this,
                                          1, stack_, &tcb_, 0);
    if (!worker_) __atomic_store_n(&ready_, false, __ATOMIC_RELEASE);
    return worker_ != nullptr;
  }
  bool ready() const { return __atomic_load_n(&ready_, __ATOMIC_ACQUIRE); }
  bool probe() const { return ready(); }
  bool reconnect() const { return ready(); }
  void markOffline() {} // Owner worker decides health, never discard RAM here.
  bool primary() const { return mayapStoragePrimaryOnline(); }
  bool configOutstanding() const { return load(configJob_) != 0; }
  bool pendingConfig() const { return configWaiting_; }
  bool pendingBatch() const { return batchWaiting_; }
  bool batchBusy() const { return load(batchJob_) == 1; }
  bool pendingReminders() const { return reminderWaiting_; }
  bool loadConfig(MachineConfig &out) {
    if (!configValid_ && load(recoveredConfigReady_) == 1) {
      config_ = recoveredConfig_; configValid_ = true; set(recoveredConfigReady_, 2);
    }
    if (!configValid_) return false;
    out = config_; return true;
  }
  bool loadBatch(PackedBatchV1 &out) { if (!batchValid_) return false; out = batch_; return true; }
  bool loadReminders(ReminderSet &out) { out = reminders_; return true; }
  bool takeRecoveredReminders(ReminderSet &out) {
    if (load(recoveredReminderReady_) != 1) return false;
    out = reminders_ = recoveredReminders_; set(recoveredReminderReady_, 0); return true;
  }
  bool saveConfig(const MachineConfig &input, MachineConfig &out) {
    configWaiting_ = false;
    if (asyncMode_ && !worker_) return false;
    if (!worker_) { const bool ok = backend_.saveConfig(input, out); if (ok) { config_ = out; configValid_ = true; } return ok; }
    const uint8_t state = load(configJob_);
    if (state == 1) { configWaiting_ = true; return false; }
    if (state >= 2) {
      if (state == 2) { config_ = configResult_; configValid_ = true; out = config_; }
      set(configJob_, 0); return state == 2;
    }
    configRequest_ = input; set(configJob_, 1); configWaiting_ = true; return false;
  }
  bool saveBatch(const PackedBatchV1 &input, BackupSyncReason reason = BackupSyncReason::NormalCheckpoint) {
    batchWaiting_ = false;
    if (asyncMode_ && !worker_) return false;
    if (!worker_) { const bool ok = backend_.saveBatch(input, reason); if (ok) { batch_ = input; batchValid_ = true; } return ok; }
    const uint8_t state = load(batchJob_);
    if (state == 1) { batchWaiting_ = true; return false; }
    if (state >= 2) {
      const bool same = reason == batchReason_ && memcmp(&input, &batchRequest_, sizeof(input)) == 0;
      if (state == 2) { batch_ = batchRequest_; batchValid_ = true; }
      set(batchJob_, 0);
      // A completed checkpoint cannot acknowledge a newer STOP or START.
      if (same) return state == 2;
    }
    batchRequest_ = input; batchReason_ = reason; set(batchJob_, 1); batchWaiting_ = true; return false;
  }
  bool saveReminders(const ReminderSet &input, ReminderSet &out) {
    reminderWaiting_ = false;
    if (asyncMode_ && !worker_) return false;
    if (!worker_) return backend_.saveReminders(input, out);
    const uint8_t state = load(reminderJob_);
    if (state == 1) { reminderWaiting_ = true; return false; }
    if (state >= 2) {
      if (state == 2) out = reminders_ = reminderResult_;
      set(reminderJob_, 0); return state == 2;
    }
    reminderRequest_ = input; set(reminderJob_, 1); reminderWaiting_ = true; return false;
  }
  // Poll periodic checkpoints without submitting a new timestamp each loop.
  bool finishBatch(bool &ok, BackupSyncReason *reason = nullptr) {
    const uint8_t state = load(batchJob_);
    if (state < 2) return false;
    ok = state == 2;
    if (reason) *reason = batchReason_;
    if (ok) { batch_ = batchRequest_; batchValid_ = true; }
    set(batchJob_, 0); batchWaiting_ = false; return true;
  }
  uint32_t takeEepromSoftRetryEvents() { return __atomic_exchange_n(&retries_, 0U, __ATOMIC_ACQ_REL); }
 private:
  static uint8_t load(const uint8_t &v) { return __atomic_load_n(&v, __ATOMIC_ACQUIRE); }
  static void set(uint8_t &v, uint8_t value) { __atomic_store_n(&v, value, __ATOMIC_RELEASE); }
 public:
  void workerStep() {
      if (!mayapFirmwareMaintenanceActive()) {
        backend_.service(millis());
        if (load(configJob_) == 1) set(configJob_, backend_.saveConfig(configRequest_, configResult_) ? 2 : 3);
        if (load(batchJob_) == 1) set(batchJob_, backend_.saveBatch(batchRequest_, batchReason_) ? 2 : 3);
        if (load(reminderJob_) == 1) set(reminderJob_, backend_.saveReminders(reminderRequest_, reminderResult_) ? 2 : 3);
        if (load(recoveredConfigReady_) == 0 && backend_.ready() && backend_.loadConfig(recoveredConfig_))
          set(recoveredConfigReady_, 1);
        __atomic_store_n(&ready_, backend_.ready(), __ATOMIC_RELEASE);
        __atomic_add_fetch(&retries_, backend_.retries(), __ATOMIC_ACQ_REL);
        if (!backend_.primary() || backend_.takeOptionalChanged()) optionalWasPrimary_ = false;
        if (backend_.primary() && !optionalWasPrimary_ && load(recoveredReminderReady_) == 0) {
          if (backend_.loadReminders(recoveredReminders_)) set(recoveredReminderReady_, 1);
          optionalWasPrimary_ = true;
        }
        mayapTemperatureHistoryService();
      }
  }
 private:
  static void run(void *context) {
    auto &s = *static_cast<PersistentStore *>(context);
    for (;;) { s.workerStep(); vTaskDelay(pdMS_TO_TICKS(10)); }
  }
  DualStorageBackend backend_{};
  MachineConfig config_{}, configRequest_{}, configResult_{}, recoveredConfig_{};
  uint8_t recoveredConfigReady_ = 0;
  PackedBatchV1 batch_{}, batchRequest_{};
  BackupSyncReason batchReason_ = BackupSyncReason::NormalCheckpoint;
  ReminderSet reminders_{}, reminderRequest_{}, reminderResult_{}, recoveredReminders_{};
  uint8_t recoveredReminderReady_ = 0;
  bool optionalWasPrimary_ = false;
  uint8_t configJob_ = 0, batchJob_ = 0, reminderJob_ = 0;
  bool configWaiting_ = false, batchWaiting_ = false, reminderWaiting_ = false;
  bool configValid_ = false, batchValid_ = false, ready_ = false, asyncMode_ = false;
  uint32_t retries_ = 0;
  StaticTask_t tcb_{};
  StackType_t stack_[6144 / sizeof(StackType_t)]{};
  TaskHandle_t worker_ = nullptr;
};
