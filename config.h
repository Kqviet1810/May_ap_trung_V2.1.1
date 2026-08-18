#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

// ============================================================================
// MAY AP TRUNG INDUSTRIAL v3.4.0 - CAU HINH DUY NHAT CAN SUA
// MCU: ESP32-S3-WROOM-1U-N8 (8 MB Quad Flash, khong PSRAM)
// ============================================================================

constexpr char MAYAP_FIRMWARE_VERSION[] = "3.4.0";
constexpr char MAYAP_HARDWARE_REVISION[] = "CTRL-S3-N8-R1";
constexpr char HMI_FIRMWARE_VERSION[] = "3.6.0";
constexpr char HMI_HARDWARE_REVISION[] = "HMI-S3-R2";

// ----------------------------- BUILD -----------------------------------------
// 1: mo phong 8 input bang Serial. 0: doc input 12 V that qua opto.
#ifndef MAYAP_SERIAL_INPUT_SIM
#define MAYAP_SERIAL_INPUT_SIM 0
#endif

// Log chan doan khong tham gia dieu khien. Nen de 0 o ban giao thuong mai.
#ifndef MAYAP_DIAGNOSTIC_SERIAL
#define MAYAP_DIAGNOSTIC_SERIAL 1
#endif

// Thong tin Wi-Fi chi duoc dung khi nguoi van hanh chon ONLINE tren HMI.
// De trong SSID de vo hieu hoa ket noi mang ngay ca khi vo tinh chon ONLINE.
// Co the thay hai macro nay bang build flags de khong ghi mat khau vao source.
#ifndef MAYAP_WIFI_SSID
#define MAYAP_WIFI_SSID ""
#endif
#ifndef MAYAP_WIFI_PASSWORD
#define MAYAP_WIFI_PASSWORD ""
#endif
constexpr char NETWORK_WIFI_SSID[] = MAYAP_WIFI_SSID;
constexpr char NETWORK_WIFI_PASSWORD[] = MAYAP_WIFI_PASSWORD;
constexpr char NETWORK_WIFI_HOSTNAME[] = "mayap-industrial";
static_assert(sizeof(NETWORK_WIFI_SSID) <= 33U,
              "Wi-Fi SSID toi da 32 ky tu");
static_assert(sizeof(NETWORK_WIFI_PASSWORD) <= 64U,
              "Wi-Fi password toi da 63 ky tu");
static_assert(sizeof(NETWORK_WIFI_HOSTNAME) <= 33U,
              "Wi-Fi hostname toi da 32 ky tu");

// HMI chi duoc bien dich trong firmware tong; da loai bo demo doc lap.
#define MAYAP_HMI_OWNS_I2C_BUS 0
#define MAYAP_HMI_ENCODER_INTERRUPT 1

// ----------------------------- GPIO ------------------------------------------
// Output HIGH = ON.
constexpr uint8_t PIN_OUT_HEATER_SSR   = 1;   // KAO3400 - SSR thanh nhiet
constexpr uint8_t PIN_OUT_PULSE_SPARE  = 2;   // KAO3400 du phong
// Pinmap thuc te v3.2.8:
// - Doi cap relay dao trai/phai voi cap quat tuan hoan/contactor nhiet.
// - Ghep theo thu tu yeu cau: TRAI <-> QUAT, PHAI <-> NHIET.
constexpr uint8_t PIN_OUT_CIRC_FAN     = 10;  // truoc day: DAO TRAI
constexpr uint8_t PIN_OUT_HEAT_MASTER  = 11;  // truoc day: DAO PHAI
constexpr uint8_t PIN_OUT_LIGHT        = 12;
constexpr uint8_t PIN_OUT_VENT_FAN     = 13;
constexpr uint8_t PIN_OUT_TURN_RIGHT   = 14;  // truoc day: NHIET TONG
constexpr uint8_t PIN_OUT_TURN_LEFT    = 21;  // truoc day: QUAT TUAN HOAN
constexpr uint8_t PIN_OUT_SIREN        = 47;
constexpr uint8_t PIN_OUT_RELAY_SPARE  = 48;
constexpr uint8_t PIN_STATUS_RGB       = 42;  // SK6812MINI-C

// Input opto ACTIVE-LOW: kich 12 V => ngo ra opto keo GPIO xuong GND.
constexpr uint8_t PIN_IN_LIMIT_LEFT    = 4;
constexpr uint8_t PIN_IN_LIMIT_RIGHT   = 5;
constexpr uint8_t PIN_IN_AUTO          = 6;
constexpr uint8_t PIN_IN_HEATER_ENABLE = 7;
constexpr uint8_t PIN_IN_CIRC_FAN      = 15;
constexpr uint8_t PIN_IN_LIGHT         = 16;
constexpr uint8_t PIN_IN_TURN_LEFT     = 17;
constexpr uint8_t PIN_IN_TURN_RIGHT    = 18;  // da doi tu GPIO8 de tranh SDA

