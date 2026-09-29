#pragma once
#include <Arduino.h>

// MQTT handshake, Cloud HTTPS and OTA HTTPS formerly allocated TLS working
// sets concurrently on the no-PSRAM board. Keep one transient TLS operation
// at a time; the established MQTT connection continues pumping independently.
namespace MayapNetworkIoInternal {
static uint8_t tlsBusy = 0U;
}
class MayapTlsOperation {
 public:
  MayapTlsOperation() {
    uint8_t expected = 0U;
    acquired_ = __atomic_compare_exchange_n(&MayapNetworkIoInternal::tlsBusy,
        &expected, 1U, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (acquired_ && (ESP.getFreeHeap() < 32768U || ESP.getMaxAllocHeap() < 16384U)) {
      __atomic_store_n(&MayapNetworkIoInternal::tlsBusy, 0U, __ATOMIC_RELEASE);
      acquired_ = false;
    }
  }
  ~MayapTlsOperation() {
    if (acquired_) __atomic_store_n(&MayapNetworkIoInternal::tlsBusy, 0U, __ATOMIC_RELEASE);
  }
  explicit operator bool() const { return acquired_; }
  MayapTlsOperation(const MayapTlsOperation &) = delete;
  MayapTlsOperation &operator=(const MayapTlsOperation &) = delete;
 private:
  bool acquired_ = false;
};
