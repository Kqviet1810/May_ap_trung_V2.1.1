#pragma once
#include "config.h"
#include <Arduino.h>

namespace MayapAttinyBusInternal {
inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return static_cast<uint32_t>(now - then);
}
inline bool reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

// -----------------------------------------------------------------------------
// Wire format
// -----------------------------------------------------------------------------
// ESP -> Tiny commands: pulse count 1..7.
// Tiny -> ESP command ACK: one ~30 ms LOW pulse.
// Tiny -> ESP STATUS: ONE frame only.
//   physical pulse count 8..15 = batch / 9V-low / siren lower 3 bits
//   first LOW ~30 ms          = activity OFF
//   first LOW ~120 ms         = activity ON
//   remaining LOWs ~30 ms
// ESP -> Tiny STATUS ACK: one ~30 ms LOW pulse.
//
// IMPORTANT: STATUS_QUERY owns the bus from the first command pulse until the
// final STATUS ACK has finished. No other command may be queued or inserted in
// the middle. This deliberately keeps the protocol as one atomic transaction.
constexpr uint8_t STATUS_WIRE_MIN = ATTINY_MSG_STATUS_BASE;  // 8
constexpr uint8_t STATUS_WIRE_MAX = static_cast<uint8_t>(
    ATTINY_MSG_STATUS_BASE + ATTINY_STATUS_FLAG_BATCH +
    ATTINY_STATUS_FLAG_9V_LOW + ATTINY_STATUS_FLAG_SIREN);   // 15
constexpr uint32_t STATUS_SHORT_MAX_US = 45UL * 1000UL;
constexpr uint32_t STATUS_LONG_MIN_US  = 100UL * 1000UL;
constexpr uint32_t STATUS_LONG_MAX_US  = 140UL * 1000UL;
constexpr uint32_t STATUS_HARD_MAX_US  = 150UL * 1000UL;

static_assert(STATUS_WIRE_MAX == 15U,
              "ATtiny physical STATUS must stay <=15 pulses");
static_assert(ATTINY_STATUS_FLAG_ACTIVITY == 8U,
              "ATtiny activity must remain logical bit3");
static_assert(ATTINY_MSG_STATUS_MAX == 23U,
              "ATtiny logical STATUS must remain 8..23");

// -----------------------------------------------------------------------------
// GPIO capture
// -----------------------------------------------------------------------------
enum CaptureMode : uint8_t {
  CaptureIgnore = 0U,
  CaptureCommandAck = 1U,
  CaptureStatus = 2U
};
static volatile uint8_t captureMode_ = CaptureIgnore;

// Tiny -> ESP command ACK capture.
static volatile uint32_t ackFallAtUs_ = 0U;
static volatile uint32_t ackLowUs_ = 0U;
static volatile bool ackFallSeen_ = false;
static volatile bool ackPulseReady_ = false;

// Tiny -> ESP STATUS capture.
static volatile uint32_t rxFallAtUs_ = 0U;
static volatile uint32_t rxLastEdgeAtUs_ = 0U;
static volatile uint8_t rxPulseCount_ = 0U;
static volatile bool rxFrameActive_ = false;
static volatile bool rxLowActive_ = false;
static volatile bool rxInvalid_ = false;
static volatile bool rxActivityMarker_ = false;

inline void busRelease() {
  pinMode(PIN_ATTINY_BUS, INPUT);
}
inline void busDriveLow() {
  pinMode(PIN_ATTINY_BUS, OUTPUT);
  digitalWrite(PIN_ATTINY_BUS, LOW);
}

inline void resetStatusCaptureUnsafe() {
  rxFallAtUs_ = 0U;
  rxLastEdgeAtUs_ = 0U;
  rxPulseCount_ = 0U;
  rxFrameActive_ = false;
  rxLowActive_ = false;
  rxInvalid_ = false;
  rxActivityMarker_ = false;
}

inline void captureIgnore() {
  noInterrupts();
  captureMode_ = CaptureIgnore;
  ackFallAtUs_ = 0U;
  ackLowUs_ = 0U;
  ackFallSeen_ = false;
  ackPulseReady_ = false;
  resetStatusCaptureUnsafe();
  interrupts();
}