constexpr bool INPUT_ACTIVE_LOW = true;
constexpr bool OUTPUT_ACTIVE_HIGH = true;

// MAX3485 / Modbus RTU.
constexpr uint8_t SHT_UART_PORT = 1;
constexpr uint8_t PIN_RS485_RX = 37;     // RO
constexpr uint8_t PIN_RS485_DE_RE = 36;  // DE + /RE
constexpr uint8_t PIN_RS485_TX = 35;     // DI

// HMI ST7567 + rotary + coi phu duy nhat. GPIO41 bao phim, trang thai, dao va loi.
// GPIO47 chi danh cho coi lon qua nhiet cap cao nhat.
constexpr uint8_t LCD_I2C_ADDRESS = 0x3F;
constexpr uint8_t PIN_I2C_SDA = 8;
constexpr uint8_t PIN_I2C_SCL = 9;
constexpr uint8_t PIN_ENCODER_CLK = 38;
constexpr uint8_t PIN_ENCODER_DT  = 39;
constexpr uint8_t PIN_ENCODER_SW  = 40;
constexpr uint8_t PIN_BUZZER      = 41;
constexpr bool BUZZER_ACTIVE_HIGH = true;

// Kiem tra toan bo GPIO tai compile-time.
constexpr uint8_t MAYAP_USED_PINS[] = {
  PIN_OUT_HEATER_SSR, PIN_OUT_PULSE_SPARE, PIN_OUT_TURN_RIGHT,
  PIN_OUT_TURN_LEFT, PIN_OUT_VENT_FAN, PIN_OUT_LIGHT,
  PIN_OUT_HEAT_MASTER, PIN_OUT_CIRC_FAN, PIN_OUT_SIREN,
  PIN_OUT_RELAY_SPARE, PIN_STATUS_RGB,
  PIN_IN_LIMIT_LEFT, PIN_IN_LIMIT_RIGHT, PIN_IN_AUTO,
  PIN_IN_HEATER_ENABLE, PIN_IN_CIRC_FAN, PIN_IN_LIGHT,
  PIN_IN_TURN_LEFT, PIN_IN_TURN_RIGHT,
  PIN_RS485_RX, PIN_RS485_DE_RE, PIN_RS485_TX,
  PIN_I2C_SDA, PIN_I2C_SCL, PIN_ENCODER_CLK, PIN_ENCODER_DT,
  PIN_ENCODER_SW, PIN_BUZZER
};

constexpr bool mayapPinsValidAndUnique() {
  constexpr size_t count = sizeof(MAYAP_USED_PINS) / sizeof(MAYAP_USED_PINS[0]);
  for (size_t i = 0; i < count; ++i) {
    if (MAYAP_USED_PINS[i] > 48U) return false;
    for (size_t j = i + 1; j < count; ++j) {
      if (MAYAP_USED_PINS[i] == MAYAP_USED_PINS[j]) return false;
    }
  }
  return true;
}
static_assert(mayapPinsValidAndUnique(), "MAYAP: GPIO trung nhau/ngoai pham vi");

// ----------------------------- HMI -------------------------------------------
constexpr uint8_t HMI_MAX_VALID_GPIO = 48;
constexpr uint8_t HMI_USED_PINS[] = {
  PIN_I2C_SDA, PIN_I2C_SCL, PIN_ENCODER_CLK, PIN_ENCODER_DT,
  PIN_ENCODER_SW, PIN_BUZZER
};
constexpr bool hmiPinsAreValidAndUnique() {
  constexpr size_t count = sizeof(HMI_USED_PINS) / sizeof(HMI_USED_PINS[0]);
  for (size_t i = 0; i < count; ++i) {
    if (HMI_USED_PINS[i] > HMI_MAX_VALID_GPIO) return false;
    for (size_t j = i + 1; j < count; ++j) {
      if (HMI_USED_PINS[i] == HMI_USED_PINS[j]) return false;
    }
  }
  return true;
}
static_assert(hmiPinsAreValidAndUnique(), "HMI: GPIO trung nhau/ngoai pham vi");

