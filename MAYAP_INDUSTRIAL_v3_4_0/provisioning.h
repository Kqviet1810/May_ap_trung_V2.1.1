#pragma once

#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>

// Cau hinh rieng tung thiet bi duoc giu trong NVS, doc lap voi firmware OTA.
// Cac macro trong config.h chi la gia tri khoi tao mot lan de di tru firmware
// cu; ban phat hanh chung de trong toan bo credential.
namespace MayapProvisioningInternal {

constexpr char NVS_NAMESPACE[] = "mayap_conn";
constexpr size_t MQTT_HOST_CAPACITY = 128U;
constexpr size_t MQTT_USER_CAPACITY = 65U;
constexpr size_t MQTT_PASSWORD_CAPACITY = 129U;
constexpr size_t DEVICE_SECRET_CAPACITY = 129U;
constexpr size_t FACTORY_PIN_CAPACITY = 7U;

static Preferences prefs;
static bool loaded = false;
static char mqttHost[MQTT_HOST_CAPACITY] = "";
static uint16_t mqttPort = 8883U;
static char mqttUsername[MQTT_USER_CAPACITY] = "";
static char mqttPassword[MQTT_PASSWORD_CAPACITY] = "";
static char deviceSecret[DEVICE_SECRET_CAPACITY] = "";
static char factoryPin[FACTORY_PIN_CAPACITY] = "";
static uint32_t revision = 0U;

inline bool hostValid(const char *value) {
  if (!value) return false;
  const size_t len = strlen(value);
  if (len < 1U || len >= MQTT_HOST_CAPACITY) return false;
  for (size_t i = 0U; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (!(isalnum(c) || c == '.' || c == '-')) return false;
  }
  return value[0] != '.' && value[len - 1U] != '.';
}

inline bool pinValid(const char *value) {
  if (!value || strlen(value) != 6U) return false;
  for (size_t i = 0U; i < 6U; ++i) {
    if (!isdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
}

inline void copyText(char *out, size_t capacity, const String &value) {
  snprintf(out, capacity, "%s", value.c_str());
}

inline void loadText(const char *key, const char *fallback, char *out,
                     size_t capacity) {
  String value = prefs.getString(key, "");
  if (value.isEmpty() && fallback && fallback[0]) {
    value = fallback;
    prefs.putString(key, value);
  }
  copyText(out, capacity, value);
}

inline bool begin() {
  if (loaded) return true;
  if (!prefs.begin(NVS_NAMESPACE, false)) return false;
  loadText("mqtt_host", MQTT_BROKER_HOST, mqttHost, sizeof(mqttHost));
  mqttPort = prefs.getUShort("mqtt_port", MQTT_BROKER_PORT);
  if (mqttPort == 0U) mqttPort = 8883U;
  loadText("mqtt_user", MQTT_USERNAME, mqttUsername, sizeof(mqttUsername));
  loadText("mqtt_pass", MQTT_PASSWORD, mqttPassword, sizeof(mqttPassword));
  loadText("device_key", CLOUD_DEVICE_SECRET, deviceSecret, sizeof(deviceSecret));
  loadText("factory_pin", CLOUD_FACTORY_PIN, factoryPin, sizeof(factoryPin));
  loaded = true;
  revision = 1U;
  return true;
}

inline void ensureLoaded() {
  if (!loaded) (void)begin();
}

inline bool mqttReady() {
  ensureLoaded();
  return hostValid(mqttHost) && mqttPort != 0U && mqttUsername[0] &&
         mqttPassword[0];
}

inline bool cloudReady() {
  ensureLoaded();
  const size_t secretLength = strlen(deviceSecret);
  return secretLength >= 32U && secretLength <= 128U && pinValid(factoryPin);
}

inline bool saveServiceConfig(const char *host, uint16_t port,
                              const char *username, const char *password,
                              const char *secret, const char *pin) {
  ensureLoaded();
  const String nextHost = host && host[0] ? host : mqttHost;
  const String nextUser = username && username[0] ? username : mqttUsername;
  const String nextPassword = password && password[0] ? password : mqttPassword;
  const String nextSecret = secret && secret[0] ? secret : deviceSecret;
  const String nextPin = pin && pin[0] ? pin : factoryPin;
  const uint16_t nextPort = port ? port : mqttPort;

  if (!hostValid(nextHost.c_str()) || nextPort == 0U || nextUser.isEmpty() ||
      nextUser.length() >= MQTT_USER_CAPACITY || nextPassword.isEmpty() ||
      nextPassword.length() >= MQTT_PASSWORD_CAPACITY ||
      nextSecret.length() < 32U || nextSecret.length() > 128U ||
      !pinValid(nextPin.c_str())) {
    return false;
  }

  if (prefs.putString("mqtt_host", nextHost) != nextHost.length() ||
      prefs.putUShort("mqtt_port", nextPort) != sizeof(uint16_t) ||
      prefs.putString("mqtt_user", nextUser) != nextUser.length() ||
      prefs.putString("mqtt_pass", nextPassword) != nextPassword.length() ||
      prefs.putString("device_key", nextSecret) != nextSecret.length() ||
      prefs.putString("factory_pin", nextPin) != nextPin.length()) {
    return false;
  }

  snprintf(mqttHost, sizeof(mqttHost), "%s", nextHost.c_str());
  mqttPort = nextPort;
  snprintf(mqttUsername, sizeof(mqttUsername), "%s", nextUser.c_str());
  snprintf(mqttPassword, sizeof(mqttPassword), "%s", nextPassword.c_str());
  snprintf(deviceSecret, sizeof(deviceSecret), "%s", nextSecret.c_str());
  snprintf(factoryPin, sizeof(factoryPin), "%s", nextPin.c_str());
  ++revision;
  if (revision == 0U) revision = 1U;
  return true;
}

}  // namespace MayapProvisioningInternal

inline bool mayapProvisioningBegin() {
  return MayapProvisioningInternal::begin();
}
inline const char *mayapMqttHost() {
  MayapProvisioningInternal::ensureLoaded();
  return MayapProvisioningInternal::mqttHost;
}
inline uint16_t mayapMqttPort() {
  MayapProvisioningInternal::ensureLoaded();
  return MayapProvisioningInternal::mqttPort;
}
inline const char *mayapMqttUsername() {
  MayapProvisioningInternal::ensureLoaded();
  return MayapProvisioningInternal::mqttUsername;
}
inline const char *mayapMqttPassword() {
  MayapProvisioningInternal::ensureLoaded();
  return MayapProvisioningInternal::mqttPassword;
}
inline const char *mayapDeviceSecret() {
  MayapProvisioningInternal::ensureLoaded();
  return MayapProvisioningInternal::deviceSecret;
}
inline const char *mayapFactoryPin() {
  MayapProvisioningInternal::ensureLoaded();
  return MayapProvisioningInternal::factoryPin;
}
inline bool mayapMqttProvisioned() {
  return MayapProvisioningInternal::mqttReady();
}
inline bool mayapCloudProvisioned() {
  return MayapProvisioningInternal::cloudReady();
}
inline uint32_t mayapProvisioningRevision() {
  MayapProvisioningInternal::ensureLoaded();
  return MayapProvisioningInternal::revision;
}
inline bool mayapSaveServiceProvisioning(const char *host, uint16_t port,
                                         const char *username,
                                         const char *password,
                                         const char *secret,
                                         const char *pin) {
  return MayapProvisioningInternal::saveServiceConfig(
      host, port, username, password, secret, pin);
}

static_assert(sizeof(MayapProvisioningInternal::NVS_NAMESPACE) <= 16U,
              "NVS namespace toi da 15 ky tu");
