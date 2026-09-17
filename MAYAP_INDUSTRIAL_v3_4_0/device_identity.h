#pragma once

#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

#ifndef MAYAP_ENABLE_LEGACY_DEVICE_MIGRATION
#define MAYAP_ENABLE_LEGACY_DEVICE_MIGRATION 0
#endif

namespace MayapDeviceIdentityInternal {
static char activeKey[65] = "";
static char webPin[9] = "";
// May cu da co PIN tren Worker khong bao gio nhan lai PIN qua HTTPS. Co nay
// cho HMI biet dung PIN cu thay vi hien "DANG DONG BO" vo han.
static bool webPinConfigured = false;
static bool usingLegacyKey = false;

inline void randomHex(char *out, size_t bytes) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  uint8_t data[32];
  if (bytes > sizeof(data)) bytes = sizeof(data);
  esp_fill_random(data, bytes);
  for (size_t i = 0; i < bytes; ++i) {
    out[i * 2U] = HEX_DIGITS[data[i] >> 4U];
    out[i * 2U + 1U] = HEX_DIGITS[data[i] & 0x0FU];
  }
  out[bytes * 2U] = '\0';
}
}

inline void mayapDeviceIdentityBegin() {
  using namespace MayapDeviceIdentityInternal;
  Preferences prefs;
  if (!prefs.begin("mayap-id", false)) return;

  // Doc PIN truoc de phan biet may da hardening/migrate thanh cong voi may
  // dang ket o trang thai 401 do firmware truoc tu sinh device-key ngau nhien.
  const String storedPin = prefs.getString("web-pin", "");
  if (storedPin.length() >= 4U && storedPin.length() < sizeof(webPin)) {
    strlcpy(webPin, storedPin.c_str(), sizeof(webPin));
    webPinConfigured = true;
  } else {
    webPinConfigured = prefs.getBool("web-pin-set", false);
  }

  const String storedKey = prefs.getString("device-key", "");
  const bool storedKeyValid = storedKey.length() >= 32U && storedKey.length() < sizeof(activeKey);
  const bool legacyDone = prefs.getBool("legacy-done", false);
  const bool migrationRequested = (MAYAP_ENABLE_LEGACY_DEVICE_MIGRATION != 0) &&
                                  CLOUD_DEVICE_SECRET[0] && !legacyDone &&
                                  !webPinConfigured;

  if (migrationRequested) {
    // Ban migration CHI dung mot lan: cho phep khoa build cu xac thuc voi
    // Worker, sau do cloud_alert_link xoay ngay sang khoa rieng va ghi vao NVS.
    // Khong xoa storedKey hien co: neu migration that bai, build thuong sau do
    // van con khoa NVS cu de chan doan/khoi phuc.
    strlcpy(activeKey, CLOUD_DEVICE_SECRET, sizeof(activeKey));
    usingLegacyKey = true;
  } else if (storedKeyValid) {
    strlcpy(activeKey, storedKey.c_str(), sizeof(activeKey));
  } else if (CLOUD_DEVICE_SECRET[0]) {
    // May cu chua tung co khoa NVS: dung khoa build cu dung mot lan, sau do
    // cloud_alert_link se xoay sang khoa rieng trong NVS.
    strlcpy(activeKey, CLOUD_DEVICE_SECRET, sizeof(activeKey));
    usingLegacyKey = true;
  } else {
    randomHex(activeKey, 32U);
    prefs.putString("device-key", activeKey);
  }
  prefs.end();
}

inline const char *mayapDeviceSecret() {
  return MayapDeviceIdentityInternal::activeKey;
}

inline bool mayapDeviceUsingLegacySecret() {
  return MayapDeviceIdentityInternal::usingLegacyKey;
}

inline void mayapGenerateDeviceSecret(char out[65]) {
  MayapDeviceIdentityInternal::randomHex(out, 32U);
}

inline bool mayapCommitDeviceSecret(const char *key) {
  using namespace MayapDeviceIdentityInternal;
  if (!key || strlen(key) < 32U || strlen(key) >= sizeof(activeKey)) return false;
  const bool wasLegacy = usingLegacyKey;
  Preferences prefs;
  if (!prefs.begin("mayap-id", false)) return false;
  const bool ok = prefs.putString("device-key", key) > 0U;
  // Marker nay ngan ban migration quay lai khoa cu sau reboot. Ghi best-effort:
  // web-pin-set cung da duoc danh dau ngay khi register 200, nen co hai lop
  // bao ve doc lap neu NVS gap loi ghi hiem gap tai dung thoi diem nay.
  if (ok && wasLegacy) prefs.putBool("legacy-done", true);
  prefs.end();
  if (ok) {
    strlcpy(activeKey, key, sizeof(activeKey));
    usingLegacyKey = false;
  }
  return ok;
}

inline void mayapMarkWebPinConfigured() {
  using namespace MayapDeviceIdentityInternal;
  Preferences prefs;
  if (!prefs.begin("mayap-id", false)) return;
  const bool ok = prefs.putBool("web-pin-set", true);
  prefs.end();
  if (ok) webPinConfigured = true;
}

inline void mayapStoreWebPin(const char *pin) {
  using namespace MayapDeviceIdentityInternal;
  if (!pin || strlen(pin) < 4U || strlen(pin) >= sizeof(webPin)) return;
  Preferences prefs;
  if (!prefs.begin("mayap-id", false)) return;
  const bool ok = prefs.putString("web-pin", pin) > 0U;
  if (ok) prefs.putBool("web-pin-set", true);
  prefs.end();
  if (ok) {
    strlcpy(webPin, pin, sizeof(webPin));
    webPinConfigured = true;
  }
}

inline const char *mayapWebPinText() {
  using namespace MayapDeviceIdentityInternal;
  if (webPin[0]) return webPin;
  return webPinConfigured ? "DUNG PIN CU" : "DANG DONG BO";
}
