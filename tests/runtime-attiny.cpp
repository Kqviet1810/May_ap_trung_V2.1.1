// Execute the actual ESP bus driver against a deterministic GPIO/RMT HAL.
// Hardware events/IRAM capture continue while the foreground is stalled.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <algorithm>
#include <vector>

#define IRAM_ATTR
#define MAYAP_DIAGNOSTIC_SERIAL 0
#define portMUX_INITIALIZER_UNLOCKED 0
using portMUX_TYPE = int;
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
constexpr int HIGH = 1, LOW = 0, ESP_OK = 0, ESP_FAIL = -1;
constexpr int ESP_INTR_FLAG_IRAM = 1, GPIO_MODE_INPUT_OUTPUT_OD = 1;
constexpr int GPIO_PULLUP_ENABLE = 1, GPIO_PULLDOWN_DISABLE = 0;
constexpr int GPIO_INTR_DISABLE = 0, GPIO_INTR_ANYEDGE = 1, RMT_CLK_SRC_DEFAULT = 0;
using gpio_num_t = int;
using rmt_channel_handle_t = void *;
using rmt_encoder_handle_t = void *;
struct rmt_tx_done_event_data_t {};
struct gpio_config_t { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; };
struct rmt_symbol_word_t { uint16_t duration0, duration1; uint8_t level0, level1; };
struct rmt_copy_encoder_config_t {};
struct rmt_tx_channel_config_t {
  int gpio_num, clk_src; uint32_t resolution_hz; size_t mem_block_symbols, trans_queue_depth;
  struct { uint32_t io_od_mode, io_loop_back, init_level; } flags;
};
struct rmt_transmit_config_t {
  int loop_count; struct { uint32_t eot_level, queue_nonblocking; } flags;
};
using DoneCallback = bool (*)(rmt_channel_handle_t, const rmt_tx_done_event_data_t *, void *);
struct rmt_tx_event_callbacks_t { DoneCallback on_trans_done; };

