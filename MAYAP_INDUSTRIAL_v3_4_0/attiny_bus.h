#pragma once
#include "config.h"
#include <Arduino.h>

namespace MayapAttinyBusInternal {
inline uint32_t elapsedMs(uint32_t now, uint32_t then) { return static_cast<uint32_t>(now - then); }
inline bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

constexpr uint8_t EDGE_BUF_SIZE = 48U;
constexpr uint8_t TX_QUEUE_SIZE = 8U;
static_assert(static_cast<uint16_t>(ATTINY_MSG_STATUS_MAX) * 2U <= EDGE_BUF_SIZE,
              "ATtiny status frame vuot edge buffer");

// ISR capture mode. Khi ESP dang tu phat xung thi bo qua canh cua chinh no;
// khi cho ACK thi ISR do truc tiep do rong xung ACK; con lai ghi canh STATUS.
enum CaptureMode : uint8_t { CaptureIgnore = 0U, CaptureAck = 1U, CaptureIncoming = 2U };
static volatile uint8_t captureMode_ = CaptureIncoming;

static volatile uint32_t edgeAtUs_[EDGE_BUF_SIZE];
static volatile uint8_t edgeLow_[EDGE_BUF_SIZE];
static volatile uint8_t edgeCount_ = 0U;
static volatile bool edgeOverflow_ = false;

static volatile uint32_t ackFallAtUs_ = 0U;
static volatile uint32_t ackLowUs_ = 0U;
static volatile bool ackFallSeen_ = false;
static volatile bool ackPulseReady_ = false;

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
  edgeCount_ = 0U;
  edgeOverflow_ = false;
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

  if (mode == CaptureAck) {
    if (ackPulseReady_) return;
    if (low) {
      ackFallAtUs_ = atUs;
      ackFallSeen_ = true;
    } else if (ackFallSeen_) {
      ackLowUs_ = static_cast<uint32_t>(atUs - ackFallAtUs_);
      ackFallSeen_ = false;
      ackPulseReady_ = true;
    }
    return;
  }

  if (edgeCount_ >= EDGE_BUF_SIZE) {
    edgeOverflow_ = true;
    return;
  }
  const uint8_t index = edgeCount_;
  edgeAtUs_[index] = atUs;
  edgeLow_[index] = low ? 1U : 0U;
  edgeCount_ = static_cast<uint8_t>(index + 1U);
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
  // Mo thu STATUS truoc khi cong bo transaction da xong. Tiny STATUS_QUERY
  // doi 170 ms sau ACK, nhung thu tu nay van loai bo cua so race cu.
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
  // Bo qua canh do chinh ESP tao ra trong suot pha TX.
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
  const bool incomingPending = edgeCount_ != 0U || edgeOverflow_;
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
        // Tiny chi ACK sau END_GAP=150 ms; ISR da duoc mo ngay tu day nen
        // khong con phu thuoc controlTask co kip polling trong 30 ms hay khong.
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
          // Xung ngan la noise: tiep tuc cho ACK hop le trong timeout goc.
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

  // Copy frame atomically, roi clear buffer truoc khi parse. Neu frame moi bat
  // dau sau do thi ISR ghi vao buffer moi, khong bi reset mat nhu code cu.
  uint32_t localAtUs[EDGE_BUF_SIZE];
  uint8_t localLow[EDGE_BUF_SIZE];
  uint8_t n = 0U;
  bool overflow = false;

  noInterrupts();
  n = edgeCount_;
  overflow = edgeOverflow_;
  if (overflow) {
    resetIncomingUnsafe();
    interrupts();
    return 0U;
  }
  if (n == 0U) {
    interrupts();
    return 0U;
  }

  const uint32_t nowUs = micros();
  const uint32_t lastEdgeAt = edgeAtUs_[n - 1U];
  if (static_cast<uint32_t>(nowUs - lastEdgeAt) < (ATTINY_BUS_END_GAP_MS * 1000UL)) {
    interrupts();
    return 0U;
  }

  for (uint8_t i = 0U; i < n; ++i) {
    localAtUs[i] = edgeAtUs_[i];
    localLow[i] = edgeLow_[i];
  }
  resetIncomingUnsafe();
  interrupts();

  uint8_t pulses = 0U;
  bool waitingRise = false;
  uint32_t fallAtUs = 0U;
  const uint32_t minUs = ATTINY_BUS_MIN_PULSE_MS * 1000UL;
  const uint32_t maxUs = ATTINY_BUS_PULSE_MS * 3UL * 1000UL;

  for (uint8_t i = 0U; i < n; ++i) {
    if (localLow[i] != 0U) {
      // FALL khi van dang cho RISE => mat canh/noise, bo ca frame de khong
      // bien status N thanh N-1/N+1 mot cach im lang.
      if (waitingRise) return 0U;
      fallAtUs = localAtUs[i];
      waitingRise = true;
    } else {
      // Frame hop le phai bat dau bang FALL. RISE don le bi coi la race/noise.
      if (!waitingRise) return 0U;
      const uint32_t lowUs = static_cast<uint32_t>(localAtUs[i] - fallAtUs);
      waitingRise = false;
      if (lowUs >= maxUs) return 0U;
      if (lowUs >= minUs) ++pulses;  // xung ngan hon MIN la noise, bo qua.
    }
  }

  if (waitingRise) return 0U;
  if (pulses < ATTINY_MSG_STATUS_BASE || pulses > ATTINY_MSG_STATUS_MAX) return 0U;

  ackRequested_ = true;
  return pulses;
}
