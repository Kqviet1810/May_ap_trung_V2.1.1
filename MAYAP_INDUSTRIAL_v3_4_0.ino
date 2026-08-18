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

#include "network_service.h"
#include "hmi.h"
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

static volatile uint32_t controlHeartbeatMs = 0U;
static volatile uint32_t hmiHeartbeatMs = 0U;
static volatile uint32_t controlLastCycleUs = 0U;
static volatile uint32_t controlMaxCycleUs = 0U;
static volatile uint8_t controlTripCycleCount = 0U;

static_assert(sizeof(controlTaskStack) >= CONTROL_TASK_STACK_BYTES,
              "Control stack buffer qua nho");
static_assert(sizeof(hmiTaskStack) >= HMI_TASK_STACK_BYTES,
              "HMI stack buffer qua nho");
static_assert(sizeof(supervisorTaskStack) >= SUPERVISOR_TASK_STACK_BYTES,
              "Supervisor stack buffer qua nho");
static_assert(sizeof(networkTaskStack) >= NETWORK_TASK_STACK_BYTES,
              "Network stack buffer qua nho");

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
          "[TASK] stack ctrl=%u hmi=%u sup=%u net=%u bytes cycle=%luus max=%luus\n",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(controlTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(hmiTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(supervisorTaskHandle)),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(networkTaskHandle)),
          static_cast<unsigned long>(__atomic_load_n(&controlLastCycleUs, __ATOMIC_ACQUIRE)),
          static_cast<unsigned long>(__atomic_load_n(&controlMaxCycleUs, __ATOMIC_ACQUIRE)));
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
    hmiUpdate(now);
    __atomic_store_n(&hmiHeartbeatMs, now, __ATOMIC_RELEASE);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(HMI_TASK_PERIOD_MS));
  }
}

void networkTask(void *parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    mayapNetworkUpdate(millis());
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(NETWORK_TASK_PERIOD_MS));
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
      mayapSerialPrintf(false, "[SUPERVISOR] HMI %s\n",
                        hmiHealthy ? "RECOVERED" : "HEARTBEAT SLOW");
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

  // Dam bao radio tat truoc khi nap cau hinh EEPROM. Chi networkTask moi
  // duoc phep khoi dong Wi-Fi neu nguoi dung da chon ONLINE.
  mayapNetworkBegin();

  esp_task_wdt_config_t wdtConfig{};
  wdtConfig.timeout_ms = CONTROL_WDT_TIMEOUT_MS;
  wdtConfig.idle_core_mask = 0U;
  wdtConfig.trigger_panic = true;
  esp_err_t result = esp_task_wdt_reconfigure(&wdtConfig);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_init(&wdtConfig);
  if (result != ESP_OK) fatalRestart("WDT INIT", result);

  Machine.begin();
  hmiSetConfig(Machine.config());
  hmiSetRuntime(Machine.runtime());
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

  if (!controlTaskHandle || !hmiTaskHandle || !supervisorTaskHandle ||
      !networkTaskHandle) {
    fatalRestart("TASK CREATE", ESP_ERR_NO_MEM);
  }
}

void loop() {
  // loopTask khong tham gia dieu khien; chi nghi theo chu ky huu han.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
