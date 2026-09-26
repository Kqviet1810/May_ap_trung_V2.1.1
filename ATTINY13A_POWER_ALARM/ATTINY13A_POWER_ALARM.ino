// ATtiny13A backup power-alarm controller - protocol v3 wire-hardening.
// PB0 BUS open-drain to ESP32, PB1 siren drive, PB2 3V3_ESP sense,
// PB3 9V siren-supply sense. AVR-libc only; target ATtiny13A @ 1.2 MHz.
#define F_CPU 1200000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <util/delay.h>

constexpr uint8_t PROTOCOL_VERSION = 3U;
constexpr uint8_t PIN_BUS = PB0;
constexpr uint8_t PIN_SIREN = PB1;
constexpr uint8_t PIN_3V3 = PB2;
constexpr uint8_t PIN_9V = PB3;

// ============================================================================
// FIELD CALIBRATION - NGUONG PHAT HIEN NGUON (GHI CHU, CHUA DIEU KHIEN LOGIC)
// ============================================================================
// PB2/PB3 hien dang doc DIGITAL + PCINT. Vi vay nguong chuyen HIGH/LOW thuc te
// phu thuoc vao cau chia dien ap + VIH/VIL + VCC ATtiny. Cac hang so nay chi
// ghi lai ket qua bench; hien tai KHONG tham gia espPowerOk()/nineVoltOk().
constexpr uint16_t FIELD_MEASURED_3V3_LOSS_MV     = 0U;
constexpr uint16_t FIELD_MEASURED_3V3_RESTORE_MV  = 0U;
constexpr uint16_t FIELD_MEASURED_9V_LOW_MV       = 0U;
constexpr uint16_t FIELD_MEASURED_9V_OK_MV        = 0U;

constexpr uint16_t PULSE_MS = 30U;
constexpr uint16_t ACTIVITY_MARK_PULSE_MS = 60U;
constexpr uint16_t MIN_PULSE_MS = 15U;
constexpr uint16_t END_GAP_MS = 150U;
constexpr uint16_t ACK_TIMEOUT_MS = 350U;
constexpr uint8_t MSG_BATCH_START = 1U;
constexpr uint8_t MSG_BATCH_END = 2U;
constexpr uint8_t MSG_SIREN_ON = 3U;
constexpr uint8_t MSG_SIREN_OFF = 4U;
constexpr uint8_t MSG_STATUS_QUERY = 5U;
constexpr uint8_t MSG_ACTIVITY_ON = 6U;
constexpr uint8_t MSG_ACTIVITY_OFF = 7U;
constexpr uint8_t MSG_STATUS_BASE = 8U;
constexpr uint8_t MSG_STATUS_MAX = 23U;       // logical status seen by ESP
constexpr uint8_t STATUS_WIRE_MAX = 15U;      // physical pulse count
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

static inline void delayMs(uint16_t ms) {
  while (ms--) {
    _delay_ms(1);
    wdt_reset();
  }
}

// Open-drain BUS: never actively drive HIGH.
static inline void busRelease() {
  DDRB &= static_cast<uint8_t>(~_BV(PIN_BUS));
  PORTB &= static_cast<uint8_t>(~_BV(PIN_BUS));
}
static inline void busLow() {
  PORTB &= static_cast<uint8_t>(~_BV(PIN_BUS));
  DDRB |= _BV(PIN_BUS);
}
static inline bool isBusLow() { return (PINB & _BV(PIN_BUS)) == 0U; }
static inline bool espPowerOk() { return (PINB & _BV(PIN_3V3)) != 0U; }
static inline bool nineVoltOk() { return (PINB & _BV(PIN_9V)) != 0U; }
static inline void setSiren(bool on) {
  if (on) PORTB |= _BV(PIN_SIREN);
  else PORTB &= static_cast<uint8_t>(~_BV(PIN_SIREN));
}

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

static void sendAck() {
  busLow();
  delayMs(PULSE_MS);
  busRelease();
}

static uint8_t receiveCommand() {
  uint8_t pulses = 0U;
  for (;;) {
    if (isBusLow()) {
      uint16_t low = 0U;
      while (isBusLow() && low < PULSE_MS * 3U) {
        _delay_ms(1);
        ++low;
        wdt_reset();
      }
      if (isBusLow()) return 0U;
      if (low >= MIN_PULSE_MS && ++pulses > MSG_ACTIVITY_OFF) return 0U;
    }

    uint16_t high = 0U;
    while (!isBusLow() && high < END_GAP_MS) {
      _delay_ms(1);
      ++high;
      wdt_reset();
    }
    if (high >= END_GAP_MS) return pulses;
  }
}

