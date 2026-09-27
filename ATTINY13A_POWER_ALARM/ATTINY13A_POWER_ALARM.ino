// ATtiny13A backup power-alarm controller - pulse-width protocol v4.
// PB0 BUS open-drain to ESP32, PB1 siren drive, PB2 3V3_ESP sense,
// PB3 9V siren-supply sense. AVR-libc only; target ATtiny13A @ 1.2 MHz.
#define F_CPU 1200000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <util/delay.h>

constexpr uint8_t PROTOCOL_VERSION = 4U;
constexpr uint8_t PIN_BUS = PB0;
constexpr uint8_t PIN_SIREN = PB1;
constexpr uint8_t PIN_3V3 = PB2;
constexpr uint8_t PIN_9V = PB3;

// ============================================================================
// FIELD CALIBRATION - NGUONG PHAT HIEN NGUON (GHI CHU, CHUA DIEU KHIEN LOGIC)
// ============================================================================
// PB2/PB3 hien dang doc DIGITAL + PCINT. Vi vay nguong chuyen HIGH/LOW thuc te
// phu thuoc vao:
//   1) ty le cau chia dien ap tren PCB,
//   2) VIH/VIL + hysteresis cua input ATtiny13A,
//   3) VCC ATtiny tai thoi diem do.
// Bon hang so ben duoi CHI DE GHI LAI KET QUA HIEU CHINH TREN MAY THAT; gia
// tri 0 = CHUA DO. Chung CHUA duoc dung trong espPowerOk()/nineVoltOk(), nen
// thay doi cac so nay KHONG tu lam thay doi nguong bao. Sau khi bench-test,
// bao lai 4 moc nay de quyet dinh giu digital + chot divider hay chuyen ADC.
//
// QUY UOC DO: ghi dien ap NGUON THUC TE TRUOC CAU CHIA, don vi mV.
// 3V3_LOSS    : ha tu tu 3.3V xuong -> PB2 vua doi sang LOW.
// 3V3_RESTORE : tang tu tu tu 0V len -> PB2 vua doi sang HIGH.
// 9V_LOW      : ha tu tu nguon coi xuong -> PB3 vua doi sang LOW / E502 bat.
// 9V_OK       : tang tu tu nguon coi len -> PB3 vua doi sang HIGH / E502 xoa.
constexpr uint16_t FIELD_MEASURED_3V3_LOSS_MV    = 0U;  // TODO bench calibration
constexpr uint16_t FIELD_MEASURED_3V3_RESTORE_MV = 0U;  // TODO bench calibration
constexpr uint16_t FIELD_MEASURED_9V_LOW_MV      = 0U;  // TODO bench calibration
constexpr uint16_t FIELD_MEASURED_9V_OK_MV       = 0U;  // TODO bench calibration

constexpr uint8_t BIT_ZERO_MS = 10U;
constexpr uint8_t BIT_ONE_MS = 30U;
constexpr uint8_t BIT_GAP_MS = 15U;
constexpr uint8_t RESPONSE_GAP_MS = 40U;
constexpr uint8_t PREAMBLE_MS = 60U;
constexpr uint8_t MSG_BATCH_START = 1U;
constexpr uint8_t MSG_BATCH_END = 2U;
constexpr uint8_t MSG_SIREN_ON = 3U;
constexpr uint8_t MSG_SIREN_OFF = 4U;
constexpr uint8_t MSG_STATUS_QUERY = 5U;
constexpr uint8_t MSG_ACTIVITY_ON = 6U;
constexpr uint8_t MSG_ACTIVITY_OFF = 7U;
constexpr uint8_t MSG_STATUS_BASE = 8U;
constexpr uint8_t MSG_STATUS_MAX = 23U;
constexpr uint8_t FLAG_BATCH = 1U;
constexpr uint8_t FLAG_9V_LOW = 2U;
constexpr uint8_t FLAG_SIREN = 4U;
constexpr uint8_t FLAG_ACTIVITY = 8U;

uint8_t EEMEM eeBatchState;
uint8_t EEMEM eeBatchStateInv;
uint8_t EEMEM eeActivityState;
uint8_t EEMEM eeActivityStateInv;
static bool batchActive;
static bool criticalActivity;
static bool emergencySiren;

static inline void delayMs(uint16_t ms) { while (ms--) { _delay_ms(1); wdt_reset(); } }
static inline void busRelease() { DDRB &= static_cast<uint8_t>(~_BV(PIN_BUS)); PORTB &= static_cast<uint8_t>(~_BV(PIN_BUS)); }
static inline void busLow() { PORTB &= static_cast<uint8_t>(~_BV(PIN_BUS)); DDRB |= _BV(PIN_BUS); }
static inline bool isBusLow() { return (PINB & _BV(PIN_BUS)) == 0U; }
static inline bool espPowerOk() { return (PINB & _BV(PIN_3V3)) != 0U; }
static inline bool nineVoltOk() { return (PINB & _BV(PIN_9V)) != 0U; }
static inline void setSiren(bool on) { if (on) PORTB |= _BV(PIN_SIREN); else PORTB &= static_cast<uint8_t>(~_BV(PIN_SIREN)); }

static bool loadState(const uint8_t *stateAddr, const uint8_t *invAddr) {
  const uint8_t v = eeprom_read_byte(stateAddr);
  const uint8_t n = eeprom_read_byte(invAddr);
  if ((v ^ n) == 0xFFU && v <= 1U) return v != 0U;
  return true;  // Record rach/chua hop le: fail-safe = ARMED.
}

