#pragma once

// Sao che thanh secrets.h trong cung thu muc nay. secrets.h da nam trong
// .gitignore va KHONG duoc commit. Firmware phat hanh chung khong can file
// nay: MQTT/device secret/PIN duoc provision vao NVS qua cong doi Wi-Fi.

#define MAYAP_PRODUCTION_BUILD 1
#define MAYAP_ENABLE_ARDUINO_OTA 0
#define MAYAP_ALLOW_INSECURE_TLS 0

#define MAYAP_MQTT_PORT 8883
#define MAYAP_MQTT_USE_TLS 1

#define MAYAP_CLOUD_API_HOST "mayap-push-worker.vietk-mayaptrung.workers.dev"

// certificates.h da kem ISRG Root X1 + GTS Root R4. Chi override khi broker
// dung CA khac, vi du:
// #define MAYAP_MQTT_ROOT_CA R"MAYAPCERT(-----BEGIN CERTIFICATE-----
// ...
// -----END CERTIFICATE-----
// )MAYAPCERT"
