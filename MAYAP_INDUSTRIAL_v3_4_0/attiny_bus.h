#pragma once
#include "config.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp32-hal-rmt.h>

// Protocol v4: one hardware-timed command pulse; Tiny replies with a
// six-pulse status frame (preamble, four state bits, XOR parity).
// GPIO41 and PB0 must remain open drain with an external 3.3 V pull-up.
namespace MayapAttinyBusInternal {
constexpr uint8_t TX_QUEUE_SIZE = 8U;
constexpr uint8_t RESPONSE_EDGES = 12U;
constexpr uint8_t CAPTURE_EDGES = 14U; // Optional two command edges plus reply.
constexpr uint32_t RESPONSE_TIMEOUT_MS = 500UL;
constexpr uint32_t RETRY_GAP_MS = 200UL;
constexpr uint32_t FRAME_END_GAP_US = 30000UL;
constexpr uint32_t IDLE_GAP_US = 30000UL;
// A missing pull-up or a stuck-low peer must not leave a queued command
// pending forever: the controller needs a terminal result to raise E501.
constexpr uint32_t BUS_IDLE_TIMEOUT_MS = 500UL;

static uint8_t txQueue_[TX_QUEUE_SIZE]{};
static uint8_t txHead_ = 0U, txTail_ = 0U, txCount_ = 0U;
static uint8_t txCode_ = 0U, txAttempt_ = 0U;
static uint8_t resultCode_ = 0U, incomingCode_ = 0U;
static bool resultReady_ = false, resultAcked_ = false, rmtReady_ = false;
static uint32_t txHoldUntil_ = 0U, txStartedAt_ = 0U;
static uint32_t responseStartedAt_ = 0U, retryAt_ = 0U;
static uint32_t idleBlockedAt_ = 0U;
static bool idleBlocked_ = false;
static rmt_data_t txSymbol_[8]{};
enum class Phase : uint8_t { Idle, Sending, WaitingReply, RetryGap };
static Phase phase_ = Phase::Idle;

static volatile bool rxEnabled_ = false, edgeOverflow_ = false;
static volatile uint8_t edgeCount_ = 0U;
static volatile uint32_t edgeAtUs_[CAPTURE_EDGES]{};
static volatile uint32_t lastBusEdgeUs_ = 0U;

void IRAM_ATTR busIsr() {
  const uint32_t at = micros();
  lastBusEdgeUs_ = at;
  if (!rxEnabled_) return;
  if (edgeCount_ >= CAPTURE_EDGES) { edgeOverflow_ = true; return; }
  edgeAtUs_[edgeCount_++] = at;
}
inline bool reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
inline void clearEdges() {
  noInterrupts();
  edgeCount_ = 0U;
  edgeOverflow_ = false;
  interrupts();
}
inline bool queuedOrActive(uint8_t code) {
  if (phase_ != Phase::Idle && txCode_ == code) return true;
  for (uint8_t i = 0U, p = txHead_; i < txCount_; ++i) {
    if (txQueue_[p] == code) return true;
    p = static_cast<uint8_t>((p + 1U) % TX_QUEUE_SIZE);
  }
  return false;
}
inline void finish(bool ok, uint8_t flags = 0U) {
  rxEnabled_ = false;
  clearEdges();
  resultCode_ = txCode_;
  resultAcked_ = ok;
  resultReady_ = true;
  incomingCode_ = ok ? static_cast<uint8_t>(ATTINY_MSG_STATUS_BASE + flags) : 0U;
  txCode_ = 0U;
  idleBlocked_ = false;
  phase_ = Phase::Idle;
}
#if MAYAP_DIAGNOSTIC_SERIAL
inline void logFailure(const char *reason) {
  Serial.printf("[ATTINY-BUS] FAIL reason=%s cmd=%u attempt=%u edges=%u overflow=%u\n",
                reason, static_cast<unsigned>(txCode_),
                static_cast<unsigned>(txAttempt_),
                static_cast<unsigned>(edgeCount_), edgeOverflow_ ? 1U : 0U);
}
#else
inline void logFailure(const char *) {}
#endif
inline void retryOrFinish(uint32_t now, const char *reason) {
  logFailure(reason);
  rxEnabled_ = false;
  clearEdges();
  if (txAttempt_ >= ATTINY_BUS_MAX_RETRY) finish(false);
  else { retryAt_ = now + RETRY_GAP_MS; phase_ = Phase::RetryGap; }
}
inline bool decodeFrame(uint8_t &flags) {
  uint32_t edge[CAPTURE_EDGES];
  noInterrupts();
  const uint8_t count = edgeCount_;
  const bool overflow = edgeOverflow_;
  if (count >= RESPONSE_EDGES && count <= CAPTURE_EDGES && !overflow) {
    for (uint8_t i = 0U; i < count; ++i) edge[i] = edgeAtUs_[i];
  }
  interrupts();
  if (count < RESPONSE_EDGES || count > CAPTURE_EDGES || overflow) return false;
  const uint8_t base = count - RESPONSE_EDGES;
  const uint32_t preamble = edge[base + 1U] - edge[base];
  if (preamble < 45000UL || preamble > 75000UL) return false;
  uint8_t parity = 0U;
  flags = 0U;
  for (uint8_t i = 1U; i < 6U; ++i) {
    const uint32_t gap = edge[base + i * 2U] - edge[base + i * 2U - 1U];
    if (gap < 8000UL || gap > 25000UL) return false;
    const uint32_t width = edge[base + i * 2U + 1U] - edge[base + i * 2U];
    uint8_t bit;
    if (width >= 6000UL && width <= 16000UL) bit = 0U;
    else if (width >= 22000UL && width <= 40000UL) bit = 1U;
    else return false;
    if (i < 5U) { flags |= static_cast<uint8_t>(bit << (i - 1U)); parity ^= bit; }
    else if (bit != parity) return false;
  }
  return true;
}
inline void startAttempt(uint32_t now) {
  if (!rmtReady_) { logFailure("RMT_INIT"); finish(false); return; }
  clearEdges();
  rxEnabled_ = true;
  uint32_t remainingUs = ATTINY_COMMAND_WIDTH_MS[txCode_] * 1000UL;
  uint8_t symbols = 0U;
  while (remainingUs != 0U) {
    rmt_data_t &symbol = txSymbol_[symbols++];
    const uint16_t first = remainingUs > 30000UL ? 30000U : remainingUs;
    symbol.duration0 = first;
    symbol.level0 = 0U;
    remainingUs -= first;
    if (remainingUs != 0U) {
      const uint16_t second = remainingUs > 30000UL ? 30000U : remainingUs;
      symbol.duration1 = second;
      symbol.level1 = 0U;
      remainingUs -= second;
    } else {
      symbol.duration1 = 1U;
      symbol.level1 = 1U;
    }
  }
  ++txAttempt_;
  if (!rmtWriteAsync(PIN_ATTINY_BUS, txSymbol_, symbols)) {
    retryOrFinish(now, "TX_START");
    return;
  }
  txStartedAt_ = now;
  phase_ = Phase::Sending;
}
inline void startNext(uint32_t now) {
  if (txCount_ == 0U || resultReady_ || !reached(now, txHoldUntil_)) {
    idleBlocked_ = false;
    return;
  }
  if (digitalRead(PIN_ATTINY_BUS) == LOW ||
      static_cast<uint32_t>(micros() - lastBusEdgeUs_) < IDLE_GAP_US) {
    if (!idleBlocked_) {
      idleBlocked_ = true;
      idleBlockedAt_ = now;
    } else if (static_cast<uint32_t>(now - idleBlockedAt_) >= BUS_IDLE_TIMEOUT_MS) {
      txCode_ = txQueue_[txHead_];
      txHead_ = static_cast<uint8_t>((txHead_ + 1U) % TX_QUEUE_SIZE);
      --txCount_;
      txAttempt_ = 0U;
      logFailure("BUS_NOT_IDLE");
      finish(false);
    }
    return;
  }
  idleBlocked_ = false;
  txCode_ = txQueue_[txHead_];
  txHead_ = static_cast<uint8_t>((txHead_ + 1U) % TX_QUEUE_SIZE);
  --txCount_;
  txAttempt_ = 0U;
  startAttempt(now);
}
} // namespace MayapAttinyBusInternal