inline void armCommandAckCapture() {
  noInterrupts();
  ackFallAtUs_ = 0U;
  ackLowUs_ = 0U;
  ackFallSeen_ = false;
  ackPulseReady_ = false;
  resetStatusCaptureUnsafe();
  captureMode_ = CaptureCommandAck;
  interrupts();
}

inline void armStatusCapture() {
  noInterrupts();
  ackFallAtUs_ = 0U;
  ackLowUs_ = 0U;
  ackFallSeen_ = false;
  ackPulseReady_ = false;
  resetStatusCaptureUnsafe();
  captureMode_ = CaptureStatus;
  interrupts();
}

void IRAM_ATTR busIsr() {
  const uint8_t mode = captureMode_;
  if (mode == CaptureIgnore) return;

  const uint32_t atUs = micros();
  const bool low = digitalRead(PIN_ATTINY_BUS) == LOW;
  const uint32_t minUs = ATTINY_BUS_MIN_PULSE_MS * 1000UL;

  if (mode == CaptureCommandAck) {
    if (ackPulseReady_) return;

    if (low) {
      if (!ackFallSeen_) {
        ackFallAtUs_ = atUs;
        ackFallSeen_ = true;
      }
      return;
    }

    if (ackFallSeen_) {
      const uint32_t lowUs = static_cast<uint32_t>(atUs - ackFallAtUs_);
      ackFallSeen_ = false;
      if (lowUs < minUs) return;
      ackLowUs_ = lowUs;
      ackPulseReady_ = true;
    }
    return;
  }

  // CaptureStatus
  rxLastEdgeAtUs_ = atUs;

  if (low) {
    if (rxLowActive_) {
      rxInvalid_ = true;
      return;
    }
    rxFallAtUs_ = atUs;
    rxLowActive_ = true;
    rxFrameActive_ = true;
    return;
  }

  if (!rxLowActive_) {
    if (rxFrameActive_) rxInvalid_ = true;
    return;
  }

  const uint32_t lowUs = static_cast<uint32_t>(atUs - rxFallAtUs_);
  rxLowActive_ = false;

  if (lowUs < minUs) {
    // Ignore isolated short noise before the first real STATUS pulse.
    if (rxPulseCount_ == 0U) {
      rxFrameActive_ = false;
      rxInvalid_ = false;
    } else {
      rxInvalid_ = true;
    }
    return;
  }

  if (lowUs >= STATUS_HARD_MAX_US) {
    rxInvalid_ = true;
    return;
  }

  if (rxPulseCount_ == 0U) {
    if (lowUs < STATUS_SHORT_MAX_US) {
      rxActivityMarker_ = false;
    } else if (lowUs >= STATUS_LONG_MIN_US && lowUs < STATUS_LONG_MAX_US) {
      rxActivityMarker_ = true;
    } else {
      // Deliberate dead bands reject ambiguous first-pulse widths.
      rxInvalid_ = true;
      return;
    }
  } else if (lowUs >= STATUS_SHORT_MAX_US) {
    // Every pulse after the first must be the ordinary ~30 ms pulse.
    rxInvalid_ = true;
    return;
  }

  if (rxPulseCount_ < UINT8_MAX) ++rxPulseCount_;
  if (rxPulseCount_ > STATUS_WIRE_MAX) rxInvalid_ = true;
}

// -----------------------------------------------------------------------------
// Atomic transaction state machine
// -----------------------------------------------------------------------------
enum class TxPhase : uint8_t {
  Idle,
  IdleGap,
  PulseLow,
  PulseHigh,
  WaitCommandAck,
  WaitStatus,
  StatusAckLow,
  StatusAckHigh
};

static TxPhase txPhase_ = TxPhase::Idle;
static uint8_t txCode_ = 0U;
static uint8_t txPulsesRemaining_ = 0U;
static uint8_t txAttempt_ = 0U;
static uint32_t txDeadline_ = 0U;
static uint32_t ackWaitStartedAt_ = 0U;
static uint32_t statusWaitStartedAt_ = 0U;
static uint32_t txHoldUntil_ = 0U;

static bool resultReady_ = false;
static bool resultAcked_ = false;
static uint8_t resultCode_ = 0U;