constexpr uint32_t I2C_CLOCK_HZ = 100000UL;
constexpr uint16_t I2C_TIMEOUT_MS = 25;          // timeout phan cung moi giao dich
constexpr uint16_t I2C_STORAGE_LOCK_TIMEOUT_MS = 120; // doi LCD full-buffer toi da co gioi han
constexpr uint8_t DEFAULT_CONTRAST = 230;
constexpr bool REVERSE_ENCODER = false;
constexpr uint32_t LCD_RETRY_INTERVAL_MS = 3000UL;
constexpr uint32_t LCD_HEALTH_CHECK_MS = 5000UL;
constexpr uint32_t LCD_FAULT_LOG_INTERVAL_MS = 30000UL;
#ifndef LCD_PROFILE
#define LCD_PROFILE 1
#endif
constexpr uint8_t ENCODER_STEPS_PER_DETENT = 4;
constexpr uint8_t ENCODER_MAX_STEPS_PER_UPDATE = 3;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30UL;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 900UL;
constexpr uint32_t DISPLAY_MIN_DRAW_MS = 110UL;
constexpr uint32_t HOME_REFRESH_MS = 5000UL;
constexpr uint32_t ALARM_REFRESH_MS = 5000UL;
constexpr uint32_t HMI_COMMAND_POLL_MS = 100UL;
constexpr uint32_t MENU_IDLE_TIMEOUT_MS = 60000UL;
constexpr uint32_t SAVE_CONFIRM_TIMEOUT_MS = 8000UL;
constexpr uint32_t COMMAND_CONFIRM_TIMEOUT_MS = 8000UL;
constexpr uint16_t COMMAND_DEFAULT_VALID_MS = 5000U;
constexpr uint16_t COMMAND_AUTOTUNE_VALID_MS = 5000U;
constexpr uint32_t TOAST_INFO_MS = 2200UL;
constexpr uint32_t TOAST_ERROR_MS = 4200UL;
// Thong bao HMI chi chiem thanh trang thai 9 px o day, khong che noi dung.
constexpr uint8_t HMI_STATUS_TEXT_MAX_CHARS = 22U;
constexpr uint32_t HMI_INPUT_GUARD_MS = 140UL;
// Gui lai cung mot framebuffer them mot lan sau khi doi trang. Khong blank,
// giup tu sua neu mot frame I2C bi nhieu/khong tron ven.
constexpr uint32_t HMI_VERIFY_REDRAW_DELAY_MS = 220UL;
// Coi dao: hai bip ngan moi chu ky, khong keu lien tuc.
constexpr uint32_t TURN_BUZZER_ON1_MS = 90UL;
constexpr uint32_t TURN_BUZZER_GAP_MS = 120UL;
constexpr uint32_t TURN_BUZZER_ON2_MS = 90UL;
constexpr uint32_t TURN_BUZZER_PAUSE_MS = 1700UL;
constexpr uint8_t COMMAND_QUEUE_SIZE = 4;
constexpr uint8_t COMMAND_ACK_QUEUE_SIZE = 4;
constexpr uint32_t EMERGENCY_RESOUND_MS = 60000UL;
constexpr uint32_t CRITICAL_RESOUND_MS = 300000UL;
// AUTO bi tat giua me: chi bao sau 2 phut, sau ACK neu van OFF thi keu lai 10 phut/lan.
constexpr uint32_t AUTO_LOST_ALARM_DELAY_MS = 120000UL;
constexpr uint32_t AUTO_LOST_RESOUND_MS = 600000UL;
// Nhac nguoi van hanh xac nhan phuc hoi me sau mat dien. Bat buoc keu,
// khong bi vo hieu boi tuy chon tat coi nhac nho.
constexpr uint32_t RESUME_PROMPT_ON_MS = 220UL;
constexpr uint32_t RESUME_PROMPT_OFF_MS = 780UL;

// --------------------- CAU HINH LOGIC DE CHINH -------------------------------
// Huong tim goc khi khay nam giua. true = trai; false = phai.
constexpr bool HOME_TO_LEFT = true;

// Quyen cho phep nhiet ngoai me nam trong MachineConfig va chinh tren LCD.
// Mac dinh false; du bat van bat buoc quat tuan hoan da chay on dinh.
// Cong tac nhiet la dieu kien bat buoc cua mot me. Khong cho phep tat
// trong ban thuong mai vi me co the van dem thoi gian nhung khong duoc gia nhiet.
constexpr bool REQUIRE_HEATER_ENABLE_TO_START = true;

// MANUAL chi trao quyen quat tuan hoan va dao cho cong tac cung.
// Tat quat trong MANUAL se khoa ca SSR va contactor tong nhiet.
constexpr bool MANUAL_FAN_CAN_DISABLE_HEATING = true;

// WDT/panic/software reset: tu phuc hoi me neu snapshot EEPROM hop le.
// POWERON/BROWNOUT: bat buoc hoi nguoi van hanh co tiep tuc me hay khong.
constexpr uint8_t RESET_STORM_LIMIT = 3U;
constexpr uint32_t RESET_STORM_STABLE_CLEAR_MS = 600000UL; // 10 phut chay on dinh xoa dem

