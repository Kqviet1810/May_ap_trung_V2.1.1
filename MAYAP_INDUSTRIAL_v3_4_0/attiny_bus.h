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
static volatile uint32_t edgeAtUs_[EDGE_BUF_SIZE];
static volatile uint8_t edgeCount_ = 0U;
static volatile bool edgeOverflow_ = false;
static volatile bool busBusy_ = false;
static uint8_t txQueue_[TX_QUEUE_SIZE]{};
static uint8_t txHead_ = 0U, txTail_ = 0U, txCount_ = 0U;
enum class TxPhase : uint8_t { Idle, IdleGap, PulseLow, PulseHigh, WaitAck, AckLow };
static TxPhase txPhase_ = TxPhase::Idle;
static uint8_t txCode_ = 0U, txPulsesRemaining_ = 0U, txAttempt_ = 0U;
static uint32_t txDeadline_ = 0U, ackWaitStartedAt_ = 0U, ackLowStartedAt_ = 0U;
static uint32_t txHoldUntil_ = 0U;
static bool resultReady_ = false, resultAcked_ = false, ackRequested_ = false, txIsAck_ = false;
static uint8_t resultCode_ = 0U;

void IRAM_ATTR busIsr() {
  if (busBusy_) return;
  if (edgeCount_ >= EDGE_BUF_SIZE) { edgeOverflow_ = true; return; }
  edgeAtUs_[edgeCount_++] = micros();
}
inline void busRelease() { pinMode(PIN_ATTINY_BUS, INPUT); }
inline void busDriveLow() { pinMode(PIN_ATTINY_BUS, OUTPUT); digitalWrite(PIN_ATTINY_BUS, LOW); }
inline bool busIsLow() { return digitalRead(PIN_ATTINY_BUS) == LOW; }
inline void resetEdges() { noInterrupts(); edgeCount_ = 0U; edgeOverflow_ = false; interrupts(); }
inline bool queuedOrActive(uint8_t code) {
  if (txPhase_ != TxPhase::Idle && txCode_ == code) return true;
  for (uint8_t i = 0U, p = txHead_; i < txCount_; ++i) {
    if (txQueue_[p] == code) return true;
    p = static_cast<uint8_t>((p + 1U) % TX_QUEUE_SIZE);
  }
  return false;
}
inline void publishResult(bool acked) {
  busRelease(); resetEdges(); busBusy_ = false;
  resultCode_ = txCode_; resultAcked_ = acked; resultReady_ = true;
  txCode_ = 0U; txPhase_ = TxPhase::Idle;
}
inline void beginAttempt(uint32_t now) {
  ++txAttempt_; txPulsesRemaining_ = txCode_; busRelease();
  txDeadline_ = now + 5UL; txPhase_ = TxPhase::IdleGap;
}
inline void retryOrFinish(uint32_t now) {
  busRelease();
  if (txAttempt_ < ATTINY_BUS_MAX_RETRY) beginAttempt(now); else publishResult(false);
}
inline void startNext(uint32_t now) {
  if (edgeCount_ != 0U || edgeOverflow_) return;
  if (ackRequested_) {
    ackRequested_ = false; txCode_ = 1U; txIsAck_ = true;
  } else {
    if (!reached(now, txHoldUntil_) || txCount_ == 0U || resultReady_) return;
    txCode_ = txQueue_[txHead_]; txHead_ = static_cast<uint8_t>((txHead_ + 1U) % TX_QUEUE_SIZE);
    --txCount_; txIsAck_ = false;
  }
  txAttempt_ = 0U; busBusy_ = true; resetEdges(); beginAttempt(now);
}
}  // namespace MayapAttinyBusInternal

