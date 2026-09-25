#include "config.h"
#include <esp_timer.h>

static volatile bool gMayapSystemTripLatched = false;

bool mayapSystemTripLatched() {
  return __atomic_load_n(&gMayapSystemTripLatched, __ATOMIC_ACQUIRE);
}

void mayapLatchSystemTrip() {
  __atomic_store_n(&gMayapSystemTripLatched, true, __ATOMIC_RELEASE);
}

static StaticSemaphore_t i2cMutexBuffer;
static SemaphoreHandle_t i2cMutex = nullptr;

bool mayapI2cLock(uint32_t timeoutMs) {
  if (!i2cMutex) return false;
  const TickType_t ticks = timeoutMs ? pdMS_TO_TICKS(timeoutMs) : 0;
  return xSemaphoreTake(i2cMutex, ticks) == pdTRUE;
}

void mayapI2cUnlock() {
  if (i2cMutex) xSemaphoreGive(i2cMutex);
}

#include "device_identity.h"
#include "network_service.h"
#include "ota_update.h"
#include "ota_web_update.h"
#include "ota_rollback.h"
#include "hmi.h"
#include "history_store.h"
#define MQTT_USE_TLS MAYAP_MQTT_USE_TLS
#include "realtime_link.h"
#undef MQTT_USE_TLS
#include "cloud_alert_link.h"
#include "attiny_bus.h"
#include "machine_control.h"

using namespace Mayap;

// Core 1 duoc giu rieng cho dieu khien an toan. Core 0 tach thanh cac task
// I/O doc lap de HTTPS/NTP/OTA khong the chan mqtt.loop(). Tat ca stack tinh,
// khong tao/xoa task trong runtime.
constexpr uint32_t NETWORK_FAST_TASK_PERIOD_MS = 50UL;
constexpr uint32_t MQTT_TASK_PERIOD_MS = 20UL;
constexpr uint32_t CLOUD_TASK_PERIOD_MS = 100UL;
constexpr size_t MQTT_TASK_STACK_BYTES = 12288U;
constexpr size_t CLOUD_TASK_STACK_BYTES = 12288U;

