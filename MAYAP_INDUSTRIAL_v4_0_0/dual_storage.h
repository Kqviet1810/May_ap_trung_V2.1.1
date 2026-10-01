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

class DualStorageBackend {
 public:
  DualStorageBackend() : primary_({EEPROM_PRIMARY_CAPACITY, EEPROM_PRIMARY_PAGE, EEPROM_PRIMARY_ADDRESS}),
      backup_({EEPROM_CAPACITY_BYTES, EEPROM_PAGE_SIZE, EEPROM_BACKUP_ADDRESS}),
      mainJournal_(primary_, {STORAGE_JOURNAL_BASE, STORAGE_JOURNAL_SLOTS, STORAGE_SLOT_BYTES}),
      backupJournal_(backup_, {STORAGE_BACKUP_BASE, STORAGE_BACKUP_SLOTS, STORAGE_SLOT_BYTES}) {
    state_.configSchema = CONFIG_SCHEMA; state_.batchSchema = BATCH_SCHEMA;
  }
  bool begin() {
    mainReadable_ = primary_.begin() && mainJournal_.scan();
    backupReadable_ = backup_.begin() && backupJournal_.scan();
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
        ReminderSet reminders{};
        if (mainReadable_ && legacy.loadReminders(reminders)) {
          ReminderSet ignored{};
          primaryLegacy_.begin();
          primaryLegacy_.saveReminders(reminders, ignored);
        }
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
    if (primaryActive_ && have_) mirrorCritical();
    if (have_ && (primaryActive_ || backupReadable_)) {
      if (primaryActive_) markMigrated(primary_);
      if (backupReadable_) markMigrated(backup_);
    }
    publish();
    return ready();
  }
  bool ready() const { return !incompatible_ && (primaryActive_ ? (!writeSuspect_ && health_.failures < 3) : backupReadable_); }
  bool primary() const { return primaryActive_; }
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
    if (!commit(next)) return false;
    out = clean; return true;
  }
  bool saveBatch(const PackedBatchV1 &batch) {
    CriticalSnapshot next = state_; next.batch = batch; next.hasBatch = 1;
    if (!commit(next)) return false;
    // A stopped primary alone cannot invalidate an older backup after reset.
    // Keep the existing NVS stop tombstone until backup also verifies STOP.
    return batch.wasRunning != 0 || (backupReadable_ && backupJournal_.found &&
        backupJournal_.latest.payload.hasBatch && backupJournal_.latest.payload.batch.wasRunning == 0);
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
  void service(uint32_t now) {
    if (incompatible_ || uint32_t(now - lastService_) < STORAGE_SERVICE_MS) return;
    lastService_ = now;
    if (mayapI2cBusFault()) { health_.failed(now, true); publish(); return; }
    if (primaryActive_) {
      // After a failed write, ACK alone cannot clear verify-failure evidence.
      const bool ok = writeSuspect_ ? mainJournal_.append(state_, ++generation_) : (primary_.begin() && mainJournal_.verifyLatest());
      if (ok) {
        health_.good(); writeSuspect_ = false;
        if (!backupReadable_) backupReadable_ = backup_.begin() && backupJournal_.scan();
        if (have_) mirrorCritical();
      }
      else if (health_.failed(now, false)) {
        backupReadable_ = backup_.begin() && backupJournal_.scan();
        if (backupReadable_ && (!have_ || backupJournal_.append(state_, ++generation_))) {
          primaryActive_ = false; health_.stable = 0;
        }
      }
    } else {
      // Keep backup alive independently; no writes to optional data here.
      if (!backupReadable_) backupReadable_ = backup_.begin() && backupJournal_.scan();
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
      mirrorCritical();
      if (have_) { markMigrated(primary_); if (backupReadable_) markMigrated(backup_); }
    }
    publish();
  }
 private:
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
  bool commit(const CriticalSnapshot &next) {
    if (!ready()) return false;
    if (have_ && memcmp(&next, &state_, sizeof(next)) == 0) return true;
    const bool ok = primaryActive_ ? mainJournal_.append(next, ++generation_) :
                                    backupJournal_.append(next, ++generation_);
    if (!ok) {
      if (primaryActive_) writeSuspect_ = true;
      else backupReadable_ = false;
      publish(); return false;
    }
    state_ = next; have_ = true;
    if (primaryActive_) { health_.good(); writeSuspect_ = false; mirrorCritical(); }
    publish(); return true;
  }
  void mirrorCritical() {
    // Only critical payload is backed up, never reminders/history/notes.
    if (!backupReadable_) return;
    if (backupJournal_.found && backupJournal_.latest.generation == generation_) return;
    if (!backupJournal_.append(state_, generation_)) backupReadable_ = false;
  }
  void publish() { mayapStorageSetPrimaryOnline(primaryActive_ && !writeSuspect_ && !incompatible_ && health_.failures == 0); }
  ExternalEeprom24xx primary_, backup_;
  CriticalJournal mainJournal_, backupJournal_;
  LegacyPersistentStore primaryLegacy_{true};
  CriticalSnapshot state_{};
  uint64_t generation_ = 0;
  uint32_t lastService_ = 0;
  MayapStorage::Health health_{};
  bool have_ = false, primaryActive_ = false, mainReadable_ = false;
  bool backupReadable_ = false, writeSuspect_ = false, incompatible_ = false;
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
  bool pendingConfig() const { return load(configJob_) == 1; }
  bool pendingBatch() const { return load(batchJob_) == 1; }
  bool pendingReminders() const { return load(reminderJob_) == 1; }
  bool loadConfig(MachineConfig &out) {
    if (!configValid_ && load(recoveredConfigReady_) == 1) {
      config_ = recoveredConfig_; configValid_ = true; set(recoveredConfigReady_, 2);
    }
    if (!configValid_) return false;
    out = config_; return true;
  }
  bool loadBatch(PackedBatchV1 &out) { if (!batchValid_) return false; out = batch_; return true; }
  bool loadReminders(ReminderSet &out) { out = reminders_; return true; }
  bool saveConfig(const MachineConfig &input, MachineConfig &out) {
    if (asyncMode_ && !worker_) return false;
    if (!worker_) { const bool ok = backend_.saveConfig(input, out); if (ok) { config_ = out; configValid_ = true; } return ok; }
    const uint8_t state = load(configJob_);
    if (state == 1) return false;
    if (state >= 2) {
      if (state == 2) { config_ = configResult_; configValid_ = true; out = config_; }
      set(configJob_, 0); return state == 2;
    }
    configRequest_ = input; set(configJob_, 1); return false;
  }
  bool saveBatch(const PackedBatchV1 &input) {
    if (asyncMode_ && !worker_) return false;
    if (!worker_) { const bool ok = backend_.saveBatch(input); if (ok) { batch_ = input; batchValid_ = true; } return ok; }
    const uint8_t state = load(batchJob_);
    if (state == 1) return false;
    if (state >= 2) {
      const bool same = memcmp(&input, &batchRequest_, sizeof(input)) == 0;
      if (state == 2) { batch_ = batchRequest_; batchValid_ = true; }
      set(batchJob_, 0);
      // A completed checkpoint cannot acknowledge a newer STOP or START.
      if (same) return state == 2;
    }
    batchRequest_ = input; set(batchJob_, 1); return false;
  }
  bool saveReminders(const ReminderSet &input, ReminderSet &out) {
    if (asyncMode_ && !worker_) return false;
    if (!worker_) return backend_.saveReminders(input, out);
    const uint8_t state = load(reminderJob_);
    if (state == 1) return false;
    if (state >= 2) {
      if (state == 2) out = reminders_ = reminderResult_;
      set(reminderJob_, 0); return state == 2;
    }
    reminderRequest_ = input; set(reminderJob_, 1); return false;
  }
  // Poll periodic checkpoints without submitting a new timestamp each loop.
  bool finishBatch(bool &ok) {
    const uint8_t state = load(batchJob_);
    if (state < 2) return false;
    ok = state == 2;
    if (ok) { batch_ = batchRequest_; batchValid_ = true; }
    set(batchJob_, 0); return true;
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
        if (load(batchJob_) == 1) set(batchJob_, backend_.saveBatch(batchRequest_) ? 2 : 3);
        if (load(reminderJob_) == 1) set(reminderJob_, backend_.saveReminders(reminderRequest_, reminderResult_) ? 2 : 3);
        if (load(recoveredConfigReady_) == 0 && backend_.ready() && backend_.loadConfig(recoveredConfig_))
          set(recoveredConfigReady_, 1);
        __atomic_store_n(&ready_, backend_.ready(), __ATOMIC_RELEASE);
        __atomic_add_fetch(&retries_, backend_.retries(), __ATOMIC_ACQ_REL);
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
  ReminderSet reminders_{}, reminderRequest_{}, reminderResult_{};
  uint8_t configJob_ = 0, batchJob_ = 0, reminderJob_ = 0;
  bool configValid_ = false, batchValid_ = false, ready_ = false, asyncMode_ = false;
  uint32_t retries_ = 0;
  StaticTask_t tcb_{};
  StackType_t stack_[6144 / sizeof(StackType_t)]{};
  TaskHandle_t worker_ = nullptr;
};
