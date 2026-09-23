from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding='utf-8')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected 1 match, found {count}')
    return text.replace(old, new, 1)


def replace_re(text: str, pattern: str, repl: str, label: str) -> str:
    out, count = re.subn(pattern, repl, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f'{label}: expected 1 regex match, found {count}')
    return out


# ---------------------------------------------------------------------------
# config.h: protocol v3. Keep 9V measurement; add activity commands.
# ---------------------------------------------------------------------------
path = 'MAYAP_INDUSTRIAL_v3_4_0/config.h'
text = read(path)
text = replace_re(
    text,
    r'// ATtiny Link v2: ESP32 la master; status frame Tiny->ESP la 6\.\.13\..*?constexpr uint32_t ATTINY_9V_CONFIRM_MS = 3000UL;\n',
    '''// ATtiny Link v3: ESP32 la master. Giu nguyen do nguon 9V/E502; them
// CRITICAL_ACTIVITY cho tai quan trong dang chay NGOAI ME. Activity chi nam
// RAM Tiny de khong mai EEPROM theo PID/relay. Status frame Tiny->ESP 8..15;
// bitmask van gom batch / 9V-low / emergency-siren.
constexpr uint8_t ATTINY_PROTOCOL_VERSION = 3U;
constexpr uint8_t ATTINY_MSG_BATCH_START = 1U;
constexpr uint8_t ATTINY_MSG_BATCH_END = 2U;
constexpr uint8_t ATTINY_MSG_SIREN_ON = 3U;
constexpr uint8_t ATTINY_MSG_SIREN_OFF = 4U;
constexpr uint8_t ATTINY_MSG_STATUS_QUERY = 5U;
constexpr uint8_t ATTINY_MSG_ACTIVITY_ON = 6U;
constexpr uint8_t ATTINY_MSG_ACTIVITY_OFF = 7U;
constexpr uint8_t ATTINY_MSG_STATUS_BASE = 8U;
constexpr uint8_t ATTINY_MSG_STATUS_MAX = 15U;
constexpr uint8_t ATTINY_MSG_MAX_COMMAND = 7U;
constexpr uint8_t ATTINY_MSG_MAX_CODE = 15U;
constexpr uint8_t ATTINY_STATUS_FLAG_BATCH = 1U;
constexpr uint8_t ATTINY_STATUS_FLAG_9V_LOW = 2U;
constexpr uint8_t ATTINY_STATUS_FLAG_SIREN = 4U;
constexpr uint32_t ATTINY_STATUS_INTERVAL_MS = 1UL * 3600UL * 1000UL;
constexpr uint32_t ATTINY_STATUS_RESPONSE_TIMEOUT_MS = 2500UL;
constexpr uint32_t ATTINY_RESYNC_RETRY_MS = 30000UL;
constexpr uint32_t ATTINY_SIREN_REASSERT_MS = 15000UL;
constexpr uint32_t ATTINY_ACTIVITY_REASSERT_MS = 5000UL;
constexpr uint32_t ATTINY_9V_CONFIRM_MS = 3000UL;
''',
    'config protocol v3')
text = replace_once(
    text,
    '  bool attinySirenBatteryLow = false;\n',
    '  bool attinySirenBatteryLow = false;\n'
    '  bool attinyCriticalActivityArmed = false;\n',
    'runtime activity diagnostic')
write(path, text)

# ---------------------------------------------------------------------------
# attiny_bus.h: make receive/tx constraints explicit.
# ---------------------------------------------------------------------------
path = 'MAYAP_INDUSTRIAL_v3_4_0/attiny_bus.h'
text = read(path)
text = replace_once(
    text,
    'constexpr uint8_t TX_QUEUE_SIZE = 8U;\n',
    'constexpr uint8_t TX_QUEUE_SIZE = 8U;\n'
    'static_assert(static_cast<uint16_t>(ATTINY_MSG_STATUS_MAX) * 2U <= EDGE_BUF_SIZE,\n'
    '              "ATtiny status frame vuot edge buffer");\n',
    'edge buffer guard')
