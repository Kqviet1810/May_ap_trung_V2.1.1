#pragma once

#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <stdint.h>

// Wi-Fi duoc cach ly khoi task dieu khien. File nay chi duoc goi boi
// networkTask (tru mayapSetConnectivityMode/mayapGetNetworkStatus dung atomic).
namespace MayapNetworkInternal {

constexpr size_t SSID_LENGTH = sizeof(NETWORK_WIFI_SSID) - 1U;
constexpr size_t PASSWORD_LENGTH = sizeof(NETWORK_WIFI_PASSWORD) - 1U;
constexpr bool CREDENTIALS_CONFIGURED =
    SSID_LENGTH > 0U &&
    (PASSWORD_LENGTH == 0U || PASSWORD_LENGTH >= 8U);

static volatile uint8_t requestedMode =
    static_cast<uint8_t>(ConnectivityMode::Offline);
static volatile uint8_t publishedState =
    static_cast<uint8_t>(NetworkStateCode::Offline);
static volatile bool publishedConfigured = CREDENTIALS_CONFIGURED;
static volatile bool publishedConnected = false;
static volatile int8_t publishedRssiDbm = -127;

static bool radioActive = false;
static uint32_t connectionStartedAt = 0U;
static uint32_t lastRetryAt = 0U;
static uint32_t lastStartAttemptAt = 0U;
static bool startAttempted = false;

inline void publish(NetworkStateCode state, bool connected,
                    int8_t rssiDbm = -127) {
  __atomic_store_n(&publishedConfigured, CREDENTIALS_CONFIGURED,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&publishedConnected, connected, __ATOMIC_RELEASE);
  __atomic_store_n(&publishedRssiDbm, rssiDbm, __ATOMIC_RELEASE);
  __atomic_store_n(&publishedState, static_cast<uint8_t>(state),
                   __ATOMIC_RELEASE);
}

inline void stopRadio() {
  WiFi.setAutoReconnect(false);
  (void)WiFi.disconnect(true, false);
  radioActive = false;
  connectionStartedAt = 0U;
  lastRetryAt = 0U;
  lastStartAttemptAt = 0U;
  startAttempted = false;
}

inline bool startStation(uint32_t now) {
  // Tai lieu Arduino-ESP32 yeu cau hostname duoc dat truoc khi khoi dong Wi-Fi.
  (void)WiFi.disconnect(true, false);
  if (!WiFi.setHostname(NETWORK_WIFI_HOSTNAME)) {
    stopRadio();
    return false;
  }
  if (!WiFi.mode(WIFI_STA)) {
    stopRadio();
    return false;
  }
  (void)WiFi.setAutoReconnect(true);
  const char *password = PASSWORD_LENGTH == 0U ? nullptr
                                               : NETWORK_WIFI_PASSWORD;
  (void)WiFi.begin(NETWORK_WIFI_SSID, password);
  radioActive = true;
  connectionStartedAt = now;
  lastRetryAt = now - NETWORK_RETRY_INTERVAL_MS;
  publish(NetworkStateCode::Connecting, false);
  return true;
}

}  // namespace MayapNetworkInternal

inline void mayapNetworkBegin() {
  using namespace MayapNetworkInternal;
  __atomic_store_n(&requestedMode,
                   static_cast<uint8_t>(ConnectivityMode::Offline),
                   __ATOMIC_RELEASE);
  stopRadio();
  publish(NetworkStateCode::Offline, false);
}

inline void mayapSetConnectivityMode(ConnectivityMode mode) {
  if (static_cast<uint8_t>(mode) >
      static_cast<uint8_t>(ConnectivityMode::Online)) {
    mode = ConnectivityMode::Offline;
  }
  __atomic_store_n(&MayapNetworkInternal::requestedMode,
                   static_cast<uint8_t>(mode), __ATOMIC_RELEASE);
}

inline NetworkStatus mayapGetNetworkStatus() {
  using namespace MayapNetworkInternal;
  NetworkStatus status{};
  uint8_t mode = __atomic_load_n(&requestedMode, __ATOMIC_ACQUIRE);
  if (mode > static_cast<uint8_t>(ConnectivityMode::Online)) {
    mode = static_cast<uint8_t>(ConnectivityMode::Offline);
  }
  uint8_t state = __atomic_load_n(&publishedState, __ATOMIC_ACQUIRE);
  if (state > static_cast<uint8_t>(NetworkStateCode::Connected)) {
    state = static_cast<uint8_t>(NetworkStateCode::Offline);
  }
  status.requestedMode = static_cast<ConnectivityMode>(mode);
  status.state = static_cast<NetworkStateCode>(state);
  status.credentialsConfigured = __atomic_load_n(
      &publishedConfigured, __ATOMIC_ACQUIRE);
  status.connected = __atomic_load_n(&publishedConnected, __ATOMIC_ACQUIRE);
  status.rssiDbm = __atomic_load_n(&publishedRssiDbm, __ATOMIC_ACQUIRE);
  return status;
}

inline void mayapNetworkUpdate(uint32_t now) {
  using namespace MayapNetworkInternal;
  const uint8_t requested = __atomic_load_n(&requestedMode, __ATOMIC_ACQUIRE);
  const bool onlineRequested =
      requested == static_cast<uint8_t>(ConnectivityMode::Online);

  if (!onlineRequested) {
    if (radioActive) stopRadio();
    publish(NetworkStateCode::Offline, false);
    return;
  }

  if (!CREDENTIALS_CONFIGURED) {
    if (radioActive) stopRadio();
    publish(NetworkStateCode::NotConfigured, false);
    return;
  }

  if (!radioActive) {
    if (startAttempted &&
        now - lastStartAttemptAt < NETWORK_RETRY_INTERVAL_MS) {
      publish(NetworkStateCode::Connecting, false);
      return;
    }
    const bool started = startStation(now);
    lastStartAttemptAt = now;
    startAttempted = true;
    if (!started) {
      publish(NetworkStateCode::Connecting, false);
      return;
    }
  }

  if (WiFi.isConnected()) {
    int32_t rssi = WiFi.RSSI();
    if (rssi < -127) rssi = -127;
    if (rssi > 0) rssi = 0;
    publish(NetworkStateCode::Connected, true,
            static_cast<int8_t>(rssi));
    return;
  }

  publish(NetworkStateCode::Connecting, false);
  if (now - connectionStartedAt < NETWORK_CONNECT_TIMEOUT_MS ||
      now - lastRetryAt < NETWORK_RETRY_INTERVAL_MS) {
    return;
  }

  lastRetryAt = now;
  connectionStartedAt = now;
  if (!WiFi.reconnect()) {
    const char *password = PASSWORD_LENGTH == 0U ? nullptr
                                                 : NETWORK_WIFI_PASSWORD;
    (void)WiFi.begin(NETWORK_WIFI_SSID, password);
  }
}
