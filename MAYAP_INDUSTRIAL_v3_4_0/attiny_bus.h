#pragma once
#include "config.h"
#include <Arduino.h>

namespace MayapAttinyBusInternal {
inline uint32_t elapsedMs(uint32_t now, uint32_t then) { return static_cast<uint32_t>(now - then); }
inline bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

constexpr uint8_t TX_QUEUE_SIZE = 8U;

// BUS RX/TX capture modes. The ESP never drives HIGH: OUTPUT LOW or INPUT/Hi-Z only.
enum CaptureMode : uint8_t { CaptureIgnore = 0U, CaptureAck = 1U, CaptureIncoming = 2U };
static volatile uint8_t captureMode_ = CaptureIncoming;

// ACK capture (Tiny -> ESP). Width is measured fully inside the GPIO ISR so a
// 30 ms ACK cannot be missed even if the 5 ms control task is briefly delayed.
static volatile uint32_t ackFallAtUs_ = 0U;
static volatile uint32_t ackLowUs_ = 0U;
static volatile bool ackFallSeen_ = false;
static volatile bool ackPulseReady_ = false;

// Incoming STATUS capture (Tiny -> ESP). Protocol encodes only the number of
// valid LOW pulses, so count pulses directly in the ISR instead of buffering
// and later pairing every edge. This removes frame-length sensitivity and a
// lost-edge pairing failure mode for long status values (16..23).
static volatile uint32_t rxFallAtUs_ = 0U;
static volatile uint32_t rxLastEdgeAtUs_ = 0U;
static volatile uint8_t rxPulseCount_ = 0U;
static volatile bool rxFrameActive_ = false;
static volatile bool rxLowActive_ = false;
static volatile bool rxInvalid_ = false;

static volatile bool busBusy_ = false;
static uint8_t txQueue_[TX_QUEUE_SIZE]{};
static uint8_t txHead_ = 0U, txTail_ = 0U, txCount_ = 0U;

enum class TxPhase : uint8_t { Idle, IdleGap, PulseLow, PulseHigh, WaitAck };
static TxPhase txPhase_ = TxPhase::Idle;
static uint8_t txCode_ = 0U, txPulsesRemaining_ = 0U, txAttempt_ = 0U;
static uint32_t txDeadline_ = 0U, ackWaitStartedAt_ = 0U;
static uint32_t txHoldUntil_ = 0U;
static bool resultReady_ = false, resultAcked_ = false, ackRequested_ = false, txIsAck_ = false;
static uint8_t resultCode_ = 0U;

inline void busRelease() { pinMode(PIN_ATTINY_BUS, INPUT); }
inline void busDriveLow() { pinMode(PIN_ATTINY_BUS, OUTPUT); digitalWrite(PIN_ATTINY_BUS, LOW); }
inline bool busIsLow() { return digitalRead(PIN_ATTINY_BUS) == LOW; }

inline void resetIncomingUnsafe() {
  rxFallAtUs_ = 0U;
  rxLastEdgeAtUs_ = 0U;
  rxPulseCount_ = 0U;
  rxFrameActive_ = false;
  rxLowActive_ = false;
  rxInvalid_ = false;
}

inline void setCaptureIgnore() {
  noInterrupts();
  captureMode_ = CaptureIgnore;
  ackFallSeen_ = false;
  ackPulseReady_ = false;
  resetIncomingUnsafe();
  interrupts();
}

inline void armAckCapture() {
  noInterrupts();
  ackFallAtUs_ = 0U;
  ackLowUs_ = 0U;
  ackFallSeen_ = false;
  ackPulseReady_ = false;
  resetIncomingUnsafe();
  captureMode_ = CaptureAck;
  interrupts();
}

inline void armIncomingCapture() {
  noInterrupts();
  resetIncomingUnsafe();
  ackFallSeen_ = false;
  ackPulseReady_ = false;
  captureMode_ = CaptureIncoming;
  interrupts();
}

void IRAM_ATTR busIsr() {
  const uint8_t mode = captureMode_;
  if (mode == CaptureIgnore) return;

  const uint32_t atUs = micros();
  const bool low = digitalRead(PIN_ATTINY_BUS) == LOW;
  const uint32_t minUs = ATTINY_BUS_MIN_PULSE_MS * 1000UL;
  const uint32_t maxUs = ATTINY_BUS_PULSE_MS * 3UL * 1000UL;

  if (mode == CaptureAck) {
    if (ackPulseReady_) return;

    if (low) {
      if (!ackFallSeen_) {
        ackFallAtUs_ = atUs;
        ackFallSeen_ = true;
      }
    } else if (ackFallSeen_) {
      const uint32_t lowUs = static_cast<uint32_t>(atUs - ackFallAtUs_);
      ackFallSeen_ = false;

      if (lowUs < minUs) {
        return;
      }

      ackLowUs_ = lowUs;
      ackPulseReady_ = true;
    }
    return;
  }

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

  if (lowUs >= maxUs) {
    rxInvalid_ = true;
    return;
  }

  if (lowUs >= minUs) {
    if (rxPulseCount_ < UINT8_MAX) ++rxPulseCount_;
    if (rxPulseCount_ > ATTINY_MSG_STATUS_MAX) rxInvalid_ = true;
  }
}

inline bool queuedOrActive(uint8_t code) {
  if (txPhase_ != TxPhase::Idle && txCode_ == code) return true;
  for (uint8_t i = 0U, p = txHead_; i < txCount_; ++i) {
    if (txQueue_[p] == code) return true;
    p = static_cast<uint8_t>((p + 1U) % TX_QUEUE_SIZE);
  }
  return false;
}

inline void publishResult(bool acked) {
  busRelease();
  armIncomingCapture();
  busBusy_ = false;
  resultCode_ = txCode_;
  resultAcked_ = acked;
  resultReady_ = true;
  txCode_ = 0U;
  txPhase_ = TxPhase::Idle;
}

inline void beginAttempt(uint32_t now) {
  ++txAttempt_;
  txPulsesRemaining_ = txCode_;
  setCaptureIgnore();
  busRelease();
  txDeadline_ = now + 5UL;
  txPhase_ = TxPhase::IdleGap;
}

inline void retryOrFinish(uint32_t now) {
  setCaptureIgnore();
  busRelease();
  if (txAttempt_ < ATTINY_BUS_MAX_RETRY) beginAttempt(now);
  else publishResult(false);
}

inline void finishAckTransmit() {
  busRelease();
  busBusy_ = false;
  txCode_ = 0U;
  txIsAck_ = false;
  txPhase_ = TxPhase::Idle;
  armIncomingCapture();
}

inline void startNext(uint32_t now) {
  noInterrupts();
  const bool incomingPending = rxFrameActive_ || rxLowActive_;
  interrupts();
  if (incomingPending) return;

  if (ackRequested_) {
    ackRequested_ = false;
    txCode_ = 1U;
    txIsAck_ = true;
  } else {
    if (!reached(now, txHoldUntil_) || txCount_ == 0U || resultReady_) return;
    txCode_ = txQueue_[txHead_];
    txHead_ = static_cast<uint8_t>((txHead_ + 1U) % TX_QUEUE_SIZE);
    --txCount_;
    txIsAck_ = false;
  }

  txAttempt_ = 0U;
  busBusy_ = true;
  beginAttempt(now);
}
}  // namespace MayapAttinyBusInternal