// Cac moc thoi gian an toan.
constexpr uint32_t INPUT_SCAN_MS = 5UL;
constexpr uint32_t INPUT_DEBOUNCE_MS = 30UL;
// Sau khi cong tac thiet yeu bat lai, doi on dinh truoc khi xoa E130/E131.
constexpr uint32_t ESSENTIAL_INPUT_RESTORE_CONFIRM_MS = 1000UL;
constexpr uint32_t LIMIT_DEBOUNCE_MS = 20UL;
constexpr uint32_t FAN_PRESTART_MS = 5000UL;
constexpr uint32_t HEAT_MASTER_PICKUP_MS = 500UL;
constexpr uint32_t HEAT_MASTER_DROP_DELAY_MS = 120UL; // tat SSR truoc, roi nha contactor
// Bao ve contactor tong nhiet: sau khi nha output master, doi toi thieu truoc khi dong lai.
// Khong bao gio tri hoan thao tac OFF vi an toan.
constexpr uint32_t HEAT_MASTER_MIN_OFF_MS = 3000UL;
constexpr uint32_t HEAT_RESTART_LOCKOUT_MS = 30000UL;
constexpr uint32_t POST_COOL_MS = 60000UL;
constexpr uint32_t TURN_DIRECTION_DEADTIME_MS = 500UL;
constexpr uint32_t TURN_LIMIT_RELEASE_TIMEOUT_MS = 2500UL;
constexpr uint32_t TURN_INPUT_CONFLICT_MS = 500UL;
constexpr uint32_t SENSOR_RECOVERY_GOOD_SAMPLES = 3UL;
// Mau nhiet giam dot ngot co the lam PID tang cong suat sai. Mau tang cao
// van duoc chap nhan ngay de bao ve qua nhiet.
constexpr float SENSOR_MAX_DOWN_STEP_C = 1.5f;
constexpr float SENSOR_PLAUSIBILITY_MATCH_C = 0.30f;
constexpr uint8_t SENSOR_PLAUSIBILITY_CONFIRM_SAMPLES = 3U;
constexpr uint32_t SENSOR_STARTUP_GRACE_MS = 20000UL; // chua bao coi trong 20 s dau
constexpr uint32_t LOW_TEMP_STARTUP_GRACE_MS = 300000UL; // 5 phut
constexpr uint32_t LOW_TEMP_CONFIRM_MS = 300000UL;       // thap lien tuc 5 phut
constexpr uint32_t HIGH_TEMP_CONFIRM_MS = 1000UL;
constexpr float HIGH_TEMP_CLEAR_HYSTERESIS_C = 0.2f;
constexpr uint32_t HIGH_TEMP_CLEAR_CONFIRM_MS = 10000UL;
constexpr float EMERGENCY_CLEAR_HYSTERESIS_C = 0.3f;
constexpr uint32_t EMERGENCY_CLEAR_CONFIRM_MS = 30000UL;
constexpr uint32_t SIREN_TEMPORARY_MUTE_MS = 300000UL;
constexpr uint32_t BATCH_CHECKPOINT_MS = 300000UL;
// Trend trong me: 5 phut/mau. Event quan trong duoc ghi ngay khi xay ra.
constexpr uint32_t BATCH_LOG_SAMPLE_MS = 300000UL;
constexpr size_t BATCH_LOG_MIN_FREE_BYTES = 256U * 1024U;
constexpr uint8_t BATCH_LOG_MAX_FILES = 8U;
// Mac dinh ngung dao trong 3 ngay cuoi truoc no. Ap dung theo tong so ngay me.
constexpr uint8_t TURN_LOCKDOWN_DAYS = 3U;
// Khi nguoi dung dung/huy me ma chua xoa duoc ban ghi EEPROM, may van dung
// output ngay va thu lai theo chu ky huu han. Khong cho bat dau/phuc hoi me moi.
constexpr uint32_t BATCH_CLEAR_RETRY_MS = 3000UL;

// NVS noi bo chi dung lam nhat ky an toan rat nho, KHONG luu cau hinh may.
// Muc dich: khong cho phuc hoi lai me cu neu nguoi dung da bam DUNG/HUY
// nhung EEPROM ngoai chua kip xac nhan wasRunning=0 truoc mot lan reset.
constexpr char SAFETY_NVS_NAMESPACE[] = "mayap_safe";
constexpr char SAFETY_NVS_STOP_KEY[] = "stop_intent";
constexpr char SAFETY_NVS_RESET_KEY[] = "reset_count";
constexpr uint32_t RUNTIME_TO_HMI_MS = 200UL;
constexpr uint32_t DIAGNOSTIC_STATUS_MS = 10000UL;
constexpr bool SERIAL_DEBUG_DEFAULT_ON = false;
constexpr uint32_t CONTROL_TASK_PERIOD_MS = 5UL;
constexpr uint32_t HMI_TASK_PERIOD_MS = 5UL;
constexpr uint32_t SUPERVISOR_TASK_PERIOD_MS = 50UL;
constexpr uint32_t NETWORK_TASK_PERIOD_MS = 250UL;
// ESP-IDF tinh stack task theo byte. Tat ca buffer cap phat tinh, khong phan manh heap.
constexpr size_t CONTROL_TASK_STACK_BYTES = 8192U;
constexpr size_t HMI_TASK_STACK_BYTES = 10240U;
constexpr size_t SUPERVISOR_TASK_STACK_BYTES = 4096U;
constexpr size_t NETWORK_TASK_STACK_BYTES = 6144U;
constexpr uint32_t TASK_STACK_MONITOR_MS = 60000UL;
// 5 s: du bien cho giao dich I2C huu han nhung van phat hien task bi treo.
constexpr uint32_t CONTROL_WDT_TIMEOUT_MS = 5000UL;
constexpr uint32_t CONTROL_HEARTBEAT_TIMEOUT_MS = 500UL;
// Nguong trip da tinh den giao dich EEPROM dong bo hiem khi xay ra.
constexpr uint32_t CONTROL_CYCLE_TRIP_US = 400000UL;
constexpr uint8_t CONTROL_CYCLE_TRIP_COUNT = 3U;
constexpr uint32_t HMI_HEARTBEAT_TIMEOUT_MS = 2000UL;
constexpr uint32_t SUPERVISOR_RESTART_FALLBACK_MS = 7000UL; // TWDT 5 s duoc uu tien; day la fallback