inline void mayapAttinyBusBegin() {
  using namespace MayapAttinyBusInternal;
  pinMode(PIN_ATTINY_BUS, INPUT);
  lastBusEdgeUs_ = micros();
  rmtReady_ = rmtInit(PIN_ATTINY_BUS, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000U);
  if (rmtReady_) {
    rmtReady_ = rmtSetEOT(PIN_ATTINY_BUS, HIGH) &&
                gpio_set_direction(static_cast<gpio_num_t>(PIN_ATTINY_BUS),
                                   GPIO_MODE_INPUT_OUTPUT_OD) == ESP_OK;
    if (rmtReady_) {
      rmt_data_t idle{};
      idle.duration0 = 1U;
      idle.level0 = 1U;
      idle.duration1 = 1U;
      idle.level1 = 1U;
      rmtReady_ = rmtWrite(PIN_ATTINY_BUS, &idle, 1U, 50U);
    }
  }
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
  switch (phase_) {
    case Phase::Idle: startNext(now); return;
    case Phase::Sending:
      if (!rmtTransmitCompleted(PIN_ATTINY_BUS)) {
        if (static_cast<uint32_t>(now - txStartedAt_) >
            ATTINY_COMMAND_WIDTH_MS[txCode_] + 200UL) {
          logFailure("TX_TIMEOUT");
          finish(false);
        }
        return;
      }
      responseStartedAt_ = now;
      phase_ = Phase::WaitingReply;
      return;
    case Phase::WaitingReply: {
      uint8_t count;
      bool overflow;
      uint32_t lastEdge = 0U;
      noInterrupts();
      count = edgeCount_;
      overflow = edgeOverflow_;
      if (count != 0U) lastEdge = edgeAtUs_[count - 1U];
      interrupts();
      if (overflow) { retryOrFinish(now, "RX_OVERFLOW"); return; }
      if (count >= RESPONSE_EDGES && count <= CAPTURE_EDGES &&
          static_cast<uint32_t>(micros() - lastEdge) >= FRAME_END_GAP_US) {
        uint8_t flags = 0U;
        if (decodeFrame(flags)) finish(true, flags);
        else retryOrFinish(now, "FRAME_INVALID");
        return;
      }
      if (static_cast<uint32_t>(now - responseStartedAt_) >= RESPONSE_TIMEOUT_MS)
        retryOrFinish(now, "RESPONSE_TIMEOUT");
      return;
    }
    case Phase::RetryGap:
      if (!reached(now, retryAt_)) return;
      if (digitalRead(PIN_ATTINY_BUS) == HIGH &&
          static_cast<uint32_t>(micros() - lastBusEdgeUs_) >= IDLE_GAP_US) {
        startAttempt(now);
      } else if (static_cast<uint32_t>(now - retryAt_) >= BUS_IDLE_TIMEOUT_MS) {
        logFailure("RETRY_BUS_NOT_IDLE");
        finish(false);
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
inline uint8_t mayapAttinyBusPollIncoming() {
  using namespace MayapAttinyBusInternal;
  const uint8_t code = incomingCode_;
  incomingCode_ = 0U;
  return code;
}
