#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
constexpr uint32_t RTC_READ_PERIOD_MS=1000, RTC_STUCK_TIMEOUT_MS=4000,
 RTC_AUTO_REPAIR_RETRY_MS=3000, RTC_AUTO_REPAIR_MAX_GAP_SEC=86400;
constexpr uint16_t RTC_VALID_YEAR_MIN=2024, RTC_VALID_YEAR_MAX=2099,
 RTC_BUS_CONTENTION_RETRY_MS=20;
constexpr uint8_t RTC_AUTO_REPAIR_CONFIRM_READS=2, RTC_AUTO_REPAIR_MAX_ATTEMPTS=3, RTC_I2C_ADDRESS=0x68;
constexpr bool RTC_AUTO_REPAIR_ENABLED=true;
static uint32_t clockMs=1000, reports=0;
static bool busBusy=false, locked=false, nack=false;
uint32_t millis() { return clockMs; }
uint32_t elapsedMs(uint32_t a,uint32_t b) { return a-b; }
bool mayapI2cLock(uint32_t timeout) { assert(timeout==0); if(busBusy) return false; assert(!locked); locked=true; return true; }
void mayapI2cUnlock() { assert(locked); locked=false; }
void mayapI2cReport(uint8_t,bool) { ++reports; }
struct FakeWire {
  uint8_t reg=0;
  std::deque<uint8_t> rx;
  void beginTransmission(uint8_t address) { assert(locked && address==RTC_I2C_ADDRESS); }
  size_t write(uint8_t value) { reg=value; return 1; }
  size_t write(const uint8_t *,size_t length) { return length; }
  uint8_t endTransmission(bool) { return nack ? 2 : 0; }
  size_t requestFrom(uint8_t,uint8_t length,bool) {
    const uint8_t data[]={uint8_t(clockMs/1000%10),0,0,4,1,0x10,0x26};
    if(reg==0) for(unsigned i=0;i<length;++i) rx.push_back(data[i]);
    else for(unsigned i=0;i<length;++i) rx.push_back(0); // OSF clear
    return length;
  }
  int available() { return int(rx.size()); }
  int read() { int value=rx.front();rx.pop_front();return value; }
} Wire;
#include "actual-storage-rtc.inc"
int main() {
  RtcDs3231 rtc; rtc.begin(clockMs); assert(rtc.valid() && rtc.online());
  const uint32_t before=reports;
  busBusy=true;
  for(unsigned i=0;i<5;++i) { clockMs+=20; rtc.update(clockMs+1000); }
  assert(rtc.valid() && rtc.online() && reports==before); // no lock wait, no false NACK
  busBusy=false; clockMs=2100; rtc.update(clockMs); assert(rtc.valid());
  nack=true;
  for(unsigned i=0;i<3;++i) { clockMs+=1000; rtc.update(clockMs); }
  assert(!rtc.valid() && !rtc.online()); // real device failures still count
  nack=false; clockMs+=1000; rtc.update(clockMs); assert(rtc.valid() && rtc.online());
  busBusy=true; clockMs+=5000; rtc.update(clockMs); assert(!rtc.valid()); // do not hide indefinitely stale RTC
  assert(!locked);
  puts("storage RTC: nonblocking bus contention, retry, real NACK and stale clock PASS");
}