// Wi-Fi chi chay o task rieng core 0; khong duoc goi tu task dieu khien.
constexpr uint32_t NETWORK_CONNECT_TIMEOUT_MS = 20000UL;
constexpr uint32_t NETWORK_RETRY_INTERVAL_MS = 30000UL;

// ----------------------- KERNEL / EVENT -------------------------------------
constexpr uint8_t INPUT_EVENT_QUEUE_SIZE = 16U;
constexpr uint8_t OUTPUT_EVENT_QUEUE_SIZE = 16U;
constexpr uint8_t EVENT_LOG_RAM_SIZE = 64U;
// HMI hien cac su kien RAM moi nhat. Flash log tam tat trong ban Recovery.
constexpr uint8_t HMI_EVENT_DISPLAY_CAPACITY = 24U;
constexpr uint8_t HMI_FAULT_DISPLAY_CAPACITY = 12U;
constexpr uint32_t RELAY_GENERAL_MIN_SWITCH_MS = 250UL;
constexpr uint32_t RELAY_FAN_MIN_ON_MS = 2000UL;
constexpr uint32_t RELAY_LIGHT_MIN_SWITCH_MS = 150UL;
constexpr uint32_t DIAGNOSTIC_FAST_STATUS_MS = 1000UL;
constexpr uint16_t MAX_RELAY_TRANSITIONS_PER_HOUR = 1800U;

// SSR zero-cross: cua so cham, co xung toi thieu de tranh dap lien tuc.
constexpr uint32_t SSR_MIN_ON_MS = 300UL;
constexpr uint32_t SSR_MIN_OFF_MS = 300UL;

// Auto Tune relay co gioi han, khong chay khi dang co me.
constexpr uint8_t AUTOTUNE_RELAY_POWER_PERCENT = 30;
constexpr float AUTOTUNE_BAND_C = 0.20f;
constexpr uint8_t AUTOTUNE_REQUIRED_CYCLES = 3;
constexpr uint32_t AUTOTUNE_MAX_MS = 2700000UL; // 45 phut
constexpr uint32_t AUTOTUNE_PHASE_MAX_MS = 900000UL; // moi pha toi da 15 phut
constexpr uint32_t AUTOTUNE_MIN_PERIOD_MS = 10000UL;
constexpr float AUTOTUNE_MIN_AMPLITUDE_C = 0.10f;

// LED RGB - do sang thap de khong nong/khong choi trong tu dien.
constexpr uint8_t RGB_BRIGHTNESS_LOW = 6;
constexpr uint8_t RGB_BRIGHTNESS_NORMAL = 14;
constexpr uint8_t RGB_BRIGHTNESS_ALARM = 30;

// Khong gia mao ngay thuc. Chi bat neu chap nhan hien ngay bien dich.
constexpr bool DISPLAY_BUILD_DATE_WHEN_RTC_MISSING = false;

