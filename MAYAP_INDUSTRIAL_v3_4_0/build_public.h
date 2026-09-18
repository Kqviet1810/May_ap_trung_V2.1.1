#pragma once

// ============================================================================
// MAYAP v3.8.0 - PUBLIC BUILD CONFIG
//
// Tat ca gia tri trong file nay KHONG PHAI BI MAT va duoc phep commit public.
// Chi MQTT username/password that nam trong build_secrets.h local (.gitignore).
// ============================================================================

#define MAYAP_WIFI_SSID ""
#define MAYAP_WIFI_PASSWORD ""

#define MAYAP_MQTT_HOST "2f4b95444c554498bd4a4b2da0de8013.s1.eu.hivemq.cloud"
#define MAYAP_MQTT_PORT 8883
#define MAYAP_MQTT_USE_TLS 1
#define MAYAP_MQTT_TOPIC_ROOT "mayap/v1"

#define MAYAP_DEVICE_SECRET ""
#define MAYAP_ENABLE_LEGACY_DEVICE_MIGRATION 0

#define MAYAP_CLOUD_API_HOST "mayap-push-worker.vietk-mayaptrung.workers.dev"

#define MAYAP_OTA_PASSWORD ""

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

#define MAYAP_OTA_SIGNING_PUBLIC_KEY R"PEM(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEy4ifrafHxbDs2uYNZ/FNuoayF5nj
43ntTUeL+jx1/XTX62V9st5fG3RUjTxBn4EngBWJ/e/uDpsn1JTihDeFmQ==
-----END PUBLIC KEY-----
)PEM"