static bool saveState(uint8_t *stateAddr, uint8_t *invAddr, bool on) {
  const uint8_t v = on ? 1U : 0U;
  eeprom_update_byte(stateAddr, v);
  eeprom_update_byte(invAddr, static_cast<uint8_t>(~v));
  return eeprom_read_byte(stateAddr) == v &&
         eeprom_read_byte(invAddr) == static_cast<uint8_t>(~v);
}

static uint8_t receiveCommand() {
  uint16_t width = 0U;
  while (isBusLow() && width <= 530U) { _delay_ms(1); ++width; wdt_reset(); }
  if (isBusLow()) return 0U;
  // Windows tolerate RC oscillator error while rejecting gaps and glitches.
  if (width >= 20U && width <= 38U) return MSG_BATCH_START;
  if (width >= 47U && width <= 65U) return MSG_BATCH_END;
  if (width >= 78U && width <= 105U) return MSG_SIREN_ON;
  if (width >= 124U && width <= 160U) return MSG_SIREN_OFF;
  if (width >= 185U && width <= 240U) return MSG_STATUS_QUERY;
  if (width >= 270U && width <= 350U) return MSG_ACTIVITY_ON;
  if (width >= 390U && width <= 510U) return MSG_ACTIVITY_OFF;
  return 0U;
}

static void sendStatus(uint8_t flags) {
  delayMs(RESPONSE_GAP_MS);
  busLow(); delayMs(PREAMBLE_MS); busRelease(); delayMs(BIT_GAP_MS);
  uint8_t parity = 0U;
  for (uint8_t i = 0U; i < 4U; ++i) {
    const uint8_t bit = static_cast<uint8_t>((flags >> i) & 1U);
    parity ^= bit;
    busLow(); delayMs(bit ? BIT_ONE_MS : BIT_ZERO_MS);
    busRelease(); delayMs(BIT_GAP_MS);
  }
  busLow(); delayMs(parity ? BIT_ONE_MS : BIT_ZERO_MS); busRelease();
}

static inline uint8_t statusCode() {
  uint8_t flags = batchActive ? FLAG_BATCH : 0U;
  if (!nineVoltOk()) flags |= FLAG_9V_LOW;
  if (emergencySiren) flags |= FLAG_SIREN;
  if (criticalActivity) flags |= FLAG_ACTIVITY;
  return flags;
}

static inline void updateSiren() {
  const bool armedForPowerLoss = batchActive || criticalActivity;
  setSiren(emergencySiren || (armedForPowerLoss && !espPowerOk()));
}

static inline void configureWakeMask(bool espOn) {
  // ESP power absent: PB0 stays Hi-Z to avoid holding/back-powering the net.
  busRelease();
  if (espOn) {
    PCMSK = _BV(PIN_BUS) | _BV(PIN_3V3) | _BV(PIN_9V);
  } else {
    PCMSK = _BV(PIN_3V3) | _BV(PIN_9V);
  }
}

ISR(PCINT0_vect) {}

static void sleepUntilPinChange() {
  cli();
  GIFR |= _BV(PCIF); // Remove stale pin-change indication before sleep.
  const bool espOn = espPowerOk();
  configureWakeMask(espOn);
  updateSiren();
  if (espPowerOk() != espOn || (espOn && isBusLow())) {
    sei();
    return;
  }
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sei();
  sleep_cpu();
  sleep_disable();
}

int main(void) {
  MCUSR = 0U;
  wdt_disable();
  DDRB = _BV(PIN_SIREN);
  PORTB = 0U;
  // Khong dung ADC/comparator: tat ro rang de giam dong nen. PB2/PB3 van la
  // digital input + PCINT, khong bi anh huong. BOD la fuse va phai bench-test.
#ifdef ACD
  ACSR |= _BV(ACD);
#endif
#ifdef PRADC
  PRR |= _BV(PRADC);
#endif
  batchActive = loadState(&eeBatchState, &eeBatchStateInv);
  criticalActivity = loadState(&eeActivityState, &eeActivityStateInv);
  emergencySiren = false;
  updateSiren();
  configureWakeMask(espPowerOk());
  GIMSK |= _BV(PCIE);
  sei();

  for (;;) {
    const bool espOn = espPowerOk();
    configureWakeMask(espOn);
    updateSiren();

    if (espOn && isBusLow()) {
      wdt_enable(WDTO_2S);
      const uint8_t code = receiveCommand();
      bool ok = false;
      if (code == MSG_BATCH_START) {
        ok = saveState(&eeBatchState, &eeBatchStateInv, true); if (ok) batchActive = true;
      } else if (code == MSG_BATCH_END) {
        ok = saveState(&eeBatchState, &eeBatchStateInv, false); if (ok) batchActive = false;
      } else if (code == MSG_SIREN_ON) {
        emergencySiren = true; ok = true;
      } else if (code == MSG_SIREN_OFF) {
        emergencySiren = false; ok = true;
      } else if (code == MSG_STATUS_QUERY) {
        ok = true;
      } else if (code == MSG_ACTIVITY_ON) {
        ok = saveState(&eeActivityState, &eeActivityStateInv, true);
        if (ok) criticalActivity = true;
      } else if (code == MSG_ACTIVITY_OFF) {
        ok = saveState(&eeActivityState, &eeActivityStateInv, false);
        if (ok) criticalActivity = false;
      }
      updateSiren();
      if (ok) sendStatus(statusCode());
      wdt_disable();
    }

    sleepUntilPinChange();
  }
}