// ----------------------- DS3231 + AT24C32 -----------------------------------
// Dia chi co dinh de code gon va xac dinh. Hay quet I2C mot lan roi sua
// EEPROM_I2C_ADDRESS neu module cua ban khong phai 0x57.
constexpr bool EXTERNAL_EEPROM_ENABLED = true;
constexpr bool EXTERNAL_EEPROM_REQUIRED = true;
constexpr uint8_t RTC_I2C_ADDRESS = 0x68U;
constexpr uint8_t EEPROM_I2C_ADDRESS = 0x57U;
constexpr uint16_t EEPROM_CAPACITY_BYTES = 4096U;
constexpr uint8_t EEPROM_PAGE_SIZE = 32U;
constexpr uint16_t EEPROM_WRITE_TIMEOUT_MS = 20U;
constexpr uint8_t EEPROM_IO_RETRIES = 2U;
constexpr uint16_t EEPROM_RETRY_GAP_MS = 2U;
// Mot lan luu loi chi bao suy giam; chi khoa cung sau nhieu lan loi lien tiep.
constexpr uint8_t EEPROM_FAILURE_LATCH_COUNT = 3U;
constexpr uint32_t RTC_READ_PERIOD_MS = 1000UL;
constexpr uint32_t RTC_STUCK_TIMEOUT_MS = 4000UL;
constexpr uint16_t RTC_VALID_YEAR_MIN = 2024U;
constexpr uint16_t RTC_VALID_YEAR_MAX = 2099U;
// Tu phuc hoi khi thao module DS3231 ra/lap lai trong luc ESP32 van con nguon.
// Firmware giu mot dong ho bong trong RAM tu moc RTC hop le gan nhat.
constexpr bool RTC_AUTO_REPAIR_ENABLED = true;
constexpr uint8_t RTC_AUTO_REPAIR_CONFIRM_READS = 2U;
constexpr uint8_t RTC_AUTO_REPAIR_MAX_ATTEMPTS = 3U;
constexpr uint32_t RTC_AUTO_REPAIR_RETRY_MS = 3000UL;
constexpr uint32_t RTC_AUTO_REPAIR_MAX_GAP_SEC = 24UL * 3600UL;
// Thu ket noi lai EEPROM dung dia chi co dinh; khong quet bus.
constexpr uint32_t EEPROM_RECONNECT_PERIOD_MS = 3000UL;
constexpr uint32_t EEPROM_HEALTH_CHECK_MS = 5000UL;
constexpr uint8_t EEPROM_RECOVERY_VERIFY_COUNT = 2U;
constexpr uint32_t MAX_RTC_RECOVERY_GAP_SEC = 45UL * 86400UL;

// Ban do AT24C32, dia chi o nho 16-bit:
// Config A/B 256 byte; Batch A/B 128 byte; phan con lai du phong.
constexpr uint16_t EEPROM_ADDR_CONFIG_A = 0x0000U;
constexpr uint16_t EEPROM_ADDR_CONFIG_B = 0x0100U;
constexpr uint16_t EEPROM_ADDR_BATCH_A  = 0x0200U;
constexpr uint16_t EEPROM_ADDR_BATCH_B  = 0x0280U;
constexpr uint16_t EEPROM_CONFIG_SLOT_BYTES = 0x0100U;
constexpr uint16_t EEPROM_BATCH_SLOT_BYTES = 0x0080U;

static_assert(EEPROM_I2C_ADDRESS >= 0x50U && EEPROM_I2C_ADDRESS <= 0x57U,
              "Dia chi AT24C32 phai nam trong 0x50..0x57");
static_assert(EEPROM_PAGE_SIZE == 32U, "AT24C32 page phai 32 byte");
static_assert(EEPROM_IO_RETRIES > 0U, "EEPROM_IO_RETRIES phai > 0");
static_assert(EEPROM_FAILURE_LATCH_COUNT > 0U,
              "EEPROM_FAILURE_LATCH_COUNT phai > 0");
static_assert(sizeof(SAFETY_NVS_NAMESPACE) <= 16U,
              "NVS namespace toi da 15 ky tu");
static_assert(sizeof(SAFETY_NVS_STOP_KEY) <= 16U,
              "NVS key toi da 15 ky tu");
static_assert(sizeof(SAFETY_NVS_RESET_KEY) <= 16U,
              "NVS key toi da 15 ky tu");
static_assert(REQUIRE_HEATER_ENABLE_TO_START,
              "Ban thuong mai bat buoc cong tac nhiet ON khi bat dau me");
static_assert(RTC_AUTO_REPAIR_CONFIRM_READS > 0U,
              "RTC_AUTO_REPAIR_CONFIRM_READS phai > 0");
static_assert(RTC_AUTO_REPAIR_MAX_ATTEMPTS > 0U,
              "RTC_AUTO_REPAIR_MAX_ATTEMPTS phai > 0");
static_assert(RTC_AUTO_REPAIR_MAX_GAP_SEC < 0x7FFFFFFFUL / 1000UL,
              "RTC auto repair gap qua lon cho millis rollover");
static_assert(EEPROM_RECOVERY_VERIFY_COUNT > 0U,
              "EEPROM_RECOVERY_VERIFY_COUNT phai > 0");
static_assert(EEPROM_ADDR_CONFIG_A + EEPROM_CONFIG_SLOT_BYTES <= EEPROM_ADDR_CONFIG_B, "Config A de len B");
static_assert(EEPROM_ADDR_CONFIG_B + EEPROM_CONFIG_SLOT_BYTES <= EEPROM_ADDR_BATCH_A, "Config B de len Batch A");
static_assert(EEPROM_ADDR_BATCH_A + EEPROM_BATCH_SLOT_BYTES <= EEPROM_ADDR_BATCH_B, "Batch A de len B");
static_assert(EEPROM_ADDR_BATCH_B + EEPROM_BATCH_SLOT_BYTES <= EEPROM_CAPACITY_BYTES,
              "Ban do EEPROM vuot 4KB");