static bool capturedStatusValid_ = false;
static uint8_t capturedStatus_ = 0U;
static bool statusReady_ = false;
static uint8_t statusValue_ = 0U;

// Keep the existing idempotent OFF protection, but it no longer participates
// in STATUS transaction sequencing.
static bool sirenOffConfirmed_ = false;

inline void finishTransaction(bool ok) {
  const uint8_t completedCode = txCode_;
  busRelease();
  captureIgnore();

  if (ok && completedCode == ATTINY_MSG_STATUS_QUERY && capturedStatusValid_) {
    statusValue_ = capturedStatus_;
    statusReady_ = true;
  }

  if (ok) {
    if (completedCode == ATTINY_MSG_SIREN_OFF) sirenOffConfirmed_ = true;
    else if (completedCode == ATTINY_MSG_SIREN_ON) sirenOffConfirmed_ = false;
  }

  resultCode_ = completedCode;
  resultAcked_ = ok;
  resultReady_ = true;

  txCode_ = 0U;
  txPulsesRemaining_ = 0U;
  txAttempt_ = 0U;
  capturedStatusValid_ = false;
  capturedStatus_ = 0U;
  txPhase_ = TxPhase::Idle;
}

inline void beginAttempt(uint32_t now) {
  ++txAttempt_;
  txPulsesRemaining_ = txCode_;
  capturedStatusValid_ = false;
  capturedStatus_ = 0U;
  captureIgnore();
  busRelease();
  txDeadline_ = now + 5UL;
  txPhase_ = TxPhase::IdleGap;
}

inline void retryOrFail(uint32_t now) {
  busRelease();
  captureIgnore();
  if (txAttempt_ < ATTINY_BUS_MAX_RETRY) beginAttempt(now);
  else finishTransaction(false);
}

// Returns: 0=pending, 1=valid complete frame, -1=invalid complete frame.
inline int8_t pollStatusFrame(uint8_t &logicalOut) {
  const uint32_t nowUs = micros();
  uint8_t pulses = 0U;
  bool invalid = false;
  bool activity = false;

  noInterrupts();
  if (!rxFrameActive_ || rxLowActive_) {
    interrupts();
    return 0;
  }

  if (static_cast<uint32_t>(nowUs - rxLastEdgeAtUs_) <
      ATTINY_BUS_END_GAP_MS * 1000UL) {
    interrupts();
    return 0;
  }

  pulses = rxPulseCount_;
  invalid = rxInvalid_;
  activity = rxActivityMarker_;
  resetStatusCaptureUnsafe();
  interrupts();

  if (invalid || pulses < STATUS_WIRE_MIN || pulses > STATUS_WIRE_MAX) {
    return -1;
  }

  const uint8_t logical = static_cast<uint8_t>(
      pulses + (activity ? ATTINY_STATUS_FLAG_ACTIVITY : 0U));
  if (logical < ATTINY_MSG_STATUS_BASE || logical > ATTINY_MSG_STATUS_MAX) {
    return -1;
  }

  logicalOut = logical;
  return 1;
}

}  // namespace MayapAttinyBusInternal

inline void mayapAttinyBusBegin() {
  using namespace MayapAttinyBusInternal;
  busRelease();
  captureIgnore();
  sirenOffConfirmed_ = false;
  attachInterrupt(digitalPinToInterrupt(PIN_ATTINY_BUS), busIsr, CHANGE);
}

inline bool mayapAttinyBusRequest(uint8_t code) {
  using namespace MayapAttinyBusInternal;
  if (code == 0U || code > ATTINY_MSG_MAX_COMMAND) return false;

  if (code == ATTINY_MSG_SIREN_OFF && sirenOffConfirmed_) return true;

  // One transaction at a time. Duplicate calls for the active code are treated
  // as accepted, but a different command is never queued behind it.
  if (txPhase_ != TxPhase::Idle) return txCode_ == code;
  if (resultReady_ || statusReady_) return false;

  const uint32_t now = millis();
  if (!reached(now, txHoldUntil_)) return false;

  txCode_ = code;
  txAttempt_ = 0U;
  beginAttempt(now);
  return true;
}

inline void mayapAttinyBusHoldTxUntil(uint32_t deadline) {
  MayapAttinyBusInternal::txHoldUntil_ = deadline;
}