static uint64_t clockUs = 1000000;
static int busLevel = HIGH;
static bool initFailure = false, txFailure = false, deferDone = false;
static bool respond = true, badParity = false, overflowReply = false;
static uint8_t replyFlags = 0;
static uint32_t measuredLowUs = 0;
static DoneCallback doneCallback = nullptr;
static void (*gpioHandler)(void *) = nullptr;
struct Event { uint64_t at; int level; bool done; };
static std::vector<Event> events;
static uint32_t millis() { return static_cast<uint32_t>(clockUs / 1000); }
static uint32_t micros() { return static_cast<uint32_t>(clockUs); }
static int64_t esp_timer_get_time() { return clockUs; }
static int gpio_get_level(gpio_num_t) { return busLevel; }
static int gpio_set_level(gpio_num_t, unsigned level) { busLevel = level; return ESP_OK; }
static int gpio_config(const gpio_config_t *) { return initFailure ? ESP_FAIL : ESP_OK; }
static int gpio_install_isr_service(int flags) { assert(flags == ESP_INTR_FLAG_IRAM); return ESP_OK; }
static int gpio_set_intr_type(gpio_num_t, int type) { assert(type == GPIO_INTR_ANYEDGE); return ESP_OK; }
static int gpio_isr_handler_add(gpio_num_t, void (*handler)(void *), void *) { gpioHandler = handler; return ESP_OK; }
static int rmt_new_tx_channel(const rmt_tx_channel_config_t *c, rmt_channel_handle_t *h) {
  assert(c->flags.io_od_mode && c->flags.io_loop_back && c->flags.init_level);
  assert(c->mem_block_symbols == 48 && c->resolution_hz == 1000000 && c->trans_queue_depth == 1);
  *h = reinterpret_cast<void *>(1); return ESP_OK;
}
static int rmt_new_copy_encoder(const rmt_copy_encoder_config_t *, rmt_encoder_handle_t *h) {
  *h = reinterpret_cast<void *>(2); return ESP_OK;
}
static int rmt_tx_register_event_callbacks(rmt_channel_handle_t, const rmt_tx_event_callbacks_t *cb, void *) {
  doneCallback = cb->on_trans_done; return ESP_OK;
}
static int rmt_enable(rmt_channel_handle_t) { return ESP_OK; }
static int rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void *data, size_t bytes,
                        const rmt_transmit_config_t *cfg) {
  assert(cfg->flags.eot_level == 1 && cfg->flags.queue_nonblocking);
  if (txFailure) return ESP_FAIL;
  assert(events.empty());
  const auto *wave = static_cast<const rmt_symbol_word_t *>(data);
  const size_t count = bytes / sizeof(*wave);
  assert(count <= 9 && count < 48 / 2); // Even initial half holds the full payload + EOF.
  uint64_t cursor = clockUs;
  measuredLowUs = 0;
  bool highSeen = false;
  for (size_t i = 0; i < count; ++i) {
    const uint16_t durations[] = {wave[i].duration0, wave[i].duration1};
    const uint8_t levels[] = {wave[i].level0, wave[i].level1};
    for (unsigned half = 0; half < 2; ++half) {
      assert(durations[half] > 0 && durations[half] <= 32767);
      if (levels[half] == LOW) { assert(!highSeen); measuredLowUs += durations[half]; }
      else highSeen = true;
      events.push_back({cursor, levels[half], false});
      cursor += durations[half];
    }
  }
  events.push_back({cursor, HIGH, true});
  if (respond) {
    // Tiny EEPROM update (worst path) + 40 ms response guard.
    cursor += 48500;
    events.push_back({cursor, LOW, false}); cursor += 60000;
    events.push_back({cursor, HIGH, false}); cursor += 15000;
    uint8_t parity = 0;
    for (unsigned i = 0; i < 5; ++i) {
      uint8_t bit = i < 4 ? (replyFlags >> i) & 1U : parity ^ badParity;
      if (i < 4) parity ^= bit;
      events.push_back({cursor, LOW, false}); cursor += bit ? 30000 : 10000;
      events.push_back({cursor, HIGH, false}); cursor += 15000;
    }
    if (overflowReply) for (unsigned i = 0; i < 4; ++i) {
      events.push_back({cursor, static_cast<int>(i % 2), false}); cursor += 1000;
    }
  }
  return ESP_OK;
}
static void advance(uint64_t target) {
  while (!events.empty() && events.front().at <= target) {
    const Event e = events.front(); events.erase(events.begin()); clockUs = e.at;
    if (busLevel != e.level) { busLevel = e.level; if (gpioHandler) gpioHandler(nullptr); }
    if (e.done && !deferDone) doneCallback(nullptr, nullptr, nullptr);
  }
  clockUs = target;
}
#include "actual-attiny-config.inc"
#include "actual-gpio_interrupts.inc"
#include "actual-attiny_bus.inc"
#include "actual-boot-mailbox.inc"
#include "../MAYAP_INDUSTRIAL_v4_0_0/startup_output_policy.h"

