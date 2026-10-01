#include <cassert>
#include <cstdio>
#include <vector>
#include "../MAYAP_INDUSTRIAL_v4_0_0/storage_journal.h"
constexpr uint32_t EEPROM_CAPACITY_BYTES=4096;
constexpr uint8_t EEPROM_PAGE_SIZE=32;
constexpr uint16_t CONFIG_SCHEMA=12, BATCH_SCHEMA=3;
constexpr uint32_t EEPROM_PRIMARY_CAPACITY=65536, STORAGE_SERVICE_MS=5000;
constexpr uint16_t EEPROM_PRIMARY_PAGE=128, STORAGE_SLOT_BYTES=512,
 STORAGE_JOURNAL_BASE=0x1000, STORAGE_JOURNAL_SLOTS=96,
 STORAGE_BACKUP_BASE=0xc00, STORAGE_BACKUP_SLOTS=2, STORAGE_PRIMARY_FENCE=0, STORAGE_BACKUP_FENCE=0xb00;
constexpr uint8_t EEPROM_PRIMARY_ADDRESS=0x56, EEPROM_BACKUP_ADDRESS=0x57;
struct PackedMachineConfigV1 { uint32_t value; };
using MachineConfig=PackedMachineConfigV1;
struct PackedBatchV1 { uint32_t elapsed, wasRunning; };
struct ReminderSet { uint32_t value; };
void sanitizeMachineConfig(MachineConfig &) {}
MachineConfig packConfig(MachineConfig c) { return c; }
MachineConfig unpackConfig(MachineConfig c) { return c; }
static bool online=false, bus=false;
static uint32_t clockMs=0;
uint32_t millis() { return clockMs; }
bool mayapI2cBusFault() { return bus; }
void mayapStorageSetPrimaryOnline(bool b) { online=b; }
struct Chip {
  std::vector<uint8_t> bytes=std::vector<uint8_t>(65536,0xff);
  bool present=true, corrupt=false;
  int budget=-1;
  unsigned writes=0;
} chips[2];
class ExternalEeprom24xx {
 public:
  explicit ExternalEeprom24xx(MayapStorage::Geometry g) : id(g.address==EEPROM_PRIMARY_ADDRESS?0:1) {}
  uint8_t deviceAddress() const { return id==0?EEPROM_PRIMARY_ADDRESS:EEPROM_BACKUP_ADDRESS; }
  bool begin() { return chips[id].present && !bus; }
  bool readBytes(uint16_t a, void *out, size_t n) {
    if (!begin()) return false;
    memcpy(out,chips[id].bytes.data()+a,n); return true;
  }
  bool writeBytes(uint16_t a, const void *p, size_t n) {
    if (!begin()) return false;
    auto &c=chips[id];
    for (size_t i=0;i<n;++i) {
      if (!c.budget) return false;
      if(c.budget>0) --c.budget;
      c.bytes[a+i]=static_cast<const uint8_t *>(p)[i] ^ (c.corrupt?1:0); ++c.writes;
    }
    return true;
  }
  uint32_t takeSoftRetryEvents() { return 0; }
 private: unsigned id;
};
class LegacyPersistentStore {
 public:
  explicit LegacyPersistentStore(bool primary=false):primary_(primary) {}
  bool begin() { return chips[primary_?0:1].present; }
  bool loadConfig(MachineConfig &c) { c.value=37; return true; }
  bool loadBatch(PackedBatchV1 &b) { b.elapsed=300; b.wasRunning=1; return true; }
  bool loadReminders(ReminderSet &r) { r.value=12; return true; }
  bool saveReminders(const ReminderSet &r, ReminderSet &out) { assert(primary_); out=r; return true; }
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
void reset() { chips[0]=Chip{}; chips[1]=Chip{}; bus=false; clockMs=0; }
int main() {
  reset(); DualStorageBackend store; assert(store.begin() && store.primary());
  MachineConfig c; PackedBatchV1 batch; assert(store.loadConfig(c) && c.value==37);
  // Migration preserves all legacy critical bytes.
  for(unsigned i=0;i<0xb00;++i) assert(chips[1].bytes[i]==0xff);
  chips[0].present=false; step(store); assert(store.primary()); // one NACK is not failover
  chips[0].present=true; step(store); assert(store.primary());
  bus=true; for(int i=0;i<6;++i) step(store); assert(store.primary()); bus=false;
  chips[0].present=false; step(store); step(store); assert(store.primary()); step(store);
  assert(!store.primary() && store.ready());
  batch.elapsed=900; batch.wasRunning=1; assert(store.saveBatch(batch));
  ReminderSet r, out; assert(!store.saveReminders(r,out));
  chips[0].present=true; for(int i=0;i<3;++i) { step(store); assert(!store.primary()); }
  chips[0].corrupt=true; step(store); assert(!store.primary()); // ACK but write verify fails
  chips[0].corrupt=false; for(int i=0;i<4;++i) step(store); assert(store.primary());
  DualStorageBackend reboot; assert(reboot.begin());
  assert(reboot.loadBatch(batch) && batch.elapsed==900);
  // Write-only failures also trigger failover despite successful ACK probes.
  chips[0].corrupt=true; batch.elapsed=1200; assert(!reboot.saveBatch(batch));
  step(reboot); step(reboot); step(reboot); assert(!reboot.primary());
  assert(reboot.saveBatch(batch));
  // Cold boot chooses newer backup, syncs before publishing primary.
  chips[0].corrupt=false; DualStorageBackend newest; assert(newest.begin() && newest.primary());
  assert(newest.loadBatch(batch) && batch.elapsed==1200);
  // Every-byte migration cut on C512, then reboot with intact legacy/backup.
  for(unsigned cut=0;cut<sizeof(CriticalJournal::Record)+3;++cut) {
    reset(); chips[0].budget=cut; DualStorageBackend migration; migration.begin();
    chips[0].budget=-1; DualStorageBackend recovered; assert(recovered.begin());
    assert(recovered.loadConfig(c) && c.value==37);
    assert(recovered.loadBatch(batch) && batch.elapsed==300);
  }
  // Power loss at every byte while syncing a newer backup into stale C512.
  for(unsigned cut=0;cut<sizeof(CriticalJournal::Record)+3;++cut) {
    reset(); DualStorageBackend active; assert(active.begin());
    chips[0].present=false; step(active); step(active); step(active); assert(!active.primary());
    batch.elapsed=777; batch.wasRunning=1; assert(active.saveBatch(batch));
    chips[0].present=true; chips[0].budget=cut;
    step(active); step(active); step(active); step(active);
    chips[0].budget=-1; DualStorageBackend restored; assert(restored.begin());
    assert(restored.loadBatch(batch) && batch.elapsed==777);
  }
  reset(); PersistentStore async; assert(async.begin()); assert(async.startWorker());
  c.value=40; MachineConfig result{};
  const unsigned before=chips[0].writes;
  assert(!async.saveConfig(c,result) && async.pendingConfig());
  assert(chips[0].writes==before); // control path cannot perform EEPROM I/O
  async.workerStep(); assert(!async.pendingConfig());
  assert(async.saveConfig(c,result) && result.value==40);
  batch.elapsed=100; batch.wasRunning=1;
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
  puts("storage backend: migration power cuts, single/bus NACK, verified failback, newer backup PASS");
}
