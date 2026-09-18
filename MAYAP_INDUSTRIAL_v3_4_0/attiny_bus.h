#pragma once

#include "config.h"
#include <Arduino.h>

// Bus 1 day open-drain voi ATtiny13A. Phien ban v3.8.0 khong con cho ACK
// bang delay/while trong controlTask. Moi buoc chi doi muc chan roi tra ve;
// mayapAttinyBusUpdate() duoc goi moi chu ky 5 ms.
namespace MayapAttinyBusInternal {

inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return static_cast<uint32_t>(now - then);
}
inline bool attinyTimeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

constexpr uint8_t EDGE_BUF_SIZE = 20U;
constexpr uint8_t TX_QUEUE_SIZE = 8U;
static volatile uint32_t edgeAtUs_[EDGE_BUF_SIZE];
static volatile uint8_t edgeCount_ = 0U;
static volatile bool busBusy_ = false;

static uint8_t txQueue_[TX_QUEUE_SIZE]{};
static uint8_t txHead_ = 0U;
static uint8_t txTail_ = 0U;
static uint8_t txCount_ = 0U;

enum class TxPhase : uint8_t {
  Idle,
  IdleGap,
  PulseLow,
  PulseHigh,
  WaitAck,
  AckLow
};
static TxPhase txPhase_ = TxPhase::Idle;
static uint8_t txCode_ = 0U;
static uint8_t txPulsesRemaining_ = 0U;
static uint8_t txAttempt_ = 0U;
static uint32_t txDeadline_ = 0U;
static uint32_t ackWaitStartedAt_ = 0U;
static uint32_t ackLowStartedAt_ = 0U;
static bool resultReady_ = false;
static uint8_t resultCode_ = 0U;
static bool resultAcked_ = false;
static bool ackRequested_ = false;
static bool txIsAck_ = false;

void IRAM_ATTR busIsr() {
  if (busBusy_) return;
  if (edgeCount_ >= EDGE_BUF_SIZE) return;
  edgeAtUs_[edgeCount_] = micros();
  ++edgeCount_;
}

inline void busRelease() {
  pinMode(PIN_ATTINY_BUS, INPUT);
}
inline void busDriveLow() {
  pinMode(PIN_ATTINY_BUS, OUTPUT);
  digitalWrite(PIN_ATTINY_BUS, LOW);
}
inline bool busIsLow() {
  return digitalRead(PIN_ATTINY_BUS) == LOW;
}
inline void resetEdgeBuffer() {
  noInterrupts();
  edgeCount_ = 0U;
  interrupts();
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
  resetEdgeBuffer();
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
  busRelease();
  txDeadline_ = now + 5UL;
  txPhase_ = TxPhase::IdleGap;
}
inline void retryOrFinish(uint32_t now) {
  busRelease();
  if (txAttempt_ < ATTINY_BUS_MAX_RETRY) beginAttempt(now);
  else publishResult(false);
}
inline void startNext(uint32_t now) {
  if (edgeCount_ != 0U) return;
  if (ackRequested_) {
    ackRequested_ = false;
    txCode_ = 1U;
    txIsAck_ = true;
  } else {
    if (txCount_ == 0U || resultReady_) return;
    txCode_ = txQueue_[txHead_];
    txHead_ = static_cast<uint8_t>((txHead_ + 1U) % TX_QUEUE_SIZE);
    --txCount_;
    txIsAck_ = false;
  }
  txAttempt_ = 0U;
  busBusy_ = true;
  resetEdgeBuffer();
  beginAttempt(now);
}

}  // namespace MayapAttinyBusInternal

inline void mayapAttinyBusBegin() {
  using namespace MayapAttinyBusInternal;
  busRelease();
  resetEdgeBuffer();
  attachInterrupt(digitalPinToInterrupt(PIN_ATTINY_BUS), busIsr, CHANGE);
}

// Xep yeu cau gui. Trung ma dang gui/da nam trong hang se duoc gop lai.
// Ham luon tra ve ngay, khong delay va khong cho ACK.
inline bool mayapAttinyBusRequest(uint8_t code) {
  using namespace MayapAttinyBusInternal;
  if (code == 0U || code > ATTINY_MSG_MAX_CODE) return false;
  if (queuedOrActive(code)) return true;
  if (txCount_ >= TX_QUEUE_SIZE) return false;
  txQueue_[txTail_] = code;
  txTail_ = static_cast<uint8_t>((txTail_ + 1U) % TX_QUEUE_SIZE);
  ++txCount_;
  return true;
}