inline void mayapAttinyBusUpdate(uint32_t now) {
  using namespace MayapAttinyBusInternal;

  switch (txPhase_) {
    case TxPhase::Idle:
      return;

    case TxPhase::IdleGap:
      if (!reached(now, txDeadline_)) return;
      busDriveLow();
      txDeadline_ = now + ATTINY_BUS_PULSE_MS;
      txPhase_ = TxPhase::PulseLow;
      return;

    case TxPhase::PulseLow:
      if (!reached(now, txDeadline_)) return;
      busRelease();
      txDeadline_ = now + ATTINY_BUS_PULSE_MS;
      txPhase_ = TxPhase::PulseHigh;
      return;

    case TxPhase::PulseHigh:
      if (!reached(now, txDeadline_)) return;
      if (txPulsesRemaining_ > 0U) --txPulsesRemaining_;

      if (txPulsesRemaining_ > 0U) {
        busDriveLow();
        txDeadline_ = now + ATTINY_BUS_PULSE_MS;
        txPhase_ = TxPhase::PulseLow;
        return;
      }

      // Command frame is complete. From this point STATUS_QUERY remains inside
      // the same transaction; nothing else can enter the wire state machine.
      armCommandAckCapture();
      ackWaitStartedAt_ = now;
      txPhase_ = TxPhase::WaitCommandAck;
      return;

    case TxPhase::WaitCommandAck: {
      bool ready = false;
      uint32_t lowUs = 0U;
      noInterrupts();
      ready = ackPulseReady_;
      lowUs = ackLowUs_;
      if (ready) ackPulseReady_ = false;
      interrupts();

      if (ready) {
        const uint32_t minUs = ATTINY_BUS_MIN_PULSE_MS * 1000UL;
        const uint32_t maxUs = ATTINY_BUS_PULSE_MS * 3UL * 1000UL;
        if (lowUs >= minUs && lowUs < maxUs) {
          if (txCode_ == ATTINY_MSG_STATUS_QUERY) {
            armStatusCapture();
            statusWaitStartedAt_ = now;
            txPhase_ = TxPhase::WaitStatus;
          } else {
            finishTransaction(true);
          }
        } else {
          retryOrFail(now);
        }
      } else if (elapsedMs(now, ackWaitStartedAt_) >= ATTINY_BUS_ACK_TIMEOUT_MS) {
        retryOrFail(now);
      }
      return;
    }

    case TxPhase::WaitStatus: {
      uint8_t logical = 0U;
      const int8_t state = pollStatusFrame(logical);
      if (state > 0) {
        capturedStatus_ = logical;
        capturedStatusValid_ = true;

        // ACK the STATUS inside this same transaction.
        captureIgnore();
        busDriveLow();
        txDeadline_ = now + ATTINY_BUS_PULSE_MS;
        txPhase_ = TxPhase::StatusAckLow;
      } else if (state < 0) {
        retryOrFail(now);
      } else if (elapsedMs(now, statusWaitStartedAt_) >=
                 ATTINY_STATUS_RESPONSE_TIMEOUT_MS) {
        retryOrFail(now);
      }
      return;
    }

    case TxPhase::StatusAckLow:
      if (!reached(now, txDeadline_)) return;
      busRelease();
      txDeadline_ = now + ATTINY_BUS_PULSE_MS;
      txPhase_ = TxPhase::StatusAckHigh;
      return;

    case TxPhase::StatusAckHigh:
      if (!reached(now, txDeadline_)) return;
      finishTransaction(true);
      return;
  }
}

inline bool mayapAttinyBusTakeResult(uint8_t &code, bool &acked) {
  using namespace MayapAttinyBusInternal;
  if (!resultReady_) return false;
  code = resultCode_;
  acked = resultAcked_;
  resultReady_ = false;
  return true;
}

inline uint8_t mayapAttinyBusPollIncoming() {
  using namespace MayapAttinyBusInternal;
  if (!statusReady_) return 0U;

  const uint8_t value = statusValue_;
  statusValue_ = 0U;
  statusReady_ = false;

  // A fresh authoritative STATUS may reveal that the Tiny really changed
  // state, so allow one new SIREN_OFF correction if needed.
  sirenOffConfirmed_ = false;
  return value;
}
