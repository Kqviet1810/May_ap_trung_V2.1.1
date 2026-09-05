#pragma once

#include "config.h"
#include <Arduino.h>
#include <ArduinoOTA.h>

// Nap firmware qua Wi-Fi bang Arduino IDE (Sketch > Upload, chon cong mang
// hien qua mDNS thay vi cong USB) - tien loi khi may da lap dat kin, kho thao
// vo de cam lai cap USB moi lan can cap nhat. Chi mo dich vu khi dang ONLINE
// va da ket noi Wi-Fi that (dua vao mayapGetNetworkStatus() cua network_
// service.h, KHONG tu mo Wi-Fi rieng) - goi mayapOtaUpdate() tu otaTask
// RIENG (xem .ino), KHONG chung voi networkTask: networkTask co the blocking
// toi vai giay moi lan MQTT/Cloud Push lam viec, neu OTA nam chung vong lap
// se co luc khong duoc ArduinoOTA.handle() phan hoi kip, gay loi "No response
// from device" phia Arduino IDE (da gap thuc te, xem lich su sua doi).
//
// Bat buoc mat khau (OTA_PASSWORD trong config.h, dat qua build flag
// MAYAP_OTA_PASSWORD): de trong se TU DONG TAT ca tinh nang nay, tranh mo mot
// cong nap firmware khong xac thuc tren mang LAN.
//
// Luu y an toan: trong luc thuc su ghi tung khoi du lieu vao flash (Update.
// write ben trong ArduinoOTA), ESP-IDF tam dung truy cap cache flash tren CA
// HAI loi CPU trong vai mili giay - anh huong ca controlTask (loi 1). Day la
// gioi han phan cung chung cua moi ung dung OTA tren ESP32, khong rieng gi
// firmware nay. Nen thuc hien nap OTA luc may dang RANH (ngoai me ap) de
// tranh trung hop hiem gap nhieu chu ky dieu khien lien tiep bi cham do cung
// luc voi ghi flash.
namespace MayapOtaInternal {

static bool started = false;
static bool inProgress = false;

inline void onStart() {
  inProgress = true;
  const char *type = (ArduinoOTA.getCommand() == U_FLASH) ? "chuong trinh" : "he thong tep";
  mayapSerialPrintf(true, "[OTA] Bat dau nap %s qua mang...\n", type);
}

inline void onEnd() {
  inProgress = false;
  mayapSerialPrintf(true, "[OTA] Nap xong, chuan bi khoi dong lai\n");
}

inline void onProgress(unsigned int progress, unsigned int total) {
  static uint32_t lastLogAt = 0U;
  const uint32_t now = millis();
  if (lastLogAt != 0U && (now - lastLogAt) < 1000U) return;
  lastLogAt = now;
  const unsigned percent = total ? (progress * 100U) / total : 0U;
  mayapSerialPrintf(false, "[OTA] Da nhan %u%%\n", percent);
}

inline void onError(ota_error_t error) {
  inProgress = false;
  const char *reason = "LOI KHONG XAC DINH";
  switch (error) {
    case OTA_AUTH_ERROR: reason = "SAI MAT KHAU"; break;
    case OTA_BEGIN_ERROR: reason = "KHONG MO DUOC VUNG NHO OTA"; break;
    case OTA_CONNECT_ERROR: reason = "MAT KET NOI"; break;
    case OTA_RECEIVE_ERROR: reason = "LOI NHAN DU LIEU"; break;
    case OTA_END_ERROR: reason = "LOI GHI HOAN TAT"; break;
    default: break;
  }
  mayapSerialPrintf(true, "[OTA] THAT BAI: %s (ma %d)\n", reason,
                    static_cast<int>(error));
}

}  // namespace MayapOtaInternal

inline bool mayapOtaEnabled() {
  return sizeof(OTA_PASSWORD) > 1U;  // build flag rong = tinh nang tat
}

inline bool mayapOtaInProgress() {
  return MayapOtaInternal::inProgress;
}

// Goi 1 lan trong setup(): chi dang ky cau hinh/callback, CHUA mo cong mang
// nao (an toan goi truoc khi Wi-Fi ket noi, thu tu giong cac module khac).
inline void mayapOtaBegin() {
  if (!mayapOtaEnabled()) return;
  ArduinoOTA.setHostname(NETWORK_WIFI_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart(MayapOtaInternal::onStart);
  ArduinoOTA.onEnd(MayapOtaInternal::onEnd);
  ArduinoOTA.onProgress(MayapOtaInternal::onProgress);
  ArduinoOTA.onError(MayapOtaInternal::onError);
}

// Goi moi chu ky tu networkTask. Chi thuc su bat dich vu OTA (mo UDP/mDNS)
// khi da ket noi Wi-Fi that; tu dong dong lai khi mat mang/chuyen OFFLINE de
// khong giu tai nguyen mang vo ich.
inline void mayapOtaUpdate(uint32_t now) {
  (void)now;
  if (!mayapOtaEnabled()) return;

  const NetworkStatus status = mayapGetNetworkStatus();
  const bool shouldRun =
      status.requestedMode == ConnectivityMode::Online && status.connected;

  if (!shouldRun) {
    if (MayapOtaInternal::started) {
      ArduinoOTA.end();
      MayapOtaInternal::started = false;
      MayapOtaInternal::inProgress = false;
    }
    return;
  }

  if (!MayapOtaInternal::started) {
    ArduinoOTA.begin();
    MayapOtaInternal::started = true;
    mayapSerialPrintf(false, "[OTA] San sang nhan firmware qua Wi-Fi (%s.local)\n",
                      NETWORK_WIFI_HOSTNAME);
  }
  ArduinoOTA.handle();
}