static StaticTask_t controlTaskTcb;
static StackType_t controlTaskStack[
    (CONTROL_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t controlTaskHandle = nullptr;

static StaticTask_t hmiTaskTcb;
static StackType_t hmiTaskStack[
    (HMI_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t hmiTaskHandle = nullptr;

static StaticTask_t supervisorTaskTcb;
static StackType_t supervisorTaskStack[
    (SUPERVISOR_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t supervisorTaskHandle = nullptr;

static StaticTask_t networkTaskTcb;
static StackType_t networkTaskStack[
    (NETWORK_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t networkTaskHandle = nullptr;

static StaticTask_t mqttTaskTcb;
static StackType_t mqttTaskStack[
    (MQTT_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t mqttTaskHandle = nullptr;

static StaticTask_t cloudTaskTcb;
static StackType_t cloudTaskStack[
    (CLOUD_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t cloudTaskHandle = nullptr;

static StaticTask_t otaTaskTcb;
static StackType_t otaTaskStack[
    (OTA_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t otaTaskHandle = nullptr;

// Hai co nay chi dung de portal cho mot I/O mang dang block ket thuc truoc
// khi doi mode radio. Task ghi co cua chinh no, networkTask chi doc.
static volatile uint8_t mqttIoBusy = 0U;
static volatile uint8_t cloudIoBusy = 0U;
static bool mqttWriteQos1Promoted = false;

static volatile uint32_t controlHeartbeatMs = 0U;
static volatile uint32_t hmiHeartbeatMs = 0U;
static volatile uint32_t controlLastCycleUs = 0U;
static volatile uint32_t controlMaxCycleUs = 0U;
static volatile uint8_t controlTripCycleCount = 0U;
static volatile uint32_t hmiLastCycleUs = 0U;
static volatile uint32_t hmiMaxCycleUs = 0U;
static volatile uint8_t hmiTripCycleCount = 0U;

static_assert(sizeof(controlTaskStack) >= CONTROL_TASK_STACK_BYTES,
              "Control stack buffer qua nho");
static_assert(sizeof(hmiTaskStack) >= HMI_TASK_STACK_BYTES,
              "HMI stack buffer qua nho");
static_assert(sizeof(supervisorTaskStack) >= SUPERVISOR_TASK_STACK_BYTES,
              "Supervisor stack buffer qua nho");
static_assert(sizeof(networkTaskStack) >= NETWORK_TASK_STACK_BYTES,
              "Network stack buffer qua nho");
static_assert(sizeof(mqttTaskStack) >= MQTT_TASK_STACK_BYTES,
              "MQTT stack buffer qua nho");
static_assert(sizeof(cloudTaskStack) >= CLOUD_TASK_STACK_BYTES,
              "Cloud stack buffer qua nho");
static_assert(sizeof(otaTaskStack) >= OTA_TASK_STACK_BYTES,
              "OTA stack buffer qua nho");

static void fatalRestart(const char *stage, esp_err_t error) {
  mayapLatchSystemTrip();
  mayapSafeOutputsEarly();
  mayapSerialPrintf(true, "[FATAL] %s err=%d -> RESTART\n",
                    stage ? stage : "SYSTEM", static_cast<int>(error));
  esp_restart();
  abort();
}

static void subscribeCurrentTaskToWdt(const char *name) {
  const esp_err_t result = esp_task_wdt_add(nullptr);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    fatalRestart(name, result);
  }
}

void controlTask(void *parameter) {
  (void)parameter;
  subscribeCurrentTaskToWdt("CTRL WDT ADD");
  TickType_t lastWake = xTaskGetTickCount();
#if MAYAP_DIAGNOSTIC_SERIAL
  uint32_t lastStackReportAt = millis();
#endif

  for (;;) {
    const uint32_t now = millis();
    const int64_t cycleStartedUs = esp_timer_get_time();
    Machine.update(now);
    const uint32_t cycleUs = static_cast<uint32_t>(
        std::min<int64_t>(UINT32_MAX, esp_timer_get_time() - cycleStartedUs));
    __atomic_store_n(&controlLastCycleUs, cycleUs, __ATOMIC_RELEASE);
    uint32_t previousMax = __atomic_load_n(&controlMaxCycleUs, __ATOMIC_ACQUIRE);
    while (cycleUs > previousMax &&
           !__atomic_compare_exchange_n(&controlMaxCycleUs, &previousMax, cycleUs,
                                        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
    uint8_t slowCount = __atomic_load_n(&controlTripCycleCount, __ATOMIC_ACQUIRE);
    if (cycleUs >= CONTROL_CYCLE_TRIP_US) {
      if (slowCount < UINT8_MAX) ++slowCount;
    } else {
      slowCount = 0U;
    }
    __atomic_store_n(&controlTripCycleCount, slowCount, __ATOMIC_RELEASE);
    __atomic_store_n(&controlHeartbeatMs, millis(), __ATOMIC_RELEASE);

#if MAYAP_DIAGNOSTIC_SERIAL
    if (elapsedMs(now, lastStackReportAt) >= TASK_STACK_MONITOR_MS) {
      lastStackReportAt = now;
      mayapSerialPrintf(false,
          "[TASK] stack ctrl=%u hmi=%u sup=%u net=%u mqtt=%u cloud=%u bytes ctrl=%lu/%luus hmi=%lu/%luus\n",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(controlTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(hmiTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(supervisorTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(networkTaskHandle)),
          mqttTaskHandle ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(mqttTaskHandle)) : 0U,
          cloudTaskHandle ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(cloudTaskHandle)) : 0U,
          static_cast<unsigned long>(__atomic_load_n(&controlLastCycleUs, __ATOMIC_ACQUIRE)),
          static_cast<unsigned long>(__atomic_load_n(&controlMaxCycleUs, __ATOMIC_ACQUIRE)),
          static_cast<unsigned long>(__atomic_load_n(&hmiLastCycleUs, __ATOMIC_ACQUIRE)),
          static_cast<unsigned long>(__atomic_load_n(&hmiMaxCycleUs, __ATOMIC_ACQUIRE)));
    }
#endif

    const esp_err_t result = esp_task_wdt_reset();
    if (result != ESP_OK) fatalRestart("CTRL WDT RESET", result);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
  }
}

void hmiTask(void *parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    const int64_t cycleStartedUs = esp_timer_get_time();
    hmiUpdate(now);
    const uint32_t cycleUs = static_cast<uint32_t>(
        std::min<int64_t>(UINT32_MAX, esp_timer_get_time() - cycleStartedUs));
    __atomic_store_n(&hmiLastCycleUs, cycleUs, __ATOMIC_RELEASE);
    uint32_t previousMax = __atomic_load_n(&hmiMaxCycleUs, __ATOMIC_ACQUIRE);
    while (cycleUs > previousMax &&
           !__atomic_compare_exchange_n(&hmiMaxCycleUs, &previousMax, cycleUs,
                                        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
    uint8_t slowCount = __atomic_load_n(&hmiTripCycleCount, __ATOMIC_ACQUIRE);
    if (cycleUs >= HMI_CYCLE_TRIP_US) {
      if (slowCount < UINT8_MAX) ++slowCount;
    } else {
      slowCount = 0U;
    }
    __atomic_store_n(&hmiTripCycleCount, slowCount, __ATOMIC_RELEASE);
    __atomic_store_n(&hmiHeartbeatMs, millis(), __ATOMIC_RELEASE);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(HMI_TASK_PERIOD_MS));
  }
}

#if MAYAP_DIAGNOSTIC_SERIAL
static const char *mqttStateText(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT: return "TIMEOUT";
    case MQTT_CONNECTION_LOST: return "CONNECTION_LOST";
    case MQTT_CONNECT_FAILED: return "TCP_TLS_CONNECT_FAILED";
    case MQTT_DISCONNECTED: return "DISCONNECTED";
    case MQTT_CONNECTED: return "CONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL: return "BAD_PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "BAD_CLIENT_ID";
    case MQTT_CONNECT_UNAVAILABLE: return "BROKER_UNAVAILABLE";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "BAD_CREDENTIALS";
    case MQTT_CONNECT_UNAUTHORIZED: return "UNAUTHORIZED";
    default: return "UNKNOWN";
  }
}
#endif

// PubSubClient mac dinh subscribe QoS0. Web gui command/config/reminder/history
// bang QoS1; broker giao theo min(pub,sub), nen neu khong nang subscription
// len QoS1 thi goi quan trong van thanh QoS0. Ham nay nang 4 kenh ghi ngay sau
// moi lan reconnect; subscribe lap lai cung topic la hop le va idempotent.
static void promoteMqttWriteSubscriptionsToQos1() {
  using namespace MayapRealtimeInternal;
  if (!mqtt.connected()) {
    mqttWriteQos1Promoted = false;
    return;
  }
  if (mqttWriteQos1Promoted) return;

  const bool ok = mqtt.subscribe(topicOf("config/set"), 1) &&
                  mqtt.subscribe(topicOf("reminders/set"), 1) &&
                  mqtt.subscribe(topicOf("command"), 1) &&
                  mqtt.subscribe(topicOf("history/request"), 1);
  if (ok) {
    mqttWriteQos1Promoted = true;
    mayapSerialPrintf(false, "[WEBLINK] kenh ghi da subscribe QoS1\n");
  }
}

// Task nay CHI quan ly STA/portal/NTP. Khi nguoi dung mo cong doi Wi-Fi, dung
// doi mode radio neu MQTT/Cloud dang o giua mot I/O blocking; hai task kia se
// thay request portal va tu dong quiesce, sau do networkTask moi cho portal di.
void networkTask(void *parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    const bool portalRequested = mayapWifiPortalExclusiveRequested();
    const bool externalIoBusy =
        __atomic_load_n(&mqttIoBusy, __ATOMIC_ACQUIRE) != 0U ||
        __atomic_load_n(&cloudIoBusy, __ATOMIC_ACQUIRE) != 0U;
    if (!(portalRequested && externalIoBusy)) {
      mayapNetworkUpdate(now);
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(NETWORK_FAST_TASK_PERIOD_MS));
  }
}

// Task realtime doc lap: Cloud HTTPS/NTP/portal web co block bao lau cung
// khong lam mqtt.loop() doi. PubSubClient + WiFiClientSecure chi duoc cham boi
// task nay, khong co truy cap dong thoi tu task khac.
void mqttTask(void *parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();
#if MAYAP_DIAGNOSTIC_SERIAL
  uint32_t lastMqttDiagAt = 0U;
#endif
  for (;;) {
    const uint32_t now = millis();

    if (mayapWifiPortalExclusiveRequested()) {
      __atomic_store_n(&mqttIoBusy, 1U, __ATOMIC_RELEASE);
      if (MayapRealtimeInternal::mqtt.connected()) MayapRealtimeInternal::mqtt.disconnect();
      MayapRealtimeInternal::netClient.stop();
      mqttWriteQos1Promoted = false;
      __atomic_store_n(&mqttIoBusy, 0U, __ATOMIC_RELEASE);
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(MQTT_TASK_PERIOD_MS));
      continue;
    }

    __atomic_store_n(&mqttIoBusy, 1U, __ATOMIC_RELEASE);
    // Dong cua so race: portal co the vua duoc controlTask yeu cau sau phep
    // kiem tra o tren nhung truoc khi ta danh dau busy.
    if (!mayapWifiPortalExclusiveRequested()) {
      mayapWebLinkUpdate(now);
      promoteMqttWriteSubscriptionsToQos1();
#if MAYAP_DIAGNOSTIC_SERIAL
      if (lastMqttDiagAt == 0U || elapsedMs(now, lastMqttDiagAt) >= 5000UL) {
        lastMqttDiagAt = now;
        const NetworkStatus netStatus = mayapGetNetworkStatus();
        const int mqttState = MayapRealtimeInternal::mqtt.state();
        const uint32_t retryInMs = MayapRealtimeInternal::mqttBackoff.ready(now)
            ? 0U
            : static_cast<uint32_t>(MayapRealtimeInternal::mqttBackoff.nextAttemptAt - now);
        Serial.printf(
            "[MQTT-DIAG] wifi=%u rssi=%d client=%s host=%s port=%u tls=%u/%u "
            "user=%s pass=%s mqtt=%u state=%d(%s) tcp=%u backoff=%u retry=%lums heap=%u\n",
            netStatus.connected ? 1U : 0U,
            netStatus.connected ? WiFi.RSSI() : 0,
            MayapRealtimeInternal::deviceId,
            MQTT_BROKER_HOST,
            static_cast<unsigned>(MQTT_BROKER_PORT),
            MQTT_USE_TLS ? 1U : 0U,
            MayapRealtimeInternal::mqttTlsReady ? 1U : 0U,
            MQTT_USERNAME[0] ? MQTT_USERNAME : "<EMPTY>",
            MQTT_PASSWORD[0] ? "SET" : "EMPTY",
            MayapRealtimeInternal::mqtt.connected() ? 1U : 0U,
            mqttState,
            mqttStateText(mqttState),
            MayapRealtimeInternal::netClient.connected() ? 1U : 0U,
            static_cast<unsigned>(MayapRealtimeInternal::mqttBackoff.step),
            static_cast<unsigned long>(retryInMs),
            static_cast<unsigned>(ESP.getFreeHeap()));
      }
#endif
    }
    __atomic_store_n(&mqttIoBusy, 0U, __ATOMIC_RELEASE);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(MQTT_TASK_PERIOD_MS));
  }
}

// HTTPS Cloudflare co the block vai giay luc TLS/HTTP cham. Cho no mot task
// rieng de canh bao van hoat dong day du nhung realtime MQTT khong bi dong bang.
void cloudTask(void *parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    if (mayapWifiPortalExclusiveRequested()) {
      __atomic_store_n(&cloudIoBusy, 0U, __ATOMIC_RELEASE);
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CLOUD_TASK_PERIOD_MS));
      continue;
    }

    __atomic_store_n(&cloudIoBusy, 1U, __ATOMIC_RELEASE);
    if (!mayapWifiPortalExclusiveRequested()) {
      mayapCloudAlertUpdate(now);
    }
    __atomic_store_n(&cloudIoBusy, 0U, __ATOMIC_RELEASE);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CLOUD_TASK_PERIOD_MS));
  }
}

void otaTask(void *parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    if (mayapWifiPortalExclusiveRequested()) {
      const bool quiesced = mayapOtaQuiesceForWifiPortal();
      mayapSetWifiPortalOtaQuiesced(quiesced);
      if (!quiesced) mayapOtaUpdate(now);
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(OTA_TASK_PERIOD_MS));
      continue;
    }
    mayapSetWifiPortalOtaQuiesced(false);
    mayapOtaUpdate(now);
    mayapFirmwareWebUpdate(now);
    mayapFirmwareRollbackUpdate(now);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(OTA_TASK_PERIOD_MS));
  }
}

void supervisorTask(void *parameter) {
  (void)parameter;
  subscribeCurrentTaskToWdt("SUP WDT ADD");
  TickType_t lastWake = xTaskGetTickCount();
  bool previousHmiHealthy = true;

  for (;;) {
    const uint32_t now = millis();
    const uint32_t ctrlBeat = __atomic_load_n(&controlHeartbeatMs, __ATOMIC_ACQUIRE);
    const uint32_t hmiBeat = __atomic_load_n(&hmiHeartbeatMs, __ATOMIC_ACQUIRE);

    const bool controlHealthy = ctrlBeat != 0U &&
        elapsedMs(now, ctrlBeat) <= CONTROL_HEARTBEAT_TIMEOUT_MS;
    const bool hmiHealthy = hmiBeat != 0U &&
        elapsedMs(now, hmiBeat) <= HMI_HEARTBEAT_TIMEOUT_MS;
    const uint8_t hmiSlowCycles = __atomic_load_n(&hmiTripCycleCount, __ATOMIC_ACQUIRE);
    const bool hmiFatal = hmiBeat != 0U &&
        (elapsedMs(now, hmiBeat) >= HMI_FATAL_HEARTBEAT_TIMEOUT_MS ||
         hmiSlowCycles >= HMI_CYCLE_TRIP_COUNT);

    const uint8_t slowCycles = __atomic_load_n(&controlTripCycleCount, __ATOMIC_ACQUIRE);
    const bool deadlineTrip = slowCycles >= CONTROL_CYCLE_TRIP_COUNT;

    if (!controlHealthy || deadlineTrip) {
      mayapLatchSystemTrip();
      if (controlTaskHandle) vTaskSuspend(controlTaskHandle);
      mayapSafeOutputsEarly();
      mayapSerialPrintf(true,
          "[SUPERVISOR] TRIP reason=%s cycle=%luus count=%u\n",
          !controlHealthy ? "HEARTBEAT" : "DEADLINE",
          static_cast<unsigned long>(__atomic_load_n(&controlLastCycleUs, __ATOMIC_ACQUIRE)),
          static_cast<unsigned>(slowCycles));
      const uint32_t tripAt = now;
      while (elapsedMs(millis(), tripAt) < SUPERVISOR_RESTART_FALLBACK_MS) {
        mayapSafeOutputsEarly();
        vTaskDelay(pdMS_TO_TICKS(20));
      }
      esp_restart();
      abort();
    }

    if (hmiBeat != 0U && hmiHealthy != previousHmiHealthy) {
      previousHmiHealthy = hmiHealthy;
      mayapSerialPrintf(false, "[SUPERVISOR] HMI %s cycle=%luus slow=%u\n",
                        hmiHealthy ? "RECOVERED" : "HEARTBEAT SLOW",
                        static_cast<unsigned long>(__atomic_load_n(&hmiLastCycleUs, __ATOMIC_ACQUIRE)),
                        static_cast<unsigned>(hmiSlowCycles));
    }

    if (hmiFatal) {
      mayapLatchSystemTrip();
      if (controlTaskHandle) vTaskSuspend(controlTaskHandle);
      if (hmiTaskHandle) vTaskSuspend(hmiTaskHandle);
      mayapSafeOutputsEarly();
      mayapSerialPrintf(true,
          "[SUPERVISOR] HMI FATAL heartbeatAge=%lums cycle=%luus slow=%u -> RESTART\n",
          static_cast<unsigned long>(elapsedMs(now, hmiBeat)),
          static_cast<unsigned long>(__atomic_load_n(&hmiLastCycleUs, __ATOMIC_ACQUIRE)),
          static_cast<unsigned>(hmiSlowCycles));
      esp_restart();
      abort();
    }

    if (Machine.healthRestartRequested()) {
      mayapLatchSystemTrip();
      if (controlTaskHandle) vTaskSuspend(controlTaskHandle);
      mayapSafeOutputsEarly();
      mayapSerialPrintf(true, "[SUPERVISOR] Health-monitor xin khoi dong lai co kiem soat\n");
      esp_restart();
      abort();
    }

    const esp_err_t result = esp_task_wdt_reset();
    if (result != ESP_OK) fatalRestart("SUP WDT RESET", result);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SUPERVISOR_TASK_PERIOD_MS));
  }
}

void setup() {
  mayapSafeOutputsEarly();
  Serial.begin(115200);

  i2cMutex = xSemaphoreCreateMutexStatic(&i2cMutexBuffer);
  if (!i2cMutex) fatalRestart("I2C MUTEX", ESP_ERR_NO_MEM);

  if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ)) {
    fatalRestart("I2C BEGIN", ESP_FAIL);
  }
  Wire.setTimeOut(I2C_TIMEOUT_MS);
  hmiSetI2cLockCallbacks(mayapI2cLock, mayapI2cUnlock);

  mayapDeviceIdentityBegin();
  mayapNetworkBegin();
  mayapOtaBegin();
  mayapOtaRollbackBegin();
  mayapWebLinkBegin();
  // PubSubClient mac dinh keepalive/socket timeout 15s. Realtime task rieng
  // cho phep dat keepalive 30s de du bien mang, nhung timeout I/O ngan 5s de
  // ket noi chet tu phuc hoi som. TLS handshake cung chan 8s thay vi 120s mac
  // dinh cua NetworkClientSecure.
  MayapRealtimeInternal::mqtt.setKeepAlive(30);
  MayapRealtimeInternal::mqtt.setSocketTimeout(5);
