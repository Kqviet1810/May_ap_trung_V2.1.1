#pragma once

// ============================================================================
// MAYAP v3.8.0 - LOCAL ARDUINO BUILD CONFIG
//
// File nay duoc de san trong nhanh de co the mo .ino va Upload truc tiep.
// CAC GIA TRI CONG KHAI da duoc dien san: broker host, Worker host, TLS CA,
// OTA verification PUBLIC KEY. Khong can file .bin cu.
//
// Khi build local, chi can dien MAYAP_MQTT_USERNAME va MAYAP_MQTT_PASSWORD.
// KHONG commit/push lai file nay sau khi da dien password that.
// OTA SIGNING PRIVATE KEY TUYET DOI khong nam trong firmware.
// ============================================================================

// Wi-Fi: de rong neu may tu cau hinh Wi-Fi bang portal/HMI.
#define MAYAP_WIFI_SSID ""
#define MAYAP_WIFI_PASSWORD ""

// MQTT realtime.
#define MAYAP_MQTT_HOST "2f4b95444c554498bd4a4b2da0de8013.s1.eu.hivemq.cloud"
#define MAYAP_MQTT_PORT 8883
#define MAYAP_MQTT_USE_TLS 1
#define MAYAP_MQTT_USERNAME ""
#define MAYAP_MQTT_PASSWORD ""
#define MAYAP_MQTT_TOPIC_ROOT "mayap/v1"

// Fresh-install only: moi may tu sinh device_key va luu NVS.
// Khong dung legacy secret nua.
#define MAYAP_DEVICE_SECRET ""
#define MAYAP_ENABLE_LEGACY_DEVICE_MIGRATION 0

// Cloud Push Worker.
#define MAYAP_CLOUD_API_HOST "mayap-push-worker.vietk-mayaptrung.workers.dev"

// ArduinoOTA qua LAN. De rong = tat.
#define MAYAP_OTA_PASSWORD ""

// Public trust anchor dang duoc firmware hien tai dung.
#define MAYAP_TLS_ROOT_CA R"PEM(
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM"

// PUBLIC KEY dung de ESP32 xac minh chu ky OTA.
#define MAYAP_OTA_SIGNING_PUBLIC_KEY R"PEM(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEy4ifrafHxbDs2uYNZ/FNuoayF5nj
43ntTUeL+jx1/XTX62V9st5fG3RUjTxBn4EngBWJ/e/uDpsn1JTihDeFmQ==
-----END PUBLIC KEY-----
)PEM"