// -------------------- RANG BUOC HMI/AN TOAN ---------------------------------
constexpr float TARGET_TEMP_MIN_C = 30.0f;
constexpr float TARGET_TEMP_MAX_C = 40.0f;
constexpr float LOW_ALARM_GAP_C = 0.1f;
constexpr float HIGH_ALARM_GAP_C = 0.1f;
constexpr float EMERGENCY_ABOVE_HIGH_C = 0.1f;
constexpr float VENT_OFF_ABOVE_SV_C = 0.0f;
constexpr float VENT_ON_ABOVE_SV_C = 0.1f;
constexpr float VENT_HYSTERESIS_C = 0.1f;
// Chuoi rang buoc: SV <= HUT OFF < HUT ON <= BAO CAO < KHAN CAP.
// VENT_HYSTERESIS_C la khoang cach toi thieu giua nguong tat va bat quat hut.
constexpr float HIGH_ALARM_MAX_C = 42.0f;
constexpr float EMERGENCY_MAX_C = 45.0f;

// ============================================================================
// HOP DONG DU LIEU HMI <-> FIRMWARE
// ============================================================================
enum class ControlMode : uint8_t { OnOff = 0, Pid = 1 };
enum class TurnDirection : uint8_t { Left = 0, Right = 1 };
enum class ConnectivityMode : uint8_t { Offline = 0, Online = 1 };
enum class NetworkStateCode : uint8_t {
  Offline = 0, NotConfigured = 1, Connecting = 2, Connected = 3
};
enum class TurnState : uint8_t {
  Stopped = 0, Left = 1, Right = 2, Waiting = 3, Fault = 4
};
enum class AutoTuneState : uint8_t {
  Idle = 0, Running = 1, Success = 2, Failed = 3
};

enum class MachineStateCode : uint8_t {
  Boot = 0, ReadyAuto, ReadyManual, ResumeWait, Prestart, Homing,
  RunningAuto, RunningManual, AutoTune,
  SensorFault, TurningFault, SystemFault, Emergency
};

enum AlarmBit : uint32_t {
  AlarmNone        = 0,
  AlarmSensor      = 1UL << 0,
  AlarmTempLow     = 1UL << 1,
  AlarmTempHigh    = 1UL << 2,
  AlarmEmergency   = 1UL << 3,
  AlarmHumidityLow = 1UL << 4,
  AlarmTurning     = 1UL << 5,
  AlarmAutoMode    = 1UL << 6,
  AlarmSystem      = 1UL << 7
};
constexpr uint32_t ALARM_KNOWN_MASK =
    AlarmSensor | AlarmTempLow | AlarmTempHigh | AlarmEmergency |
    AlarmHumidityLow | AlarmTurning | AlarmAutoMode | AlarmSystem;

struct MachineConfig {
  float targetTemp = 37.5f;
  float tempHysteresis = 0.2f;
  float lowTempAlarm = 36.5f;
  float highTempAlarm = 38.2f;
  float emergencyTemp = 39.0f;
  ControlMode controlMode = ControlMode::Pid;
  float kp = 18.0f;
  float ki = 0.8f;
  float kd = 45.0f;
  uint16_t pidCycleSec = 10;
  uint8_t maxHeaterPower = 100;

  float lowHumidityAlarm = 45.0f;
  uint16_t humidityAlarmDelaySec = 60;

  bool circulationFanEnabled = true;
  float ventOnTemp = 38.0f;
  float ventOffTemp = 37.6f;

  bool turningEnabled = true;
  uint16_t turnIntervalMin = 120;
  uint16_t turnMaxRunSec = 300;
  TurnDirection nextDirection = TurnDirection::Left;

  uint8_t totalIncubationDays = 21;
  bool allowHeatWithoutBatch = false;
  uint16_t powerRestoreDelaySec = 30;

  float tempOffset = 0.0f;
  float humidityOffset = 0.0f;
  uint16_t sensorTimeoutSec = 10;
  bool alarmEnabled = true;
  ConnectivityMode connectivityMode = ConnectivityMode::Offline;
};

struct NetworkStatus {
  ConnectivityMode requestedMode = ConnectivityMode::Offline;
  NetworkStateCode state = NetworkStateCode::Offline;
  bool credentialsConfigured = false;
  bool connected = false;
  int8_t rssiDbm = -127;
};

struct HmiFaultItem {
  uint16_t code = 0;
  int16_t detail = 0;
  uint8_t severity = 0;
  uint8_t flags = 0;  // bit0: condition con ton tai, bit1: da ACK
};