static void reset() {
  using namespace MayapAttinyBusInternal;
  events.clear(); clockUs = 1000000; busLevel = HIGH;
  initFailure = txFailure = deferDone = badParity = overflowReply = false;
  respond = true; replyFlags = 0;
  txHead_ = txTail_ = txCount_ = txCode_ = txAttempt_ = incomingCode_ = 0;
  resultReady_ = resultAcked_ = driverReady_ = idleBlocked_ = false;
  txHoldUntil_ = 0; phase_ = Phase::Idle; rxEnabled_ = false;
  mayapAttinyBusBegin(); advance(clockUs + 31000);
}
static bool result(uint8_t expected) {
  uint8_t code = 0; bool ok = false;
  assert(mayapAttinyBusTakeResult(code, ok) && code == expected);
  return ok;
}
static void pollUntilResult() {
  for (unsigned i = 0; i < 400 && !MayapAttinyBusInternal::resultReady_; ++i) {
    mayapAttinyBusUpdate(millis()); advance(clockUs + 10000);
  }
  assert(MayapAttinyBusInternal::resultReady_);
}
struct Request {
  bool heaterSsr=true, heatMaster=true, turnLeft=true, turnRight=true, light=true, humidifier=true;
  bool circulationFan=true, ventFan=true, ventFanForceOn=true, ventFanBypassTiming=false;
  bool immediateMasterDrop=false, siren=true;
};
int main() {
  assert(!mayapBootOperationsReady());
  mayapBootReleaseHome(); // Coordinator exit request is not a displayed frame.
  assert(mayapBootHomeReleased() && !mayapBootOperationsReady());
  mayapBootAcknowledgeHomeFrame();
  assert(mayapBootOperationsReady());
  unsigned transfers = 0;
  for (uint8_t command = 1; command <= 7; ++command) for (uint8_t flags = 0; flags < 16; ++flags) {
    for (uint32_t stallMs : {0U, 300U, 600U, 1100U}) {
      reset(); replyFlags = flags;
      assert(mayapAttinyBusRequest(command));
      // A stale loop sample must not backdate physical TX/deadlines.
      mayapAttinyBusUpdate(0);
      assert(MayapAttinyBusInternal::txStartedAt_ == millis());
      assert(measuredLowUs == ATTINY_COMMAND_WIDTH_MS[command] * 1000U);
      advance(clockUs + stallMs * 1000ULL);
      pollUntilResult(); assert(result(command));
      assert(mayapAttinyBusPollIncoming() == ATTINY_MSG_STATUS_BASE + flags);
      assert(busLevel == HIGH); ++transfers;
    }
  }
  reset(); respond = false; assert(mayapAttinyBusRequest(5)); pollUntilResult();
  assert(!result(5) && MayapAttinyBusInternal::txAttempt_ == ATTINY_BUS_MAX_RETRY);
  assert(mayapAttinyBusPollIncoming() == 0 && busLevel == HIGH);
  reset(); busLevel = LOW; assert(mayapAttinyBusRequest(5)); pollUntilResult(); assert(!result(5));
  reset(); badParity = true; assert(mayapAttinyBusRequest(5)); pollUntilResult(); assert(!result(5));
  reset(); overflowReply = true; assert(mayapAttinyBusRequest(5)); pollUntilResult(); assert(!result(5));
  reset(); txFailure = true; assert(mayapAttinyBusRequest(5)); pollUntilResult(); assert(!result(5));
  assert(busLevel == HIGH);
  reset(); initFailure = true; mayapAttinyBusBegin(); assert(mayapAttinyBusRequest(5));
  pollUntilResult(); assert(!result(5));
  reset(); for (uint8_t c = 1; c <= 7; ++c) assert(mayapAttinyBusRequest(c));
  assert(mayapAttinyBusRequest(7)); assert(!mayapAttinyBusRequest(0)); assert(!mayapAttinyBusRequest(8));
  pollUntilResult();
  const uint8_t queued = MayapAttinyBusInternal::txCount_;
  advance(clockUs + 100000); mayapAttinyBusUpdate(0);
  assert(MayapAttinyBusInternal::txCount_ == queued); assert(result(1));
  for (uint8_t c = 2; c <= 7; ++c) { pollUntilResult(); assert(result(c)); }
  reset(); clockUs = (1ULL << 32) - 50000; // capture timestamp wraps during reply
  assert(mayapAttinyBusRequest(5)); pollUntilResult(); assert(result(5));
  reset(); clockUs = ((1ULL << 32) - 100) * 1000; // millis wraps during command
  assert(mayapAttinyBusRequest(5)); pollUntilResult(); assert(result(5));
  reset(); deferDone = true; assert(mayapAttinyBusRequest(5));
  pollUntilResult(); assert(!result(5)); // Driver completion missing is bounded, not silently ignored.
  assert(busLevel == HIGH);
  for (bool cooling : {false, true}) for (bool siren : {false, true}) {
    Request r; mayapApplyStartupOutputPolicy(r, false, cooling, siren);
    assert(!r.heaterSsr && !r.heatMaster && !r.turnLeft && !r.turnRight && !r.light && !r.humidifier);
    assert(r.immediateMasterDrop && r.ventFan == cooling && r.circulationFan == cooling && r.siren == siren);
    assert(r.ventFanForceOn == cooling && r.ventFanBypassTiming == cooling);
  }
  Request ready; mayapApplyStartupOutputPolicy(ready, true, false, false);
  assert(ready.light && ready.heaterSsr && !ready.immediateMasterDrop);
  std::printf("Actual ATtiny bus: %u transfers, foreground stalls, corruption, disconnect, TX failure, rollover and startup interlock OK\n", transfers);
}
