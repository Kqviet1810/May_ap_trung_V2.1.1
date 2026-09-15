#pragma once

// Sao che thanh secrets.h trong cung thu muc nay. secrets.h da nam trong
// .gitignore va KHONG duoc commit. De trong bat ky gia tri bat buoc nao se
// lam build production dung ngay bang static_assert trong config.h.

#define MAYAP_PRODUCTION_BUILD 1
#define MAYAP_ENABLE_ARDUINO_OTA 0
#define MAYAP_ALLOW_INSECURE_TLS 0

#define MAYAP_MQTT_HOST ""
#define MAYAP_MQTT_PORT 8883
#define MAYAP_MQTT_USE_TLS 1
#define MAYAP_MQTT_USERNAME ""
#define MAYAP_MQTT_PASSWORD ""
#define MAYAP_MQTT_ROOT_CA ""

// Moi may phai co mot secret ngau nhien rieng (toi thieu 32 ky tu) va mot
// PIN xuat xuong rieng dung 6 chu so. Khong tai su dung giua cac may.
#define MAYAP_DEVICE_SECRET ""
#define MAYAP_FACTORY_PIN ""

#define MAYAP_CLOUD_API_HOST "mayap-push-worker.vietk-mayaptrung.workers.dev"
#define MAYAP_CLOUD_ROOT_CA ""

// CA PEM co the dat bang raw string literal, vi du:
// #define MAYAP_MQTT_ROOT_CA R"MAYAPCERT(-----BEGIN CERTIFICATE-----
// ...
// -----END CERTIFICATE-----
// )MAYAPCERT"