// Logical STATUS remains 8..23, but physical frame is ALWAYS <=15 pulses:
//   count 8..15 = lower three flags (batch / 9V-low / siren)
//   first LOW 30ms = activity OFF, 60ms = activity ON
// ESP reconstructs the original logical activity bit, so upper layers do not
// change at all. One frame, one ACK; no split transaction/state timeout.
static void sendStatus(uint8_t logicalCode) {
  if (logicalCode < MSG_STATUS_BASE || logicalCode > MSG_STATUS_MAX) return;

  const uint8_t flags = static_cast<uint8_t>(logicalCode - MSG_STATUS_BASE);
  const uint8_t wireCode = static_cast<uint8_t>(
      MSG_STATUS_BASE + (flags & static_cast<uint8_t>(FLAG_BATCH | FLAG_9V_LOW | FLAG_SIREN)));
  if (wireCode > STATUS_WIRE_MAX) return;
  const bool activity = (flags & FLAG_ACTIVITY) != 0U;

  delayMs(END_GAP_MS + 20U);
  for (uint8_t i = 0U; i < wireCode; ++i) {
    busLow();
    delayMs((i == 0U && activity) ? ACTIVITY_MARK_PULSE_MS : PULSE_MS);
    busRelease();
    delayMs(PULSE_MS);
  }

  // Wait bounded time for ESP ACK. ACK result is diagnostic only: if it is
  // lost, ESP's own status timeout/resync policy will query again later.
  uint16_t wait = 0U;
  while (!isBusLow() && wait < ACK_TIMEOUT_MS) {
    _delay_ms(1);
    ++wait;
    wdt_reset();
  }
  if (isBusLow()) {
    uint16_t low = 0U;
    while (isBusLow() && low < PULSE_MS * 3U) {
      _delay_ms(1);
      ++low;
      wdt_reset();
    }
  }
}

static inline uint8_t statusCode() {
  uint8_t flags = batchActive ? FLAG_BATCH : 0U;
  if (!nineVoltOk()) flags |= FLAG_9V_LOW;
  if (emergencySiren) flags |= FLAG_SIREN;
  if (criticalActivity) flags |= FLAG_ACTIVITY;
  return static_cast<uint8_t>(MSG_STATUS_BASE + flags);
}

static inline void updateSiren() {
  const bool armedForPowerLoss = batchActive || criticalActivity;
  setSiren(emergencySiren || (armedForPowerLoss && !espPowerOk()));
}

// IMPORTANT: when ESP is OFF the BUS must be Hi-Z. Pulling PB0 LOW while the
// ESP 3V3 rail is absent can hold/back-power the shared net and was a real
// architectural bug in the old code.
static inline void configureWakeMask(bool espOn) {
  busRelease();
  if (espOn) {
    PCMSK = _BV(PIN_BUS) | _BV(PIN_3V3) | _BV(PIN_9V);
  } else {
    PCMSK = _BV(PIN_3V3) | _BV(PIN_9V);
  }
}

ISR(PCINT0_vect) {}

// Race-free power-down entry. No post-wake 50ms delay: a BUS command uses
// 30ms LOW pulses, so the old debounce delay could consume the entire first
// pulse after wake. The SEI->SLEEP sequence is the AVR-recommended atomic
// pattern: SLEEP executes as the instruction immediately following SEI.
static void sleepUntilPinChange() {
  cli();

  // Clear stale PCINT first, then sample current levels and install the mask.
  GIFR |= _BV(PCIF);
  const bool espOn = espPowerOk();
  configureWakeMask(espOn);
  updateSiren();

  // If a command is already LOW, do not sleep. Re-read power as well so a
  // transition occurring during setup is handled in the next loop now.
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
  busRelease();

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
      bool sendState = false;

      if (code == MSG_BATCH_START) {
        ok = saveState(&eeBatchState, &eeBatchStateInv, true);
        if (ok) batchActive = true;
      } else if (code == MSG_BATCH_END) {
        ok = saveState(&eeBatchState, &eeBatchStateInv, false);
        if (ok) batchActive = false;
      } else if (code == MSG_SIREN_ON) {
        emergencySiren = true;
        ok = true;
      } else if (code == MSG_SIREN_OFF) {
        emergencySiren = false;
        ok = true;
      } else if (code == MSG_STATUS_QUERY) {
        ok = true;
        sendState = true;
      } else if (code == MSG_ACTIVITY_ON) {
        ok = saveState(&eeActivityState, &eeActivityStateInv, true);
        if (ok) criticalActivity = true;
      } else if (code == MSG_ACTIVITY_OFF) {
        ok = saveState(&eeActivityState, &eeActivityStateInv, false);
        if (ok) criticalActivity = false;
      }

      updateSiren();
      if (ok) {
        sendAck();
        if (sendState) sendStatus(statusCode());
      }
      wdt_disable();
    }

    updateSiren();
    sleepUntilPinChange();
  }
}
