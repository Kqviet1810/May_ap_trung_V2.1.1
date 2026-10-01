#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
using std::isfinite;
constexpr uint16_t TEMP_HISTORY_SLOT_COUNT=288, TEMP_HISTORY_RECORD_BYTES=4,
 TEMP_HISTORY_SAMPLE_SEC=300, EEPROM_ADDR_TEMP_HISTORY=0xb00,
 TEMP_HISTORY_STORAGE_BYTES=1152, EEPROM_PRIMARY_PAGE=128;
constexpr uint8_t EEPROM_PRIMARY_ADDRESS=0x56;
constexpr uint32_t I2C_STORAGE_LOCK_TIMEOUT_MS=2, EEPROM_WRITE_TIMEOUT_MS=20;
static bool primary=true, locked=false;
static uint32_t clockMs=0, transactions=0;
bool mayapStoragePrimaryOnline() { return primary; }
uint32_t millis() { return clockMs; }
void delay(unsigned n) { assert(!locked); clockMs+=n; }
bool mayapI2cLock(uint32_t) { assert(!locked); locked=true; return true; }
void mayapI2cUnlock() { assert(locked); locked=false; }
struct FakeWire {
  std::vector<uint8_t> bytes=std::vector<uint8_t>(65536,0xff),tx;
  std::deque<uint8_t> rx;
  uint16_t address=0;
  void beginTransmission(uint8_t device) { assert(locked && device==EEPROM_PRIMARY_ADDRESS); tx.clear(); }
  size_t write(uint8_t v) { tx.push_back(v); return 1; }
  size_t write(const uint8_t *p,size_t n) { tx.insert(tx.end(),p,p+n); return n; }
  uint8_t endTransmission(bool) {
    ++transactions;
    if(tx.size()>=2) address=(uint16_t(tx[0])<<8)|tx[1];
    for(unsigned i=2;i<tx.size();++i) bytes[address+i-2]=tx[i];
    return 0;
  }
  size_t requestFrom(uint8_t,uint8_t n,uint8_t) {
    assert(locked); for(unsigned i=0;i<n;++i) rx.push_back(bytes[address+i]); return n;
  }
  int available() { return int(rx.size()); }
  int read() { int n=rx.front();rx.pop_front();return n; }
} Wire;
#include "actual-history.inc"
int main() {
  const uint32_t epoch=1800000000;
  mayapTemperatureHistorySample(epoch,37.5f,true);
  assert(transactions==0); // caller/control path only writes RAM
  mayapTemperatureHistoryService();
  const unsigned before=transactions;
  MayapTemperatureHistoryPoint point;
  assert(mayapTemperatureHistoryReadStatus(epoch/300,point)==2);
  assert(point.epoch==epoch && point.temperatureX10==375);
  assert(transactions==before); // MQTT never does I2C
  primary=false;
  mayapTemperatureHistorySample(epoch+300,38.5f,true); mayapTemperatureHistoryService();
  assert(mayapTemperatureHistoryReadStatus(epoch/300,point)==0 && transactions==before);
  primary=true;
  for(unsigned i=0;i<TEMP_HISTORY_SLOT_COUNT;++i) mayapTemperatureHistoryService();
  assert(mayapTemperatureHistoryReadStatus(epoch/300,point)==2);
  const uint16_t address=MayapTemperatureHistoryInternal::addressForBucket(epoch/300);
  Wire.bytes[address+3]^=1;
  primary=false; mayapTemperatureHistoryService(); primary=true;
  for(unsigned i=0;i<TEMP_HISTORY_SLOT_COUNT;++i) mayapTemperatureHistoryService();
  assert(mayapTemperatureHistoryReadStatus(epoch/300,point)==1);
  puts("storage history: control/network RAM-only, backup suppression, reload and CRC PASS");
}
