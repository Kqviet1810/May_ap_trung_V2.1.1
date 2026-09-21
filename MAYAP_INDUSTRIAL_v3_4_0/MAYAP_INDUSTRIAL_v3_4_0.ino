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
// realtime_link.h dung #if de chon WiFiClientSecure. MQTT_USE_TLS trong
// config.h la constexpr (C++), preprocessor khong nhin thay constexpr va
// coi #if MQTT_USE_TLS la 0. Dua macro cau hinh that vao chi trong luc include
// file nay, sau do undef de phan code con lai van dung constexpr nhu cu.
#define MQTT_USE_TLS MAYAP_MQTT_USE_TLS
#include "realtime_link.h"
#undef MQTT_USE_TLS
#include "cloud_alert_link.h"
#include "attiny_bus.h"
#include "machine_control.h"

using namespace Mayap;

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

static StaticTask_t otaTaskTcb;
static StackType_t otaTaskStack[
    (OTA_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t otaTaskHandle = nullptr;

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
          "[TASK] stack ctrl=%u hmi=%u sup=%u net=%u bytes ctrl=%lu/%luus hmi=%lu/%luus\n",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(controlTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(hmiTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(supervisorTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(networkTaskHandle)),
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
    // Ghi heartbeat SAU khi hmiUpdate tra ve va dung millis() moi nhat; neu
    // I2C/HMI bi block thi supervisor se thay stale dung thoi gian thuc.
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

void networkTask(void *parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();
#if MAYAP_DIAGNOSTIC_SERIAL
  uint32_t lastMqttDiagAt = 0U;
#endif
  for (;;) {
    const uint32_t now = millis();
    mayapNetworkUpdate(now);
    // Lop web realtime (MQTT) va lop Cloud Push chi duoc phep hoat dong tren
    // networkTask (I/O mang); xem ghi chu dau realtime_link.h/cloud_alert_link.h.
    // Hai lop nay hoan toan doc lap voi nhau - mot ben loi khong lam hong ben kia.
    mayapWebLinkUpdate(now);
#if MAYAP_DIAGNOSTIC_SERIAL
    if (lastMqttDiagAt == 0U || elapsedMs(now, lastMqttDiagAt) >= 5000UL) {
      lastMqttDiagAt = now;
      const NetworkStatus netStatus = mayapGetNetworkStatus();
      const int mqttState = MayapRealtimeInternal::mqtt.state();
      const uint32_t retryInMs = MayapRealtimeInternal::mqttBackoff.ready(now)
          ? 0U
          : static_cast<uint32_t>(MayapRealtimeInternal::mqttBackoff.nextAttemptAt - now);
      // Dung Serial.printf truc tiep: dong chan doan MQTT KHONG bi bo neu
      // buffer cua mayapSerialPrintf dang day. Khong bao gio in password that.
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
    mayapCloudAlertUpdate(now);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(NETWORK_TASK_PERIOD_MS));
  }
}

void otaTask(void *parameter) {
  (void)parameter;
  // Task RIENG, KHONG dung chung networkTask - xem giai thich day du tai
  // dinh nghia OTA_TASK_PERIOD_MS trong config.h. Nhip nhanh (30ms) de
  // ArduinoOTA.handle() luon san sang phan hoi dung gio bat ke networkTask
  // dang blocking bao lau boi HTTP(S)/MQTT.
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    if (mayapWifiPortalExclusiveRequested()) {
      const bool quiesced = mayapOtaQuiesceForWifiPortal();
      mayapSetWifiPortalOtaQuiesced(quiesced);
      if (!quiesced) {
        // ArduinoOTA dang ghi: tiep tuc handle cho den onEnd/onError; portal
        // KHONG duoc ha radio trong thoi gian nay.
        mayapOtaUpdate(now);
      }
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(OTA_TASK_PERIOD_MS));
      continue;
    }
    mayapSetWifiPortalOtaQuiesced(false);
    mayapOtaUpdate(now);
    // Cap nhat firmware TU XA qua Cloudflare (ota_web_update.h) - cung task
    // vi ca hai deu la "dang ghi flash", tu nhien loai tru lan nhau.
    mayapFirmwareWebUpdate(now);
    // Quay lai firmware truoc do (ota_rollback.h) - cung task voi 2 thao tac
    // tren vi day cung la thao tac lien quan flash/khoi dong, tu nhien loai
    // tru lan nhau giong het 2 dong tren.
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

    const uint8_t slowCycles = __atomic_load_n(
        &controlTripCycleCount, __ATOMIC_ACQUIRE);
    const bool deadlineTrip = slowCycles >= CONTROL_CYCLE_TRIP_COUNT;

    if (!controlHealthy || deadlineTrip) {
      // Dat latch truoc khi suspend: neu Control vua tinh day, OutputArbiter
      // cung chi chap nhan trang thai an toan. Chi reset chip moi xoa latch.
      mayapLatchSystemTrip();
      if (controlTaskHandle) vTaskSuspend(controlTaskHandle);
      mayapSafeOutputsEarly();
      mayapSerialPrintf(true,
          "[SUPERVISOR] TRIP reason=%s cycle=%luus count=%u\n",
          !controlHealthy ? "HEARTBEAT" : "DEADLINE",
          static_cast<unsigned long>(__atomic_load_n(
              &controlLastCycleUs, __ATOMIC_ACQUIRE)),
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
      // HMI chet that su khong duoc de may chay vo han ma nguoi van hanh mat
      // quyen quan sat/thao tac. Cat output an toan truoc, roi software reset;
      // neu dang co me, co che EEPROM/RTC hien co se phuc hoi theo policy reset.
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

    // Giam sat suc khoe he thong (v3.6.0, xem serviceHealthMonitor() trong
    // machine_control.h): controlTask da tu quyet dinh CO NEN va KHI NAO AN
    // TOAN de khoi dong lai (RAM can kiet dan) - supervisorTask (noi duy nhat
    // duoc phep goi esp_restart() ngoai fatalRestart()) chi THUC THI quyet
    // dinh do. Dua ra ngoai vong an toan cua Output an toan truoc khi restart,
    // giong het duong TRIP o tren.
    if (Machine.healthRestartRequested()) {
      // F-14 (audit truoc phat hanh v3.7.1): duong TRIP o tren dat latch +
      // suspend controlTask TRUOC khi ep an toan ngo ra, de controlTask
      // khong the nao con chay va ghi de lai relay giua luc dang restart.
      // Nhanh nay truoc day BO QUA ca 2 buoc do - them vao cho dong bo, du
      // cua so rui ro thuc te rat ngan (2 task cung ghim core 1, supervisor
      // uu tien cao hon controlTask nen binh thuong khong the bi chen ngang,
      // chi co the xay ra dung luc mayapSerialPrintf() (blocking) nhuong CPU).
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

  // Nap khoa rieng/PIN tu NVS truoc khi bat ky kenh cloud nao khoi dong.
  mayapDeviceIdentityBegin();

  // Dam bao radio tat truoc khi nap cau hinh EEPROM. Chi networkTask moi
  // duoc phep khoi dong Wi-Fi neu nguoi dung da chon ONLINE.
  mayapNetworkBegin();
  mayapOtaBegin();
  mayapOtaRollbackBegin();
  mayapWebLinkBegin();
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

  // Cung core 0 voi hmi/network (core 1 danh rieng cho dieu khien an toan) -
  // xem giai thich tai OTA_TASK_PERIOD_MS/otaTask ve ly do can task rieng.
  otaTaskHandle = xTaskCreateStaticPinnedToCore(
      otaTask, "mayap_ota", sizeof(otaTaskStack), nullptr, 1,
      otaTaskStack, &otaTaskTcb, 0);

  if (!controlTaskHandle || !hmiTaskHandle || !supervisorTaskHandle ||
      !otaTaskHandle ||
      !networkTaskHandle) {
    fatalRestart("TASK CREATE", ESP_ERR_NO_MEM);
  }
}

void loop() {
  // loopTask khong tham gia dieu khien; chi nghi theo chu ky huu han.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