write(path, text)

# ---------------------------------------------------------------------------
# ATtiny13A firmware: protocol v3, keep PB3 9V sensing, add RAM activity arm.
# ---------------------------------------------------------------------------
path = 'ATTINY13A_POWER_ALARM/ATTINY13A_POWER_ALARM.ino'
text = r'''// ATtiny13A backup power-alarm controller - protocol v3.
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

// FIELD CALIBRATION NOTES - CHUA DUNG DE QUYET DINH LOGIC:
// PB2/PB3 hien la DIGITAL + PCINT, nen nguong dien ap that do mach chia ap +
// VIH/VIL cua ATtiny quyet dinh, KHONG phai hai hang so ben duoi. Sau khi do
// tren ban mach that, dien gia tri mat nguon 3V3 va 9V-low vao day de luu vet;
// bao lai cac gia tri do de chuyen sang nguong ADC/chinh divider neu can.
constexpr uint16_t FIELD_MEASURED_3V3_LOSS_MV = 0U;  // TODO: nguoi dung hieu chinh
constexpr uint16_t FIELD_MEASURED_9V_LOW_MV = 0U;    // TODO: nguoi dung hieu chinh

constexpr uint16_t PULSE_MS = 30U;
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
constexpr uint8_t MSG_STATUS_MAX = 15U;
constexpr uint8_t FLAG_BATCH = 1U;
constexpr uint8_t FLAG_9V_LOW = 2U;
constexpr uint8_t FLAG_SIREN = 4U;

uint8_t EEMEM eeBatchState;
uint8_t EEMEM eeBatchStateInv;
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

static bool loadBatch() {
  const uint8_t v = eeprom_read_byte(&eeBatchState);
  const uint8_t n = eeprom_read_byte(&eeBatchStateInv);
  if ((v ^ n) == 0xFFU && v <= 1U) return v != 0U;
  return true;  // EEPROM rach/chua hop le: fail-safe coi nhu dang co me.
}

static bool saveBatch(bool on) {
  const uint8_t v = on ? 1U : 0U;
  eeprom_update_byte(&eeBatchState, v);
  eeprom_update_byte(&eeBatchStateInv, static_cast<uint8_t>(~v));
  return eeprom_read_byte(&eeBatchState) == v &&
         eeprom_read_byte(&eeBatchStateInv) == static_cast<uint8_t>(~v);
}

static void sendAck() { busLow(); delayMs(PULSE_MS); busRelease(); }

static uint8_t receiveCommand() {
  uint8_t pulses = 0U;
  for (;;) {
    if (isBusLow()) {
      uint16_t low = 0U;
      while (isBusLow() && low < PULSE_MS * 3U) { _delay_ms(1); ++low; wdt_reset(); }
      if (isBusLow()) return 0U;
      if (low >= MIN_PULSE_MS && ++pulses > MSG_ACTIVITY_OFF) return 0U;
    }
    uint16_t high = 0U;
    while (!isBusLow() && high < END_GAP_MS) { _delay_ms(1); ++high; wdt_reset(); }
    if (high >= END_GAP_MS) return pulses;
  }
}

static void sendStatus(uint8_t code) {
  delayMs(END_GAP_MS + 20U);
  for (uint8_t i = 0U; i < code; ++i) {
    busLow(); delayMs(PULSE_MS); busRelease(); delayMs(PULSE_MS);
  }
  uint16_t wait = 0U;
  while (!isBusLow() && wait < ACK_TIMEOUT_MS) { _delay_ms(1); ++wait; wdt_reset(); }
  if (isBusLow()) {
    uint16_t low = 0U;
    while (isBusLow() && low < PULSE_MS * 3U) { _delay_ms(1); ++low; wdt_reset(); }
  }
}

static inline uint8_t statusCode() {
  uint8_t flags = batchActive ? FLAG_BATCH : 0U;
  if (!nineVoltOk()) flags |= FLAG_9V_LOW;
  if (emergencySiren) flags |= FLAG_SIREN;
  return static_cast<uint8_t>(MSG_STATUS_BASE + flags);
}

static inline void updateSiren() {
  const bool armedForPowerLoss = batchActive || criticalActivity;
  setSiren(emergencySiren || (armedForPowerLoss && !espPowerOk()));
}

static inline void configureWakeMask(bool espOn) {
  if (espOn) {
    busRelease();
    PCMSK = _BV(PIN_BUS) | _BV(PIN_3V3) | _BV(PIN_9V);
  } else {
    PCMSK = _BV(PIN_3V3) | _BV(PIN_9V);
    busLow();
  }
}

ISR(PCINT0_vect) {}

int main(void) {
  MCUSR = 0U;
  wdt_disable();
  DDRB = _BV(PIN_SIREN);
  PORTB = 0U;
  batchActive = loadBatch();
  criticalActivity = false;  // RAM-only; ESP resync khi link song.
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
        ok = saveBatch(true); if (ok) batchActive = true;
      } else if (code == MSG_BATCH_END) {
        ok = saveBatch(false); if (ok) batchActive = false;
      } else if (code == MSG_SIREN_ON) {
        emergencySiren = true; ok = true;
      } else if (code == MSG_SIREN_OFF) {
        emergencySiren = false; ok = true;
      } else if (code == MSG_STATUS_QUERY) {
        ok = true; sendState = true;
      } else if (code == MSG_ACTIVITY_ON) {
        criticalActivity = true; ok = true;
      } else if (code == MSG_ACTIVITY_OFF) {
        criticalActivity = false; ok = true;
      }
      updateSiren();
      if (ok) {
        sendAck();
        if (sendState) sendStatus(statusCode());
      }
      wdt_disable();
    }

    updateSiren();
    configureWakeMask(espPowerOk());
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
    _delay_ms(50);  // debounce PB2/PB3/edge wake.
  }
}
'''
write(path, text)