#if MAYAP_MQTT_USE_TLS
  MayapRealtimeInternal::netClient.setConnectionTimeout(5000);
  MayapRealtimeInternal::netClient.setHandshakeTimeout(8);
#endif
  mayapCloudAlertBegin();

  esp_task_wdt_config_t wdtConfig{};
  wdtConfig.timeout_ms = CONTROL_WDT_TIMEOUT_MS;
  wdtConfig.idle_core_mask = 0U;
  wdtConfig.trigger_panic = true;
  esp_err_t result = esp_task_wdt_reconfigure(&wdtConfig);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_init(&wdtConfig);
  if (result != ESP_OK) fatalRestart("WDT INIT", result);

  Machine.begin();
  mayapPrintNetworkConfig();
  hmiSetConfig(Machine.config());
  hmiSetRuntime(Machine.runtime());
  mayapWebSetConfig(Machine.config());
  mayapWebSetRuntime(Machine.runtime());
  mayapCloudSetRuntime(Machine.runtime());
  hmiBegin();

  const uint32_t now = millis();
  __atomic_store_n(&controlHeartbeatMs, now, __ATOMIC_RELEASE);
  __atomic_store_n(&hmiHeartbeatMs, now, __ATOMIC_RELEASE);

  controlTaskHandle = xTaskCreateStaticPinnedToCore(
      controlTask, "mayap_ctrl", sizeof(controlTaskStack), nullptr, 5,
      controlTaskStack, &controlTaskTcb, 1);

  supervisorTaskHandle = xTaskCreateStaticPinnedToCore(
      supervisorTask, "mayap_supervisor", sizeof(supervisorTaskStack), nullptr, 6,
      supervisorTaskStack, &supervisorTaskTcb, 1);

  hmiTaskHandle = xTaskCreateStaticPinnedToCore(
      hmiTask, "mayap_hmi", sizeof(hmiTaskStack), nullptr, 2,
      hmiTaskStack, &hmiTaskTcb, 0);

  networkTaskHandle = xTaskCreateStaticPinnedToCore(
      networkTask, "mayap_network", sizeof(networkTaskStack), nullptr, 1,
      networkTaskStack, &networkTaskTcb, 0);

  mqttTaskHandle = xTaskCreateStaticPinnedToCore(
      mqttTask, "mayap_mqtt", sizeof(mqttTaskStack), nullptr, 2,
      mqttTaskStack, &mqttTaskTcb, 0);

  cloudTaskHandle = xTaskCreateStaticPinnedToCore(
      cloudTask, "mayap_cloud", sizeof(cloudTaskStack), nullptr, 1,
      cloudTaskStack, &cloudTaskTcb, 0);

  otaTaskHandle = xTaskCreateStaticPinnedToCore(
      otaTask, "mayap_ota", sizeof(otaTaskStack), nullptr, 1,
      otaTaskStack, &otaTaskTcb, 0);

  if (!controlTaskHandle || !hmiTaskHandle || !supervisorTaskHandle ||
      !networkTaskHandle || !mqttTaskHandle || !cloudTaskHandle || !otaTaskHandle) {
    fatalRestart("TASK CREATE", ESP_ERR_NO_MEM);
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