struct HmiEventItem {
  uint32_t sequence = 0;
  uint32_t epoch = 0;       // ngay gio DS3231 tai luc su kien; 0 = chua hop le
  uint32_t ageSec = 0;
  uint16_t code = 0;
  int16_t value = 0;
  uint8_t type = 0;
  uint8_t flags = 0;
};

struct HmiEventSnapshot {
  uint32_t sourceSequence = 0;
  uint8_t count = 0;
  uint8_t totalInWindow = 0;
  HmiEventItem items[HMI_EVENT_DISPLAY_CAPACITY]{};
};

struct MachineRuntime {
  float temperature = NAN;
  float humidity = NAN;
  bool sensorOnline = false;
  bool batchRunning = false;
  bool autoMode = false;
  bool turningLockdown = false;
  bool batchLogAvailable = false;
  uint8_t currentDay = 0;
  float heaterPower = 0.0f;
  bool heaterOn = false;
  bool circulationFanOn = false;
  bool ventFanOn = false;
  TurnState turnState = TurnState::Stopped;
  uint16_t nextTurnMinutes = 0;
  bool nextTurnScheduled = false;
  uint16_t turnCountToday = 0;
  uint32_t turnCountBatch = 0;
  uint32_t alarmMask = AlarmNone;
  AutoTuneState autoTuneState = AutoTuneState::Idle;
  uint8_t autoTuneProgress = 0;
  MachineStateCode stateCode = MachineStateCode::Boot;
  uint16_t primaryFaultCode = 0;
  uint8_t activeFaultCount = 0;
  uint8_t activeFaultDisplayCount = 0;
  HmiFaultItem activeFaults[HMI_FAULT_DISPLAY_CAPACITY]{};
  uint32_t faultNotificationSequence = 0;
  uint16_t lastRaisedFaultCode = 0;
  uint8_t lastRaisedFaultSeverity = 0;
  uint32_t eventSequence = 0;
  uint16_t relayTransitionsHour = 0;
  bool sensorStartupGrace = true;
  bool resumeConfirmationRequired = false;
  bool timeValid = false;
  ConnectivityMode connectivityMode = ConnectivityMode::Offline;
  NetworkStateCode networkState = NetworkStateCode::Offline;
  bool networkConfigured = false;
  bool networkConnected = false;
  int8_t networkRssiDbm = -127;
  char dateText[11] = "--/--/----";
  char machineState[20] = "KHOI DONG";
};

enum class HmiCommandType : uint8_t {
  None, BatchStart, BatchStop, TurnLeft, TurnStop, TurnRight,
  AlarmAck, AutoTuneStart, ResumeYes, ResumeNo
};
struct HmiCommand {
  uint32_t id = 0;
  HmiCommandType type = HmiCommandType::None;
  uint32_t createdAt = 0;
  uint16_t validForMs = 0;
  uint16_t actuatorLeaseMs = 0;
  uint32_t alarmMask = AlarmNone;
};
enum class BuzzerCue : uint8_t { None, Key, Save, Ok, Error };

using HmiI2cLockFn = bool (*)(uint32_t timeoutMs);
using HmiI2cUnlockFn = void (*)();

// Dung chung giua HMI, EEPROM va firmware tong.
bool mayapI2cLock(uint32_t timeoutMs);
void mayapI2cUnlock();
bool mayapSerialDebugEnabled();
void mayapSetSerialDebugEnabled(bool enabled);
void mayapSerialPrintf(bool force, const char *format, ...);
// Latch an toan toan he thong: chi reset chip moi xoa duoc.
bool mayapSystemTripLatched();
void mayapLatchSystemTrip();

// API task mang: implementation nam trong network_service.h.
void mayapNetworkBegin();
void mayapNetworkUpdate(uint32_t now);
void mayapSetConnectivityMode(ConnectivityMode mode);
NetworkStatus mayapGetNetworkStatus();

void hmiBegin();
void hmiUpdate(uint32_t now);
void hmiSetConfig(const MachineConfig &config);
void hmiSetDate(const char *dateText);
const MachineConfig &hmiGetConfig();
void hmiSetRuntime(const MachineRuntime &runtime);
void hmiSetEventLog(const HmiEventSnapshot &snapshot);
bool hmiTakeSavedConfig(MachineConfig &out, uint32_t &transactionId);
bool hmiConfirmConfigSave(uint32_t transactionId, bool ok,
                          const MachineConfig *storedConfig = nullptr);
bool hmiTakeCommand(HmiCommand &out);
bool hmiConfirmCommand(uint32_t commandId, bool ok,
                       const char *message = nullptr);
void hmiSetI2cLockCallbacks(HmiI2cLockFn lockFn, HmiI2cUnlockFn unlockFn);
void hmiPlayCue(BuzzerCue cue);