# ---------------------------------------------------------------------------
# machine_control.h
# ---------------------------------------------------------------------------
path = 'MAYAP_INDUSTRIAL_v3_4_0/machine_control.h'
text = read(path)

# Read actual outputs only after they have been updated in this control cycle.
text = replace_once(
    text,
    '''    updateBatchOverdue(now);\n    updateSirenSelfTest(now);\n    updateAttinyLink(now);\n    updateAutoTune(now);\n    updateTurning(now);\n    updateHeatingAndOutputs(now);\n    processOutputEvents(now);\n    syncOutputFaults(now);\n    updateBatchTime(now);\n''',
    '''    updateBatchOverdue(now);\n    updateSirenSelfTest(now);\n    updateAutoTune(now);\n    updateTurning(now);\n    updateHeatingAndOutputs(now);\n    processOutputEvents(now);\n    syncOutputFaults(now);\n    // ATtiny activity phai doc OUTPUT THUC TE cua chinh chu ky nay.\n    updateAttinyLink(now);\n    updateBatchTime(now);\n''',
    'ATtiny update order')

# At batch stop, arm activity first if a critical output is still physically ON.
text = replace_once(
    text,
    '''    // Bao mat dien qua ATtiny13A: me da ket thuc - ATtiny se khong con tu\n    // bat coi neu sau nay mat dien (dung yeu cau "chi bao khi dang co me").\n    (void)mayapAttinyBusRequest(ATTINY_MSG_BATCH_END);\n''',
    '''    // Chuyen giao khong tao "khoang mu": neu luc dung me van con mot tai\n    // quan trong THUC TE dang ON (vd quat post-cool/dao/SSR), arm activity\n    // TRUOC roi moi xoa batch tren Tiny. Den khong tham gia dieu kien nay.\n    const OutputState &stopOut = outputs_.state();\n    const bool keepPowerLossArmed = stopOut.turnLeft || stopOut.turnRight ||\n        stopOut.circulationFan || stopOut.ventFan || stopOut.heaterSsr;\n    if (keepPowerLossArmed) (void)mayapAttinyBusRequest(ATTINY_MSG_ACTIVITY_ON);\n    (void)mayapAttinyBusRequest(ATTINY_MSG_BATCH_END);\n''',
    'batch stop ATtiny handoff')