inline void mayapAttinyBusBegin() {
  using namespace MayapAttinyBusInternal;
  busRelease();
  armIncomingCapture();
  attachInterrupt(digitalPinToInterrupt(PIN_ATTINY_BUS), busIsr, CHANGE);
}

inline bool mayapAttinyBusRequest(uint8_t code) {
  using namespace MayapAttinyBusInternal;
  if (code == 0U || code > ATTINY_MSG_MAX_COMMAND) return false;
  if (queuedOrActive(code)) return true;
  if (txCount_ >= TX_QUEUE_SIZE) return false;
  txQueue_[txTail_] = code;
  txTail_ = static_cast<uint8_t>((txTail_ + 1U) % TX_QUEUE_SIZE);
  ++txCount_;
  return true;
}

inline void mayapAttinyBusHoldTxUntil(uint32_t deadline) {
  MayapAttinyBusInternal::txHoldUntil_ = deadline;
}

inline void mayapAttinyBusUpdate(uint32_t now) {
  using namespace MayapAttinyBusInternal;

  switch (txPhase_) {
    case TxPhase::Idle:
      startNext(now);
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
      } else if (txIsAck_) {
        finishAckTransmit();
      } else {
        armAckCapture();
        ackWaitStartedAt_ = now;
        txPhase_ = TxPhase::WaitAck;
      }
      return;

    case TxPhase::WaitAck: {
      bool ready;
      uint32_t lowUs;
      noInterrupts();
      ready = ackPulseReady_;
      lowUs = ackLowUs_;
      if (ready) ackPulseReady_ = false;
      interrupts();

      if (ready) {
        const uint32_t minUs = ATTINY_BUS_MIN_PULSE_MS * 1000UL;
        const uint32_t maxUs = ATTINY_BUS_PULSE_MS * 3UL * 1000UL;
        if (lowUs >= minUs && lowUs < maxUs) {
          publishResult(true);
        } else if (lowUs < minUs) {
          armAckCapture();
        } else {
          retryOrFinish(now);
        }
      } else if (elapsedMs(now, ackWaitStartedAt_) >= ATTINY_BUS_ACK_TIMEOUT_MS) {
        retryOrFinish(now);
      }
      return;
    }
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
  if (busBusy_ || captureMode_ != CaptureIncoming) return 0U;

  const uint32_t nowUs = micros();
  uint8_t pulses = 0U;
  bool invalid = false;

  noInterrupts();
  if (!rxFrameActive_ || rxLowActive_) {
    interrupts();
    return 0U;
  }

  if (static_cast<uint32_t>(nowUs - rxLastEdgeAtUs_) <
      (ATTINY_BUS_END_GAP_MS * 1000UL)) {
    interrupts();
    return 0U;
  }

  pulses = rxPulseCount_;
  invalid = rxInvalid_;
  resetIncomingUnsafe();
  interrupts();

  if (invalid) return 0U;
  if (pulses < ATTINY_MSG_STATUS_BASE || pulses > ATTINY_MSG_STATUS_MAX) return 0U;

  ackRequested_ = true;
  return pulses;
}
