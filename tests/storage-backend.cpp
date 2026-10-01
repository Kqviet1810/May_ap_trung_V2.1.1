#include <cassert>
#include <cstdio>
#include <vector>
#include <cstdarg>
#include <string>
#include "../MAYAP_INDUSTRIAL_v4_0_0/storage_journal.h"
constexpr uint32_t EEPROM_CAPACITY_BYTES=4096;
constexpr uint8_t EEPROM_PAGE_SIZE=32;
constexpr uint16_t CONFIG_SCHEMA=12, BATCH_SCHEMA=3;
constexpr uint32_t EEPROM_PRIMARY_CAPACITY=65536, STORAGE_SERVICE_MS=5000;
constexpr uint16_t EEPROM_PRIMARY_PAGE=128, STORAGE_SLOT_BYTES=512,
 STORAGE_JOURNAL_BASE=0x1000, STORAGE_JOURNAL_SLOTS=96,
 STORAGE_BACKUP_BASE=0xc00, STORAGE_BACKUP_SLOTS=2, STORAGE_PRIMARY_FENCE=0, STORAGE_BACKUP_FENCE=0xb00;
#include "actual-storage-addresses.inc"
#include "actual-payloads.inc"
struct MachineConfig { uint32_t value; };
struct ReminderSet { uint32_t value; };
void sanitizeMachineConfig(MachineConfig &) {}
PackedMachineConfigV1 packConfig(MachineConfig c) { PackedMachineConfigV1 p{}; p.targetTemp=float(c.value); return p; }
MachineConfig unpackConfig(PackedMachineConfigV1 c) { return {uint32_t(c.targetTemp)}; }
static bool online=false, backupOnline=false, bus=false;
static std::string logs;
void mayapSerialPrintf(bool, const char *format, ...) {
  char line[200]; va_list args; va_start(args,format);
  vsnprintf(line,sizeof(line),format,args); va_end(args); logs+=line;
}
static uint32_t clockMs=0;
static int powerBudget=-1;
uint32_t millis() { return clockMs; }
bool mayapI2cBusFault() { return bus; }
void mayapStorageSetBackupOnline(bool b) { backupOnline=b; }
void mayapStorageSetPrimaryOnline(bool b) { online=b; }
struct Chip {
  std::vector<uint8_t> bytes=std::vector<uint8_t>(65536,0xff);
  bool present=true, corrupt=false;
  int budget=-1;
  unsigned writes=0;
  unsigned calls=0, publications=0;
  unsigned reads=0, probes=0;
} chips[2];
class ExternalEeprom24xx {
 public:
  explicit ExternalEeprom24xx(MayapStorage::Geometry g) : id(g.address==EEPROM_PRIMARY_ADDRESS?0:1) {}
  uint8_t deviceAddress() const { return id==0?EEPROM_PRIMARY_ADDRESS:EEPROM_BACKUP_ADDRESS; }
  bool begin() { ++chips[id].probes; return chips[id].present && !bus && powerBudget!=0; }
  bool readBytes(uint16_t a, void *out, size_t n) {
    if (!begin()) return false;
    ++chips[id].reads;
    memcpy(out,chips[id].bytes.data()+a,n); return true;
  }
  bool writeBytes(uint16_t a, const void *p, size_t n) {
    if (!begin()) return false;
    auto &c=chips[id];
    ++c.calls;
    for (size_t i=0;i<n;++i) {
      if (!c.budget || !powerBudget) return false;
      if(c.budget>0) --c.budget;
      if(powerBudget>0) --powerBudget;
      c.bytes[a+i]=static_cast<const uint8_t *>(p)[i] ^ (c.corrupt?1:0); ++c.writes;
    }
    if (n==1 && a>=STORAGE_BACKUP_BASE && a%STORAGE_SLOT_BYTES==STORAGE_SLOT_BYTES-1 &&
        *static_cast<const uint8_t *>(p)==0xa5 && !c.corrupt) ++c.publications;
    return true;
  }
  uint32_t takeSoftRetryEvents() { return 0; }
 private: unsigned id;
};
static bool primaryRemindersPresent=false;
static uint32_t primaryReminderValue=0;
class LegacyPersistentStore {
 public:
  explicit LegacyPersistentStore(bool primary=false):primary_(primary) {}
  bool begin() { return chips[primary_?0:1].present && !bus && powerBudget!=0; }
  bool loadConfig(MachineConfig &c) { c.value=37; return true; }
  bool loadBatch(PackedBatchV1 &b) { b={}; b.elapsedSec=300; b.wasRunning=1; return true; }
  bool loadReminders(ReminderSet &r) { r.value=12; return true; }
  bool loadRemindersForMigration(ReminderSet &r, bool &present) {
    if (!begin()) return false;
    present = !primary_ || primaryRemindersPresent;
    r.value = primary_ ? primaryReminderValue : 12;
    return true;
  }
  bool saveReminders(const ReminderSet &r, ReminderSet &out) {
    assert(primary_); primaryRemindersPresent=true; primaryReminderValue=r.value; out=r; return true;
  }
 private: bool primary_;
};
using StackType_t=uint32_t;
struct StaticTask_t {};
using TaskHandle_t=void *;
TaskHandle_t xTaskCreateStaticPinnedToCore(void (*)(void *),const char *,size_t,void *,int,StackType_t *,StaticTask_t *,int) { return reinterpret_cast<void *>(1); }
void vTaskDelay(uint32_t) {}
uint32_t pdMS_TO_TICKS(uint32_t n) { return n; }
bool mayapFirmwareMaintenanceActive() { return false; }
bool mayapStoragePrimaryOnline() { return online; }
void mayapTemperatureHistoryService() {}
#include "actual-storage.inc"
void step(DualStorageBackend &s) { clockMs+=5000; s.service(clockMs); }
void reset() { primaryRemindersPresent=false; primaryReminderValue=0; chips[0]=Chip{}; chips[1]=Chip{}; bus=false; clockMs=0; logs.clear(); powerBudget=-1; }
CriticalJournal::Record latest(unsigned id) {
  ExternalEeprom24xx io({id?4096U:65536U,uint16_t(id?32:128),id?EEPROM_BACKUP_ADDRESS:EEPROM_PRIMARY_ADDRESS});
  CriticalJournal journal(io,{id?STORAGE_BACKUP_BASE:STORAGE_JOURNAL_BASE,id?STORAGE_BACKUP_SLOTS:STORAGE_JOURNAL_SLOTS,STORAGE_SLOT_BYTES});
  assert(journal.scan() && journal.found); return journal.latest;
}
void assertStopBoot(DualStorageBackend &s) {
  PackedBatchV1 batch{};
  const bool has=s.loadBatch(batch);
  assert(MayapStorage::bootBatchAction(has,batch.wasRunning,true)==MayapStorage::BootBatchAction::ReconcileStop);
}
void standbyPolicy() {
  reset(); DualStorageBackend s; assert(s.begin());
  assert(logs.find("PRIMARY C512 0x50: OK")!=std::string::npos);
  assert(logs.find("BACKUP  C32  0x56: OK")!=std::string::npos);
  PackedBatchV1 b{}; assert(s.saveBatch(b));
  b.wasRunning=1; b.batchStartEpoch=86400; b.checkpointEpoch=86400;
  const unsigned starts=chips[1].publications;
  assert(s.saveBatch(b,BackupSyncReason::BatchStart));
  assert(chips[1].publications==starts+1 && latest(1).payload.batch.wasRunning);
  const unsigned writes=chips[1].writes, calls=chips[1].calls, pubs=chips[1].publications;
  const unsigned primaryPubs=chips[0].publications;
  for(unsigned i=1;i<=1000;++i) {
    b.elapsedSec=i*300; b.checkpointEpoch=86400+b.elapsedSec;
    b.lastTurnEpoch=b.checkpointEpoch; b.turnCountBatch=i; b.turnCountToday=i%100;
    b.nextDirection=i%2;
    assert(s.saveBatch(b)); clockMs+=300000; s.service(clockMs);
  }
  assert(chips[0].publications==primaryPubs+1000);
  assert(chips[1].writes==writes && chips[1].calls==calls && chips[1].publications==pubs);
  assert(latest(1).payload.batch.elapsedSec==0); // Intentional standby age.
  MachineConfig c{37}, out{}; assert(s.saveConfig(c,out)); // No-op config is not critical.
  assert(chips[1].writes==writes);
  for(unsigned i=0;i<3;++i) { DualStorageBackend boot; assert(boot.begin()); }
  assert(chips[1].writes==writes); // Reboot/health must not disguise periodic mirroring.
  assert(s.saveBatch(b,BackupSyncReason::ResumeState));
  assert(chips[1].publications==pubs+1 && latest(1).payload.batch.elapsedSec==300000);
  const unsigned resumed=chips[1].writes;
  b.elapsedSec+=300; assert(s.saveBatch(b)); step(s); assert(chips[1].writes==resumed);
  c.value=40; assert(s.saveConfig(c,out));
  assert(latest(1).payload.config.targetTemp==40 && chips[1].writes>resumed);
  const unsigned configured=chips[1].writes;
  b.elapsedSec+=300; assert(s.saveBatch(b)); step(s); assert(chips[1].writes==configured);
  b={}; assert(s.saveBatch(b,BackupSyncReason::BatchStop));
  assert(!latest(1).payload.batch.wasRunning && !latest(0).payload.batch.wasRunning);
  const unsigned stopped=chips[1].writes;
  assert(s.saveBatch(b)); step(s); assert(chips[1].writes==stopped); // STOP retry deduplicates.
  puts("standby wear: 1000 normal checkpoints => C512 1000 publications, C32 zero writes/calls; lifecycle/config/resume PASS");
}
void backupOutageAndStop() {
  reset(); DualStorageBackend s; assert(s.begin());
  PackedBatchV1 b{}; assert(s.loadBatch(b));
  chips[1].present=false; step(s);
  assert(s.primary() && s.ready() && online && !backupOnline);
  const unsigned writes=chips[1].writes;
  b.elapsedSec=600; assert(s.saveBatch(b)); step(s); assert(chips[1].writes==writes);
  chips[1].present=true; step(s);
  assert(backupOnline && chips[1].writes==writes); // Elapsed drift does not repair standby.
  chips[1].present=false; step(s);
  MachineConfig c{55}, out{}; assert(s.saveConfig(c,out));
  b.elapsedSec=900; assert(s.saveBatch(b));
  chips[1].present=true; step(s);
  assert(backupOnline && latest(1).payload.config.targetTemp==55);
  const unsigned repaired=chips[1].writes;
  b.elapsedSec=1200; assert(s.saveBatch(b)); step(s); assert(chips[1].writes==repaired);
  auto runningBackup=chips[1];
  chips[1].present=false; step(s);
  bool nvsStop=true; PackedBatchV1 stopped{};
  assert(!s.saveBatch(stopped)); // PRIMARY STOP alone cannot release tombstone.
  DualStorageBackend primaryOnly; assert(primaryOnly.begin()); assertStopBoot(primaryOnly);
  assert(!primaryOnly.saveBatch(stopped));
  chips[0].present=false; chips[1]=runningBackup;
  DualStorageBackend backupOnly; assert(backupOnly.begin()); assertStopBoot(backupOnly);
  assert(backupOnly.loadBatch(b) && b.wasRunning); // stale C32 running cannot resume.
  assert(!backupOnly.saveBatch(stopped));
  chips[0].present=true; for(int i=0;i<4;++i) step(backupOnly);
  if(backupOnly.saveBatch(stopped)) nvsStop=false;
  assert(!nvsStop && !latest(0).payload.batch.wasRunning && !latest(1).payload.batch.wasRunning);
  // Replacing a chip between health checks cannot use cached STOP as evidence.
  chips[1]=runningBackup; assert(!backupOnly.saveBatch(stopped));
  step(backupOnly); assert(backupOnly.saveBatch(stopped));
  puts("standby outage: degraded PRIMARY continues, critical repair, NVS STOP on single-copy boots and cached STOP replacement PASS");
}
void failoverLatestAndReplacement() {
  for(uint64_t foreignGeneration : {1ULL,1000000000ULL}) {
    reset(); DualStorageBackend s; assert(s.begin());
    PackedBatchV1 b{}; assert(s.loadBatch(b)); b.elapsedSec=777; assert(s.saveBatch(b));
    const auto standby=chips[1].writes;
    bus=true; for(int i=0;i<6;++i) step(s); assert(s.primary() && chips[1].writes==standby); bus=false;
    chips[0].present=false; step(s); assert(s.primary()); chips[0].present=true; step(s); assert(s.primary());
    chips[0].present=false; step(s); step(s); assert(s.primary());
    chips[1].corrupt=true; step(s); assert(s.primary()); // Failed publication cannot change routing.
    chips[1].corrupt=false; step(s); assert(!s.primary() && s.ready());
    assert(latest(1).payload.batch.elapsedSec==777);
    b.elapsedSec=888; assert(s.saveBatch(b)); assert(latest(1).payload.batch.elapsedSec==888);
    const unsigned activeWrites=chips[1].writes;
    chips[0].present=true;
    std::fill(chips[0].bytes.begin()+STORAGE_JOURNAL_BASE,chips[0].bytes.begin()+0xd000,0xff);
    ExternalEeprom24xx io({65536,128,EEPROM_PRIMARY_ADDRESS});
    CriticalJournal old(io,{STORAGE_JOURNAL_BASE,STORAGE_JOURNAL_SLOTS,STORAGE_SLOT_BYTES});
    assert(old.scan()); CriticalSnapshot foreign=latest(1).payload;
    foreign.batch.elapsedSec=9999; foreign.config.targetTemp=99;
    assert(old.append(foreign,foreignGeneration));
    for(int i=0;i<3;++i) { step(s); assert(!s.primary()); }
    step(s); assert(s.primary() && online);
    MachineConfig config{}; assert(s.loadConfig(config) && config.value==37);
    assert(s.loadBatch(b) && b.elapsedSec==888);
    assert(latest(0).generation>foreignGeneration && latest(0).payload.batch.elapsedSec==888);
    assert(chips[1].writes==activeWrites); // Failback itself does not mirror normal progress.
    for(int i=0;i<10;++i) { b.elapsedSec+=300; assert(s.saveBatch(b)); step(s); }
    assert(chips[1].writes==activeWrites);
  }
  reset(); chips[0].present=false; DualStorageBackend missingPrimary; assert(missingPrimary.begin());
  assert(logs.find("PRIMARY C512 0x50: NO ACK")!=std::string::npos);
  reset(); chips[1].present=false; DualStorageBackend missingBackup; assert(missingBackup.begin());
  assert(logs.find("BACKUP  C32  0x56: NO ACK")!=std::string::npos && !backupOnline);
  puts("failover: verified latest RAM before routing, single NACK/bus immunity, bounded confirmation, old/high-generation C512 and stable failback PASS");
}
void publicationCutsAndReboots() {
  const unsigned appendBytes=sizeof(CriticalJournal::Record)+2;
  for(unsigned cut=0;cut<=appendBytes;++cut) {
    reset(); DualStorageBackend s; assert(s.begin()); PackedBatchV1 b{}; assert(s.loadBatch(b));
    b.elapsedSec=999; chips[0].budget=cut; (void)s.saveBatch(b); // PRIMARY lost during checkpoint write.
    assert(s.loadBatch(b) && b.elapsedSec==999); // Failed running checkpoint remains authoritative RAM.
    chips[0].present=false; step(s); step(s); step(s);
    assert(!s.primary() && latest(1).payload.batch.elapsedSec==999);
    chips[0].budget=-1; chips[0].present=true;
    DualStorageBackend boot; assert(boot.begin()); assert(boot.loadBatch(b) && b.elapsedSec==999);
  }
  for(unsigned cut=0;cut<=appendBytes;++cut) {
    reset(); DualStorageBackend s; assert(s.begin()); PackedBatchV1 b{}; assert(s.loadBatch(b));
    b.elapsedSec=999; assert(s.saveBatch(b));
    chips[0].present=false; chips[1].budget=cut; step(s); step(s); step(s);
    if(!s.primary()) assert(latest(1).payload.batch.elapsedSec==999); // Routing implies committed publication.
    chips[1].budget=-1;
    DualStorageBackend boot; assert(boot.begin()); assert(boot.loadBatch(b));
    assert(b.elapsedSec==300 || b.elapsedSec==999);
  }
  // Global power loss after every byte across PRIMARY + BACKUP publication and
  // one-time fences. Reboot never adopts a torn snapshot or resumes with NVS STOP.
  for(unsigned cut=0;cut<=2*appendBytes+16;++cut) {
    reset(); powerBudget=cut; DualStorageBackend migration; migration.begin(); powerBudget=-1;
    DualStorageBackend boot; assert(boot.begin()); PackedBatchV1 b{};
    assert(boot.loadBatch(b) && b.elapsedSec==300); assertStopBoot(boot);
    powerBudget=cut; PackedBatchV1 stopped{}; (void)boot.saveBatch(stopped); powerBudget=-1;
    for(unsigned mask=1;mask<=3;++mask) {
      chips[0].present=mask&1; chips[1].present=mask&2;
      DualStorageBackend stoppedBoot; stoppedBoot.begin(); assertStopBoot(stoppedBoot);
    }
  }
  printf("publication cuts: actual record %zu bytes; PRIMARY write/failover and both-chip migration/STOP reboot cuts PASS\n",sizeof(CriticalJournal::Record));
}
void asyncReasons() {
  reset(); PersistentStore s; assert(s.begin() && s.startWorker());
  PackedBatchV1 batch{}; assert(s.loadBatch(batch)); batch.elapsedSec+=300;
  const Chip beforePrimary=chips[0], beforeBackup=chips[1];
  assert(!s.saveBatch(batch) && s.pendingBatch());
  for(unsigned i=0;i<2;++i) {
    const auto &before=i?beforeBackup:beforePrimary;
    assert(chips[i].probes==before.probes && chips[i].reads==before.reads && chips[i].writes==before.writes);
  }
  s.workerStep(); assert(chips[1].writes==beforeBackup.writes);
  // Equal bytes but different policy reason must enqueue, not consume the old
  // checkpoint as a successful lifecycle synchronization.
  assert(!s.saveBatch(batch,BackupSyncReason::ResumeState) && s.pendingBatch());
  s.workerStep(); bool ok=false; BackupSyncReason reason=BackupSyncReason::NormalCheckpoint;
  assert(s.finishBatch(ok,&reason) && ok && reason==BackupSyncReason::ResumeState);
  assert(chips[1].writes>beforeBackup.writes && latest(1).payload.batch.elapsedSec==batch.elapsedSec);
  puts("async policy: caller performs zero EEPROM probes/reads/writes; reason-bound completion and resume publication PASS");
}
int main() {
  setvbuf(stdout,nullptr,_IONBF,0);
  standbyPolicy(); backupOutageAndStop(); failoverLatestAndReplacement(); publicationCutsAndReboots(); asyncReasons();
  reset(); chips[0].present=false; DualStorageBackend delayedMigration;
  assert(delayedMigration.begin() && !delayedMigration.primary());
  assert(!primaryRemindersPresent);
  chips[0].present=true; for(int i=0;i<4;++i) step(delayedMigration);
  assert(delayedMigration.primary() && primaryRemindersPresent && primaryReminderValue==12);
  reset(); primaryRemindersPresent=true; primaryReminderValue=77;
  DualStorageBackend preserveNew; assert(preserveNew.begin() && primaryReminderValue==77);
  reset(); DualStorageBackend store; assert(store.begin() && store.primary());
  MachineConfig c; PackedBatchV1 batch{}; assert(store.loadConfig(c) && c.value==37);
  // Migration preserves all legacy critical bytes.
  for(unsigned i=0;i<0xb00;++i) assert(chips[1].bytes[i]==0xff);
  chips[0].present=false; step(store); assert(store.primary()); // one NACK is not failover
  chips[0].present=true; step(store); assert(store.primary());
  bus=true; for(int i=0;i<6;++i) step(store); assert(store.primary()); bus=false;
  chips[0].present=false; step(store); step(store); assert(store.primary()); step(store);
  assert(!store.primary() && store.ready());
  batch.elapsedSec=900; batch.wasRunning=1; assert(store.saveBatch(batch));
  ReminderSet r, out; assert(!store.saveReminders(r,out));
  chips[0].present=true; for(int i=0;i<3;++i) { step(store); assert(!store.primary()); }
  chips[0].corrupt=true; step(store); assert(!store.primary()); // ACK but write verify fails
  chips[0].corrupt=false; for(int i=0;i<4;++i) step(store); assert(store.primary());
  DualStorageBackend reboot; assert(reboot.begin());
  assert(reboot.loadBatch(batch) && batch.elapsedSec==900);
  // Write-only failures also trigger failover despite successful ACK probes.
  chips[0].corrupt=true; batch.elapsedSec=1200; assert(!reboot.saveBatch(batch));
  step(reboot); step(reboot); step(reboot); assert(!reboot.primary());
  assert(reboot.saveBatch(batch));
  // Cold boot chooses newer backup, syncs before publishing primary.
  chips[0].corrupt=false; DualStorageBackend newest; assert(newest.begin() && newest.primary());
  assert(newest.loadBatch(batch) && batch.elapsedSec==1200);
  // A stopped backup alone cannot release the NVS stop intent: C512 may
  // still contain running=1 and be the only available chip on the next boot.
  chips[0].present=false; step(newest); step(newest); step(newest);
  PackedBatchV1 stoppedOnBackup{};
  assert(!newest.saveBatch(stoppedOnBackup));
  assert(newest.loadBatch(batch) && batch.wasRunning==0);
  chips[0].present=true; for(int i=0;i<4;++i) step(newest);
  assert(newest.primary() && newest.saveBatch(stoppedOnBackup));
  // Reinserted backup has to receive current RAM before admission. Even a
  // replacement with a larger generation must not override the live state.
  auto currentBackup=chips[1];
  ExternalEeprom24xx replacement({4096,32,EEPROM_BACKUP_ADDRESS});
  CriticalJournal replacementJournal(replacement,{STORAGE_BACKUP_BASE,STORAGE_BACKUP_SLOTS,STORAGE_SLOT_BYTES});
  assert(replacementJournal.scan());
  CriticalSnapshot foreign=replacementJournal.latest.payload;
  foreign.batch.elapsedSec=9999; foreign.batch.wasRunning=1;
  assert(replacementJournal.append(foreign,10000));
  auto newerBackup=chips[1]; chips[1]=currentBackup;
  chips[1].present=false; step(newest); chips[1]=newerBackup; step(newest);
  DualStorageBackend reinserted; assert(reinserted.begin());
  assert(reinserted.loadBatch(batch) && batch.wasRunning==0);
  // Every-byte migration cut on C512, then reboot with intact legacy/backup.
  for(unsigned cut=0;cut<sizeof(CriticalJournal::Record)+3;++cut) {
    reset(); chips[0].budget=cut; DualStorageBackend migration; migration.begin();
    chips[0].budget=-1; DualStorageBackend recovered; assert(recovered.begin());
    assert(recovered.loadConfig(c) && c.value==37);
    assert(recovered.loadBatch(batch) && batch.elapsedSec==300);
  }
  // Power loss at every byte while syncing a newer backup into stale C512.
  for(unsigned cut=0;cut<sizeof(CriticalJournal::Record)+3;++cut) {
    reset(); DualStorageBackend active; assert(active.begin());
    chips[0].present=false; step(active); step(active); step(active); assert(!active.primary());
    batch.elapsedSec=777; batch.wasRunning=1; assert(active.saveBatch(batch));
    chips[0].present=true; chips[0].budget=cut;
    step(active); step(active); step(active); step(active);
    chips[0].budget=-1; DualStorageBackend restored; assert(restored.begin());
    assert(restored.loadBatch(batch) && batch.elapsedSec==777);
  }
  reset(); PersistentStore async; assert(async.begin()); assert(async.startWorker());
  c.value=40; MachineConfig result{};
  const unsigned before=chips[0].writes;
  assert(!async.saveConfig(c,result) && async.pendingConfig());
  assert(chips[0].writes==before); // control path cannot perform EEPROM I/O
  async.workerStep(); assert(async.pendingConfig()); // completion cannot race the caller outcome
  assert(async.saveConfig(c,result) && result.value==40);
  batch.elapsedSec=100; batch.wasRunning=1;
  assert(!async.saveBatch(batch)); async.workerStep();
  PackedBatchV1 stopped{};
  assert(!async.saveBatch(stopped) && async.pendingBatch()); // old checkpoint is NOT a STOP ACK
  async.workerStep(); assert(async.saveBatch(stopped));
  // STOP must keep NVS tombstone while the backup still contains running=1.
  batch.wasRunning=1; assert(!async.saveBatch(batch)); async.workerStep(); assert(async.saveBatch(batch));
  chips[1].present=false;
  assert(!async.saveBatch(stopped)); async.workerStep(); assert(!async.saveBatch(stopped));
  chips[1].present=true; clockMs+=5000; async.workerStep();
  assert(!async.saveBatch(stopped)); async.workerStep(); assert(async.saveBatch(stopped));
  // With journal corruption the migration fence blocks stale legacy resume.
  chips[0].bytes[STORAGE_JOURNAL_BASE+511]=0;
  std::fill(chips[0].bytes.begin()+STORAGE_JOURNAL_BASE,chips[0].bytes.begin()+0xd000,0xff);
  std::fill(chips[1].bytes.begin()+STORAGE_BACKUP_BASE,chips[1].bytes.begin()+4096,0xff);
  DualStorageBackend fenced; assert(!fenced.begin());
  puts("storage backend: migration/failback power cuts, NACK/bus isolation, stale reinsert, async ACK and STOP safety PASS");
}