new_function = r'''  // Bao mat dien qua ATtiny13A - protocol v3.
  // Trong me: EEPROM batch state cua Tiny arm bao mat dien nhu cu.
  // Ngoai me: activity RAM arm neu OUTPUT THUC TE cua it nhat mot tai sau ON:
  // DAO trai/phai, QUAT TUAN HOAN, QUAT HUT, SSR THANH NHIET.
  // DEN, contactor tong nhiet, coi va relay spare KHONG duoc tinh.
  void updateAttinyLink(uint32_t now) {
    mayapAttinyBusUpdate(now);
    const bool expectedBatch = batchRunning_ || resumePending_;
    const OutputState &physicalOut = outputs_.state();
    const bool expectedActivity = !expectedBatch &&
        (physicalOut.turnLeft || physicalOut.turnRight ||
         physicalOut.circulationFan || physicalOut.ventFan || physicalOut.heaterSsr);
    const bool desiredSiren = emergencyActive_ && timeReached(now, sirenMutedUntil_);

    uint8_t completedCode = 0U;
    bool completedOk = false;
    if (mayapAttinyBusTakeResult(completedCode, completedOk)) {
      attinyLinkChecked_ = true;
      if (completedOk) {
        attinyLinkHealthy_ = true;
        if (completedCode == ATTINY_MSG_BATCH_START || completedCode == ATTINY_MSG_BATCH_END) {
          attinyBatchSynced_ = (completedCode == ATTINY_MSG_BATCH_START) == expectedBatch;
          attinyLastResyncAt_ = now;
          if (mayapAttinyBusRequest(ATTINY_MSG_STATUS_QUERY)) attinyLastStatusQueryAt_ = now;
        } else if (completedCode == ATTINY_MSG_ACTIVITY_ON || completedCode == ATTINY_MSG_ACTIVITY_OFF) {
          attinyActivityMirrorOn_ = completedCode == ATTINY_MSG_ACTIVITY_ON;
          attinyLastActivityAssertAt_ = now;
        } else if (completedCode == ATTINY_MSG_SIREN_ON) {
          attinySirenMirrorOn_ = true;
          attinyLastSirenAssertAt_ = now;
        } else if (completedCode == ATTINY_MSG_SIREN_OFF) {
          attinySirenMirrorOn_ = false;
        } else if (completedCode == ATTINY_MSG_STATUS_QUERY) {
          attinyStatusAwaiting_ = true;
          attinyStatusDeadline_ = now + ATTINY_STATUS_RESPONSE_TIMEOUT_MS;
          mayapAttinyBusHoldTxUntil(attinyStatusDeadline_);
        }
      } else {
        attinyLinkHealthy_ = false;
        if (completedCode == ATTINY_MSG_BATCH_START || completedCode == ATTINY_MSG_BATCH_END) {
          attinyBatchSynced_ = false;
        }
        if (completedCode == ATTINY_MSG_ACTIVITY_ON || completedCode == ATTINY_MSG_ACTIVITY_OFF) {
          // Khong duoc tin mirror neu Tiny khong ACK lenh activity.
          attinyActivityMirrorOn_ = !expectedActivity;
        }
        if (completedCode == ATTINY_MSG_STATUS_QUERY) {
          attinyStatusAwaiting_ = false;
          mayapAttinyBusHoldTxUntil(now);
        }
      }
    }

    const uint8_t incoming = mayapAttinyBusPollIncoming();
    if (incoming >= ATTINY_MSG_STATUS_BASE && incoming <= ATTINY_MSG_STATUS_MAX) {
      const uint8_t flags = static_cast<uint8_t>(incoming - ATTINY_MSG_STATUS_BASE);
      const bool reported9vLow = (flags & ATTINY_STATUS_FLAG_9V_LOW) != 0U;
      attinyTinyBatch_ = (flags & ATTINY_STATUS_FLAG_BATCH) != 0U;
      attinyTinySirenOn_ = (flags & ATTINY_STATUS_FLAG_SIREN) != 0U;
      attinyStatusKnown_ = true;
      attinyLinkChecked_ = true;
      attinyLinkHealthy_ = true;
      attinyStatusAwaiting_ = false;
      attinyLastStatusAt_ = now;
      mayapAttinyBusHoldTxUntil(now);

      if (reported9vLow == attiny9vLow_) {
        attiny9vConfirmPending_ = false;
      } else if (!attiny9vConfirmPending_ || attiny9vCandidate_ != reported9vLow) {
        attiny9vCandidate_ = reported9vLow;
        attiny9vConfirmPending_ = true;
        attiny9vConfirmAt_ = now + ATTINY_9V_CONFIRM_MS;
      } else {
        attiny9vLow_ = reported9vLow;
        attiny9vConfirmPending_ = false;
      }

      attinyBatchSynced_ = (attinyTinyBatch_ == expectedBatch);
      mayapSerialPrintf(false,
          "[ATTINY] v=%u link=1 batchSync=%u expectedBatch=%u activity=%u mirror=%u tinyBatch=%u siren=%u tinySiren=%u 9v=%s\n",
          ATTINY_PROTOCOL_VERSION, attinyBatchSynced_ ? 1U : 0U,
          expectedBatch ? 1U : 0U, expectedActivity ? 1U : 0U,
          attinyActivityMirrorOn_ ? 1U : 0U, attinyTinyBatch_ ? 1U : 0U,
          desiredSiren ? 1U : 0U, attinyTinySirenOn_ ? 1U : 0U,
          reported9vLow ? "LOW" : "OK");
    }

    if (attinyStatusAwaiting_ && timeReached(now, attinyStatusDeadline_)) {
      attinyStatusAwaiting_ = false;
      attinyLinkChecked_ = true;
      attinyLinkHealthy_ = false;
      mayapAttinyBusHoldTxUntil(now);
    }
    if (attinyStatusKnown_) attinyBatchSynced_ = (attinyTinyBatch_ == expectedBatch);

    if (desiredSiren != attinySirenMirrorOn_ ||
        (desiredSiren && elapsedMs(now, attinyLastSirenAssertAt_) >= ATTINY_SIREN_REASSERT_MS)) {
      if (mayapAttinyBusRequest(desiredSiren ? ATTINY_MSG_SIREN_ON : ATTINY_MSG_SIREN_OFF) && desiredSiren) {
        attinyLastSirenAssertAt_ = now;
      }
    }
    if (attinyStatusKnown_ && attinyTinySirenOn_ != desiredSiren) {
      (void)mayapAttinyBusRequest(desiredSiren ? ATTINY_MSG_SIREN_ON : ATTINY_MSG_SIREN_OFF);
    }

    // Activity la RAM-only tren Tiny: ACK xac nhan moi thay doi, va khi ON
    // tai khang dinh moi 5 s de tu phuc hoi neu Tiny vua reset rieng le.
    if (expectedActivity != attinyActivityMirrorOn_ ||
        (expectedActivity && elapsedMs(now, attinyLastActivityAssertAt_) >= ATTINY_ACTIVITY_REASSERT_MS)) {
      if (mayapAttinyBusRequest(expectedActivity ? ATTINY_MSG_ACTIVITY_ON : ATTINY_MSG_ACTIVITY_OFF) && expectedActivity) {
        attinyLastActivityAssertAt_ = now;
      }
    }

    if (attinyStartupProbePending_) {
      if (mayapAttinyBusRequest(ATTINY_MSG_STATUS_QUERY)) {
        attinyStartupProbePending_ = false;
        attinyLastStatusQueryAt_ = now;
      }
    } else if (attiny9vConfirmPending_ && timeReached(now, attiny9vConfirmAt_)) {
      if (mayapAttinyBusRequest(ATTINY_MSG_STATUS_QUERY)) {
        attinyLastStatusQueryAt_ = now;
        attiny9vConfirmAt_ = now + ATTINY_9V_CONFIRM_MS;
      }
    } else if ((expectedBatch || expectedActivity) &&
               elapsedMs(now, attinyLastStatusQueryAt_) >= ATTINY_STATUS_INTERVAL_MS) {
      if (mayapAttinyBusRequest(ATTINY_MSG_STATUS_QUERY)) attinyLastStatusQueryAt_ = now;
    } else if (attinyLinkChecked_ && !attinyLinkHealthy_ &&
               elapsedMs(now, attinyLastStatusQueryAt_) >= ATTINY_RESYNC_RETRY_MS) {
      if (mayapAttinyBusRequest(ATTINY_MSG_STATUS_QUERY)) attinyLastStatusQueryAt_ = now;
    }

    if (attinyStatusKnown_ && !attinyBatchSynced_ &&
        elapsedMs(now, attinyLastResyncAt_) >= ATTINY_RESYNC_RETRY_MS) {
      if (mayapAttinyBusRequest(expectedBatch ? ATTINY_MSG_BATCH_START : ATTINY_MSG_BATCH_END)) {
        attinyLastResyncAt_ = now;
      }
    }

    faults_.set(FaultCode::AttinyBusUnresponsive,
                attinyLinkChecked_ && !attinyLinkHealthy_, now);
    faults_.set(FaultCode::SirenBatteryLow,
                attinyStatusKnown_ && attiny9vLow_, now);
    faults_.set(FaultCode::AttinyStateUnsynced,
                attinyStatusKnown_ && !attinyBatchSynced_, now);

    runtime_.attinyLinkHealthy = attinyLinkChecked_ && attinyLinkHealthy_;
    runtime_.attinyBatchSynced = attinyStatusKnown_ && attinyBatchSynced_;
    runtime_.attinyStatusKnown = attinyStatusKnown_;
    runtime_.attinySirenBatteryLow = attinyStatusKnown_ && attiny9vLow_;
    runtime_.attinyCriticalActivityArmed = expectedActivity && attinyActivityMirrorOn_;
    runtime_.attinyLastStatusAgeSec = attinyStatusKnown_
        ? elapsedMs(now, attinyLastStatusAt_) / 1000UL : UINT32_MAX;
  }

'''
text = replace_re(
    text,
    r'  // Bao mat dien qua ATtiny13A \(bus dem xung 2 chieu, xem.*?^  void updateBatchTime\(uint32_t now\) \{',
    new_function + '  void updateBatchTime(uint32_t now) {',
    'replace updateAttinyLink')

