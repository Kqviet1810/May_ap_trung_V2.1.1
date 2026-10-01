#include <cassert>
#include <cstdio>
#include <vector>
#include <deque>
#include <algorithm>
#include "../MAYAP_INDUSTRIAL_v4_0_0/storage_journal.h"
constexpr uint32_t EEPROM_CAPACITY_BYTES=4096;
constexpr uint8_t EEPROM_PAGE_SIZE=32, EEPROM_MAX_TRANSFER_BYTES=126;
constexpr uint8_t EEPROM_BACKUP_ADDRESS=0x57, EEPROM_IO_RETRIES=2;
constexpr uint32_t EEPROM_RETRY_GAP_MS=2, I2C_STORAGE_LOCK_TIMEOUT_MS=2, EEPROM_WRITE_TIMEOUT_MS=20;
static bool locked=false, nack=false;
static uint32_t clockMs=0, transactions=0;
uint32_t millis() { return clockMs; }
uint32_t elapsedMs(uint32_t a,uint32_t b) { return a-b; }
uint32_t pdMS_TO_TICKS(uint32_t n) { return n; }
void vTaskDelay(uint32_t n) { assert(!locked); clockMs+=n; }
bool mayapI2cLock(uint32_t) { assert(!locked); locked=true; return true; }
void mayapI2cUnlock() { assert(locked); locked=false; }
void mayapI2cReport(uint8_t,bool) {}
struct FakeWire {
  std::vector<uint8_t> bytes[2]={std::vector<uint8_t>(65536,0xff),std::vector<uint8_t>(4096,0xff)};
  std::vector<uint8_t> tx;
  std::deque<uint8_t> rx;
  uint16_t address=0;
  unsigned id=0;
  uint32_t busyUntil[2]={0,0};
  void beginTransmission(uint8_t device) { assert(locked); id=device==0x56?0:1; tx.clear(); }
  size_t write(uint8_t v) { tx.push_back(v); return 1; }
  size_t write(const uint8_t *p,size_t n) { assert(tx.size()+n<=128); tx.insert(tx.end(),p,p+n); return n; }
  uint8_t endTransmission(bool) {
    ++transactions;
    if(nack || clockMs<busyUntil[id]) return 2;
    if(tx.size()>=2) address=(uint16_t(tx[0])<<8)|tx[1];
    if(tx.size()>2) {
      const unsigned page=id?32:128;
      assert(address/page==(address+tx.size()-3)/page);
      assert(address+tx.size()-2<=bytes[id].size());
      for(unsigned i=2;i<tx.size();++i) bytes[id][address+i-2]=tx[i];
      busyUntil[id]=clockMs+5;
    }
    return 0;
  }
  size_t requestFrom(uint8_t,uint8_t n,bool) {
    assert(locked);
    for(unsigned i=0;i<n;++i) rx.push_back(bytes[id][address+i]);
    return n;
  }
  int available() { return int(rx.size()); }
  int read() { int n=rx.front();rx.pop_front();return n; }
} Wire;
#include "actual-driver.inc"
int main() {
  ExternalEeprom24xx primary({65536,128,0x56}), backup({4096,32,0x57});
  uint8_t input[259], output[259]; for(unsigned i=0;i<sizeof(input);++i) input[i]=uint8_t(i);
  for(unsigned a : {0U,31U,32U,127U,128U,65277U}) {
    assert(primary.writeBytes(a,input,sizeof(input)));
    assert(primary.readBytes(a,output,sizeof(output))); assert(!memcmp(input,output,sizeof(input)));
  }
  assert(!primary.writeBytes(65535,input,2));
  assert(primary.writeBytes(65535,input,1));
  assert(backup.writeBytes(3837,input,sizeof(input))); assert(!backup.writeBytes(4095,input,2));
  assert(backup.readBytes(3837,output,sizeof(output))); assert(!memcmp(input,output,sizeof(input)));
  nack=true; const unsigned before=transactions;
  assert(!primary.writeBytes(0,input,sizeof(input))); assert(transactions-before==EEPROM_IO_RETRIES);
  assert(!locked);
  puts("storage driver: geometry, page boundaries, 64KiB end, bounded retries, unlocked ACK wait PASS");
}