// Tien state machine them mot buoc. Goi moi chu ky controlTask.
inline void mayapAttinyBusUpdate(uint32_t now) {
  using namespace MayapAttinyBusInternal;
  switch (txPhase_) {
    case TxPhase::Idle:
      startNext(now);
      return;
    case TxPhase::IdleGap:
      if (!attinyTimeReached(now, txDeadline_)) return;
      busDriveLow();
      txDeadline_ = now + ATTINY_BUS_PULSE_MS;
      txPhase_ = TxPhase::PulseLow;
      return;
    case TxPhase::PulseLow:
      if (!attinyTimeReached(now, txDeadline_)) return;
      busRelease();
      txDeadline_ = now + ATTINY_BUS_PULSE_MS;
      txPhase_ = TxPhase::PulseHigh;
      return;
    case TxPhase::PulseHigh:
      if (!attinyTimeReached(now, txDeadline_)) return;
      if (txPulsesRemaining_ > 0U) --txPulsesRemaining_;
      if (txPulsesRemaining_ > 0U) {
        busDriveLow();
        txDeadline_ = now + ATTINY_BUS_PULSE_MS;
        txPhase_ = TxPhase::PulseLow;
      } else if (txIsAck_) {
        busRelease();
        resetEdgeBuffer();
        busBusy_ = false;
        txCode_ = 0U;
        txIsAck_ = false;
        txPhase_ = TxPhase::Idle;
      } else {
        ackWaitStartedAt_ = now;
        txPhase_ = TxPhase::WaitAck;
      }
      return;
    case TxPhase::WaitAck:
      if (busIsLow()) {
        ackLowStartedAt_ = now;
        txPhase_ = TxPhase::AckLow;
      } else if (elapsedMs(now, ackWaitStartedAt_) >= ATTINY_BUS_ACK_TIMEOUT_MS) {
        retryOrFinish(now);
      }
      return;
    case TxPhase::AckLow:
      if (!busIsLow()) {
        const uint32_t lowMs = elapsedMs(now, ackLowStartedAt_);
        if (lowMs >= ATTINY_BUS_MIN_PULSE_MS) publishResult(true);
        else retryOrFinish(now);
      } else if (elapsedMs(now, ackLowStartedAt_) >= ATTINY_BUS_PULSE_MS * 3UL) {
        retryOrFinish(now);
      }
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

// Nhan ban tin Tiny->ESP32 bang bo dem canh ISR. Khong block.
inline uint8_t mayapAttinyBusPollIncoming() {
  using namespace MayapAttinyBusInternal;
  if (busBusy_) return 0U;
  noInterrupts();
  const uint8_t n = edgeCount_;
  interrupts();
  if (n == 0U) return 0U;

  uint32_t lastEdgeAt;
  noInterrupts();
  lastEdgeAt = edgeAtUs_[n - 1U];
  interrupts();
  if ((micros() - lastEdgeAt) < (ATTINY_BUS_END_GAP_MS * 1000UL)) return 0U;

  uint8_t pulses = 0U;
  for (uint8_t i = 0U; static_cast<uint8_t>(i + 1U) < n;
       i = static_cast<uint8_t>(i + 2U)) {
    uint32_t tFall, tRise;
    noInterrupts();
    tFall = edgeAtUs_[i];
    tRise = edgeAtUs_[i + 1U];
    interrupts();
    if ((tRise - tFall) >= (ATTINY_BUS_MIN_PULSE_MS * 1000UL)) ++pulses;
  }
  resetEdgeBuffer();
  if (pulses == 0U || pulses > ATTINY_MSG_MAX_CODE) return 0U;

  // ACK uu tien bang 1 xung rieng trong state machine. Khong dua vao hang
  // ban tin (ma 1 cung la BATCH_START), tranh ACK tre bi hieu sai thanh lenh.
  ackRequested_ = true;
  return pulses;
}