text = replace_re(
    text,
    r'  // Bao mat dien qua ATtiny13A \(bus dem xung, xem doc/attiny_power_alarm\.md\)\..*?  uint32_t attiny9vConfirmAt_ = 0U;\n',
    '''  // Bao mat dien qua ATtiny13A protocol v3. Activity ngoai me chi nam RAM\n  // cua Tiny; 9V sense/E502 van giu nguyen.\n  bool attinySirenMirrorOn_ = false;\n  bool attinyActivityMirrorOn_ = false;\n  bool attinyStartupProbePending_ = true;\n  bool attinyLinkChecked_ = false;\n  bool attinyLinkHealthy_ = false;\n  bool attinyStatusKnown_ = false;\n  bool attinyTinyBatch_ = false;\n  bool attinyTinySirenOn_ = false;\n  bool attinyBatchSynced_ = false;\n  bool attiny9vLow_ = false;\n  bool attiny9vCandidate_ = false;\n  bool attiny9vConfirmPending_ = false;\n  bool attinyStatusAwaiting_ = false;\n  uint32_t attinyLastStatusQueryAt_ = 0U;\n  uint32_t attinyLastStatusAt_ = 0U;\n  uint32_t attinyLastResyncAt_ = 0U;\n  uint32_t attinyLastSirenAssertAt_ = 0U;\n  uint32_t attinyLastActivityAssertAt_ = 0U;\n  uint32_t attinyStatusDeadline_ = 0U;\n  uint32_t attiny9vConfirmAt_ = 0U;\n''',
    'ATtiny member state')