inline void mayapAttinyBusBegin() {
  using namespace MayapAttinyBusInternal;
  busRelease(); resetEdges();
  attachInterrupt(digitalPinToInterrupt(PIN_ATTINY_BUS), busIsr, CHANGE);
}
inline bool mayapAttinyBusRequest(uint8_t code) {
  using namespace MayapAttinyBusInternal;
  if (code == 0U || code > ATTINY_MSG_MAX_COMMAND) return false;
  if (queuedOrActive(code)) return true;
  if (txCount_ >= TX_QUEUE_SIZE) return false;
  txQueue_[txTail_] = code; txTail_ = static_cast<uint8_t>((txTail_ + 1U) % TX_QUEUE_SIZE); ++txCount_;
  return true;
}
inline void mayapAttinyBusHoldTxUntil(uint32_t deadline) {
  MayapAttinyBusInternal::txHoldUntil_ = deadline;
}
inline void mayapAttinyBusUpdate(uint32_t now) {
  using namespace MayapAttinyBusInternal;
  switch (txPhase_) {
    case TxPhase::Idle: startNext(now); return;
    case TxPhase::IdleGap:
      if (!reached(now, txDeadline_)) return;
      busDriveLow(); txDeadline_ = now + ATTINY_BUS_PULSE_MS; txPhase_ = TxPhase::PulseLow; return;
    case TxPhase::PulseLow:
      if (!reached(now, txDeadline_)) return;
      busRelease(); txDeadline_ = now + ATTINY_BUS_PULSE_MS; txPhase_ = TxPhase::PulseHigh; return;
    case TxPhase::PulseHigh:
      if (!reached(now, txDeadline_)) return;
      if (txPulsesRemaining_ > 0U) --txPulsesRemaining_;
      if (txPulsesRemaining_ > 0U) {
        busDriveLow(); txDeadline_ = now + ATTINY_BUS_PULSE_MS; txPhase_ = TxPhase::PulseLow;
      } else if (txIsAck_) {
        busRelease(); resetEdges(); busBusy_ = false; txCode_ = 0U; txIsAck_ = false; txPhase_ = TxPhase::Idle;
      } else {
        ackWaitStartedAt_ = now; txPhase_ = TxPhase::WaitAck;
      }
      return;
    case TxPhase::WaitAck:
      if (busIsLow()) { ackLowStartedAt_ = now; txPhase_ = TxPhase::AckLow; }
      else if (elapsedMs(now, ackWaitStartedAt_) >= ATTINY_BUS_ACK_TIMEOUT_MS) retryOrFinish(now);
      return;
    case TxPhase::AckLow:
      if (!busIsLow()) {
        const uint32_t lowMs = elapsedMs(now, ackLowStartedAt_);
        if (lowMs >= ATTINY_BUS_MIN_PULSE_MS) publishResult(true); else retryOrFinish(now);
      } else if (elapsedMs(now, ackLowStartedAt_) >= ATTINY_BUS_PULSE_MS * 3UL) retryOrFinish(now);
      return;
  }
}
inline bool mayapAttinyBusTakeResult(uint8_t &code, bool &acked) {
  using namespace MayapAttinyBusInternal;
  if (!resultReady_) return false;
  code = resultCode_; acked = resultAcked_; resultReady_ = false; return true;
}
inline uint8_t mayapAttinyBusPollIncoming() {
  using namespace MayapAttinyBusInternal;
  if (busBusy_) return 0U;
  noInterrupts(); const uint8_t n = edgeCount_; const bool overflow = edgeOverflow_; interrupts();
  if (overflow) { resetEdges(); return 0U; }
  if (n == 0U) return 0U;
  uint32_t lastEdgeAt; noInterrupts(); lastEdgeAt = edgeAtUs_[n - 1U]; interrupts();
  if ((micros() - lastEdgeAt) < (ATTINY_BUS_END_GAP_MS * 1000UL)) return 0U;
  uint8_t pulses = 0U;
  for (uint8_t i = 0U; static_cast<uint8_t>(i + 1U) < n; i = static_cast<uint8_t>(i + 2U)) {
    uint32_t fallAt, riseAt; noInterrupts(); fallAt = edgeAtUs_[i]; riseAt = edgeAtUs_[i + 1U]; interrupts();
    if ((riseAt - fallAt) >= (ATTINY_BUS_MIN_PULSE_MS * 1000UL)) ++pulses;
  }
  resetEdges();
  if (pulses < ATTINY_MSG_STATUS_BASE || pulses > ATTINY_MSG_STATUS_MAX) return 0U;
  ackRequested_ = true;
  return pulses;
}
