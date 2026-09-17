#pragma once

// ============================================================================
// MAYAP v3.8.0 - LOCAL BUILD SECRETS TEMPLATE
//
// Cach dung:
//   1) Copy file nay thanh: build_secrets.h (cung thu muc voi file .ino)
//   2) Dien cac gia tri BAT BUOC ben duoi.
//   3) build_secrets.h da duoc .gitignore, KHONG duoc commit len GitHub.
//
// GitHub Actions tu tao build_secrets.h rieng tu Repository Secrets. File nay
// chi danh cho build/nạp truc tiep bang Arduino IDE tren may local.
// ============================================================================

// ----------------------------- Wi-Fi (tuy chon) -------------------------------
// Co the de rong neu Wi-Fi da duoc cau hinh/lien ket tren may theo co che hien co.
#define MAYAP_WIFI_SSID ""
#define MAYAP_WIFI_PASSWORD ""

// ----------------------------- MQTT (BAT BUOC) --------------------------------
// Lay host/user/password tu dashboard cua MQTT broker dang dung.
// GitHub Secrets KHONG cho xem lai gia tri secret da luu. Neu khong con mat
// khau, tao/rotate credential MQTT moi roi cap nhat CA GitHub Secrets va file
// local nay de hai ben dung cung bo thong tin.
#define MAYAP_MQTT_HOST ""
#define MAYAP_MQTT_PORT 8883
#define MAYAP_MQTT_USE_TLS 1
#define MAYAP_MQTT_USERNAME ""
#define MAYAP_MQTT_PASSWORD ""
#define MAYAP_MQTT_TOPIC_ROOT "mayap/v1"

// -------------------------- Cloud device identity -----------------------------
// Sau khi migration 401 da thanh cong, device-key moi nam trong NVS cua ESP32.
// Build thuong PHAI de hai dong nay nhu ben duoi; KHONG nhung legacy secret cu.
#define MAYAP_DEVICE_SECRET ""
#define MAYAP_ENABLE_LEGACY_DEVICE_MIGRATION 0

// Worker hien tai cua du an. Day la hostname cong khai, khong phai secret.
#define MAYAP_CLOUD_API_HOST "mayap-push-worker.vietk-mayaptrung.workers.dev"

// -------------------------- Arduino OTA qua LAN -------------------------------
// De rong = tat ArduinoOTA. Neu muon dung OTA trong LAN, dat mot mat khau rieng.
#define MAYAP_OTA_PASSWORD ""

// ----------------------------- TLS CA (BAT BUOC) -------------------------------
// Dien PEM CA/bundle dang duoc GitHub Actions dung trong MAYAP_TLS_ROOT_CA.
// KHONG dung leaf/server certificate. KHONG dung setInsecure().
// Vi firmware hien dung TLS_ROOT_CA cho ca Cloud HTTPS va MQTT TLS, bundle nay
// phai xac thuc duoc CA chain cua CAC HAI endpoint.
#define MAYAP_TLS_ROOT_CA ""

// ----------------------- OTA signature public key (BAT BUOC) ------------------
// Day la PUBLIC KEY, khong phai private key. Co the trich lai tu firmware .bin
// hien dang chay/build thanh cong neu ban goc khong con. Tuyet doi KHONG dat
// MAYAP_OTA_SIGNING_PRIVATE_KEY vao firmware/local header nay.
#define MAYAP_OTA_SIGNING_PUBLIC_KEY ""

// ----------------------- Chan build local thieu cau hinh ----------------------
// Cung muc tieu voi CI: khong tao firmware co ve "build duoc" nhung thuc te
// mat MQTT/TLS/chu ky OTA.
static_assert(sizeof(MAYAP_MQTT_HOST) > 1U,
              "LOCAL: dien MAYAP_MQTT_HOST trong build_secrets.h");
static_assert(sizeof(MAYAP_MQTT_USERNAME) > 1U,
              "LOCAL: dien MAYAP_MQTT_USERNAME trong build_secrets.h");
static_assert(sizeof(MAYAP_MQTT_PASSWORD) > 1U,
              "LOCAL: dien MAYAP_MQTT_PASSWORD trong build_secrets.h");
static_assert(sizeof(MAYAP_TLS_ROOT_CA) > 1U,
              "LOCAL: dien MAYAP_TLS_ROOT_CA trong build_secrets.h");
static_assert(sizeof(MAYAP_OTA_SIGNING_PUBLIC_KEY) > 1U,
              "LOCAL: dien MAYAP_OTA_SIGNING_PUBLIC_KEY trong build_secrets.h");