write(path, text)

# ---------------------------------------------------------------------------
# Documentation
# ---------------------------------------------------------------------------
path = 'doc/attiny_power_alarm.md'
text = '''# ATtiny13A power alarm - protocol v3

ATtiny13A la lop bao mat dien/canh bao doc lap, khong tham gia dieu khien heater/dao.

## Chan
- PB0: BUS open-drain 1 day voi ESP32 GPIO41.
- PB1: dieu khien transistor coi.
- PB2: sense 3V3_ESP (HIGH = ESP co nguon).
- PB3: sense nguon 9V coi (HIGH = 9V OK theo nguong phan ap tren PCB).

## Khi nao Tiny arm bao mat dien
1. **Dang co me/resumePending:** batch state duoc luu EEPROM nhu v2, mat 3V3 -> coi.
2. **Ngoai me:** ESP32 gui `ACTIVITY_ON` neu output THUC TE cua it nhat mot tai sau dang ON:
   - dao trai hoac dao phai;
   - quat tuan hoan;
   - quat hut;
   - SSR thanh nhiet.
   Den, contactor tong nhiet, coi va relay spare KHONG arm bao mat dien.
3. Activity chi luu RAM Tiny, khong ghi EEPROM, de khong mai EEPROM theo PID/relay.
   ESP tai khang dinh `ACTIVITY_ON` dinh ky khi can, de tu phuc hoi neu Tiny reset rieng.

## Protocol v3
ESP32 -> Tiny:
- `1=BATCH_START`
- `2=BATCH_END`
- `3=SIREN_ON`
- `4=SIREN_OFF`
- `5=STATUS_QUERY`
- `6=ACTIVITY_ON`
- `7=ACTIVITY_OFF`

Tiny ACK moi lenh hop le. STATUS_QUERY tra frame 8..15; `status-8` la bitmask:
bit0=batch, bit1=9V low, bit2=emergency siren mirror. ESP ACK frame status.

## Chuyen trang thai khong tao khoang mu
- Bat dau me: BATCH_START duoc gui truoc khi activity ngoai me bi bo.
- Ket thuc me: neu tai quan trong van dang ON, ACTIVITY_ON duoc xep truoc BATCH_END.
- updateAttinyLink chay sau update output, nen dieu kien activity doc trang thai output vat ly moi nhat.

## Hieu chinh nguong 3.3V va 9V
PB2/PB3 hien dung DIGITAL + PCINT. Vi vay nguong dien ap thuc te do bo chia dien ap tren PCB
va nguong VIH/VIL cua ATtiny13A quyet dinh; khong co mot hang so firmware nao co the thay doi
nguong bang cach sua so don thuan. Trong `ATTINY13A_POWER_ALARM.ino` co hai placeholder:
`FIELD_MEASURED_3V3_LOSS_MV` va `FIELD_MEASURED_9V_LOW_MV`. Sau bench-test, ghi/bao lai hai
gia tri thuc te; luc do co the quyet dinh dieu chinh divider hay doi sang ADC threshold co the
hieu chinh bang code.

## Fail-safe
- EEPROM batch record hong/rach -> Tiny coi nhu dang co me.
- Tiny boot khi EEPROM bao dang co me va PB2 LOW -> coi bat ngay.
- Khi ESP mat nguon, Tiny mask PCINT PB0 va keo BUS LOW de tranh floating/wake gia.
- E501: mat giao tiep ATtiny.
- E502: nguon 9V coi low (giu nguyen protocol v3).
- E503: batch ESP/Tiny khong dong bo.

## Tieu thu dien
Tiny ngu phan lon thoi gian o `SLEEP_MODE_PWR_DOWN`; WDT chi bat trong luc xu ly bus va tat
truoc khi ngu. Tieu thu thuc te cua ca mach phu thuoc rat lon vao BOD/fuse, mach chia ap PB2/PB3,
transistor/LED va dong cua coi; can do dong tren PCB that de chot thoi luong pin.

## Build gate
GitHub Actions build rieng ATtiny13A bang avr-g++ va fail neu Flash >1024 B hoac static RAM >64 B.
CI cung so protocol version/message constants voi ESP32 truoc khi compile firmware chinh.
'''
write(path, text)

# ---------------------------------------------------------------------------
# Regression checker
# ---------------------------------------------------------------------------
path = 'tools/check_v381_reliability.py'
text = read(path)
insert_after = 'require(config, "WIFI_PORTAL_UI_IDLE_TIMEOUT_MS = 120000UL", "Wi-Fi portal 2 minute HMI timeout")\n'
addition = '''\n# ATtiny v3: keep 9V sensing and add outside-batch critical-load power-loss arm.\nattiny = read("ATTINY13A_POWER_ALARM/ATTINY13A_POWER_ALARM.ino")\nattiny_bus = read("MAYAP_INDUSTRIAL_v3_4_0/attiny_bus.h")\nattiny_doc = read("doc/attiny_power_alarm.md")\nrequire(config, "ATTINY_PROTOCOL_VERSION = 3U", "ATtiny protocol v3")\nrequire(config, "ATTINY_MSG_ACTIVITY_ON = 6U", "ATtiny activity-on command")\nrequire(config, "ATTINY_MSG_ACTIVITY_OFF = 7U", "ATtiny activity-off command")\nrequire(config, "ATTINY_STATUS_FLAG_9V_LOW = 2U", "ATtiny 9V status retained")\nrequire(machine, "physicalOut.turnLeft || physicalOut.turnRight", "outside-batch turning power-loss arm")\nrequire(machine, "physicalOut.circulationFan || physicalOut.ventFan || physicalOut.heaterSsr", "outside-batch fan/heater power-loss arm")\nrequire(machine, "const bool expectedActivity = !expectedBatch", "activity only outside batch")\nrequire(machine, "ATTINY_ACTIVITY_REASSERT_MS", "ATtiny RAM activity reassert")\nrequire(machine, "keepPowerLossArmed", "batch-stop no-gap handoff")\nrequire(attiny, "static inline bool nineVoltOk()", "ATtiny 9V sensing retained")\nrequire(attiny, "FLAG_9V_LOW", "ATtiny 9V low reporting retained")\nrequire(attiny, "batchActive || criticalActivity", "ATtiny power-loss alarm OR policy")\nrequire(attiny, "FIELD_MEASURED_3V3_LOSS_MV", "3V3 calibration note")\nrequire(attiny, "FIELD_MEASURED_9V_LOW_MV", "9V calibration note")\nrequire(attiny_doc, "Den, contactor tong nhiet", "light excluded from activity alarm documentation")\nrequire(attiny_bus, "status frame vuot edge buffer", "ATtiny status edge buffer guard")\n'''
text = replace_once(text, insert_after, insert_after + addition, 'reliability checker insertion')
write(path, text)

print('ATtiny outside-batch activity alarm patch applied')
