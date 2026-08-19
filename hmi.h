#pragma once

/*
  MAYAP HMI ST7567S 128x64 + rotary + buzzer - v3.6.0
  Phan cung: LCD 0x3F SDA8/SCL9, rotary 38/39/40, buzzer GPIO41.
  File nay chi dung trong firmware tong; khong chua setup/loop demo, Wi-Fi,
  ket noi mang, luu flash noi hay dieu khien GPIO chap hanh.
*/

#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <type_traits>
#include <ctype.h>
#include <stdlib.h>
#if MAYAP_HMI_ENCODER_INTERRUPT
#include <driver/gpio.h>
#endif

#if LCD_PROFILE == 1
U8G2_ST7567_ENH_DG128064I_F_HW_I2C lcd(U8G2_R0, U8X8_PIN_NONE);
#elif LCD_PROFILE == 2
U8G2_ST7567_JLX12864_F_HW_I2C lcd(U8G2_R0, U8X8_PIN_NONE);
#else
#error "LCD_PROFILE khong hop le"
#endif

// Coi active chi dieu khien ON/OFF qua transistor/MOSFET.
// "Nhe/to" duoc tao bang do dai va nhip keu, khong PWM relay.
struct BuzzerPattern {
  uint16_t onMs;
  uint16_t offMs;
  uint8_t pulses;
};

struct BuzzerState {
  uint32_t acknowledgedAlarmMask = 0;
  uint32_t acknowledgedAt[8] = {};
  uint32_t soundingAlarmBit = AlarmNone;
  uint32_t deadline = 0;
  bool outputOn = false;
  bool transientActive = false;
  bool transientOn = false;
  bool transientMandatory = false;
  bool resumePromptActive = false;
  bool turnPatternActive = false;
  uint8_t turnPhase = 0;
  uint8_t transientPulsesLeft = 0;
  BuzzerPattern transientPattern{0, 0, 0};
};

BuzzerState buzzer;

void buzzerBegin();
void buzzerUpdate(uint32_t now);
void buzzerPlayCue(BuzzerCue cue);
void buzzerAcknowledge(uint32_t alarmMask);
BuzzerPattern buzzerCuePattern(BuzzerCue cue);
BuzzerPattern alarmPattern(uint32_t bit);

static_assert(std::is_standard_layout<MachineConfig>::value, "MachineConfig phai la standard-layout");
static_assert(std::is_trivially_copyable<MachineConfig>::value,
              "MachineConfig phai copy duoc trong mailbox");
static_assert(std::is_trivially_copyable<MachineRuntime>::value,
              "MachineRuntime phai copy duoc trong mailbox");
static_assert(std::is_trivially_copyable<HmiEventSnapshot>::value,
              "HmiEventSnapshot phai copy duoc trong mailbox");

enum class ButtonEvent : uint8_t { None, ShortPress, LongPress };
struct RotaryState {
  uint8_t previousAB = 0;
  int16_t accumulator = 0;
  int8_t step = 0;
  bool lastRawButton = HIGH;
  bool stableButton = HIGH;
  uint32_t rawChangedAt = 0;
  uint32_t pressedAt = 0;
  bool longPressReported = false;
  ButtonEvent button = ButtonEvent::None;
};
RotaryState rotary;

// Dat bang giai ma trong DRAM de ISR rotary khong phu thuoc cache flash.
DRAM_ATTR static const int8_t QUADRATURE_TABLE[16] = {
   0, -1,  1,  0,  1,  0,  0, -1,
  -1,  0,  0,  1,  0,  1, -1,  0
};

uint8_t readAB() {
#if MAYAP_HMI_ENCODER_INTERRUPT
  return static_cast<uint8_t>(
      (gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_CLK)) << 1) |
       gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_DT)));
#else
  return static_cast<uint8_t>(
      (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT));
#endif
}

#if MAYAP_HMI_ENCODER_INTERRUPT
portMUX_TYPE rotaryIsrMux = portMUX_INITIALIZER_UNLOCKED;
volatile int16_t rotaryPendingTransitions = 0;
volatile uint8_t rotaryIsrPreviousAB = 0;

void IRAM_ATTR rotaryEncoderIsr() {
  const uint8_t ab = static_cast<uint8_t>(
      (gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_CLK)) << 1) |
       gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_DT)));

  portENTER_CRITICAL_ISR(&rotaryIsrMux);
  const uint8_t previous = rotaryIsrPreviousAB;
  rotaryIsrPreviousAB = ab;
  const int8_t delta = QUADRATURE_TABLE[(previous << 2) | ab];
  if (delta) {
    int16_t pending = static_cast<int16_t>(rotaryPendingTransitions + delta);
    // Gioi han backlog khi day encoder bi nhieu lien tuc.
    if (pending > 64) pending = 64;
    if (pending < -64) pending = -64;
    rotaryPendingTransitions = pending;
  }
  portEXIT_CRITICAL_ISR(&rotaryIsrMux);
}
#endif

void beginRotary() {
  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW, INPUT_PULLUP);
  rotary.previousAB = readAB();
#if MAYAP_HMI_ENCODER_INTERRUPT
  rotaryIsrPreviousAB = rotary.previousAB;
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_CLK), rotaryEncoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_DT), rotaryEncoderIsr, CHANGE);
#endif
  rotary.lastRawButton = digitalRead(PIN_ENCODER_SW);
  rotary.stableButton = rotary.lastRawButton;
}

void updateRotary(uint32_t now) {
  rotary.step = 0;
  rotary.button = ButtonEvent::None;

  int16_t transitionDelta = 0;
#if MAYAP_HMI_ENCODER_INTERRUPT
  portENTER_CRITICAL(&rotaryIsrMux);
  transitionDelta = rotaryPendingTransitions;
  rotaryPendingTransitions = 0;
  portEXIT_CRITICAL(&rotaryIsrMux);
#else
  const uint8_t ab = readAB();
  if (ab != rotary.previousAB) {
    transitionDelta = QUADRATURE_TABLE[(rotary.previousAB << 2) | ab];
    rotary.previousAB = ab;
  }
#endif

  rotary.accumulator = static_cast<int16_t>(rotary.accumulator + transitionDelta);
  int16_t detents = rotary.accumulator / ENCODER_STEPS_PER_DETENT;
  if (detents) {
    int16_t emitted = detents;
    if (emitted > ENCODER_MAX_STEPS_PER_UPDATE) {
      emitted = ENCODER_MAX_STEPS_PER_UPDATE;
    } else if (emitted < -static_cast<int16_t>(ENCODER_MAX_STEPS_PER_UPDATE)) {
      emitted = -static_cast<int16_t>(ENCODER_MAX_STEPS_PER_UPDATE);
    }
    rotary.accumulator = static_cast<int16_t>(
        rotary.accumulator - emitted * ENCODER_STEPS_PER_DETENT);
    rotary.step = static_cast<int8_t>(REVERSE_ENCODER ? -emitted : emitted);
  }

  const bool raw = digitalRead(PIN_ENCODER_SW);
  if (raw != rotary.lastRawButton) {
    rotary.lastRawButton = raw;
    rotary.rawChangedAt = now;
  }
  if (raw != rotary.stableButton && now - rotary.rawChangedAt >= BUTTON_DEBOUNCE_MS) {
    rotary.stableButton = raw;
    if (raw == LOW) {
      rotary.pressedAt = now;
      rotary.longPressReported = false;
    } else if (!rotary.longPressReported) {
      rotary.button = ButtonEvent::ShortPress;
    }
  }

  // Phat LongPress ngay khi du thoi gian, khong doi nguoi dung nha nut.
  if (rotary.stableButton == LOW && !rotary.longPressReported &&
      now - rotary.pressedAt >= BUTTON_LONG_PRESS_MS) {
    rotary.longPressReported = true;
    rotary.button = ButtonEvent::LongPress;
  }
}

// ============================================================
// 4. BANG CAI DAT - THEM THONG SO KHONG CAN THEM MAN HINH MOI
// ============================================================
enum class SettingType : uint8_t { Bool, U8, U16, Float };

struct SettingItem {
  const char *label;
  SettingType type;
  uint16_t offset;
  float minimum;
  float maximum;
  float step;
  uint8_t decimals;
  const char *unit;
  const char *const *options;
  uint8_t optionCount;
};

// Khai bao prototype thu cong de tranh loi Arduino .ino auto-prototype
// voi kieu SettingItem duoc khai bao sau cac ham dau tien trong sketch.
float readSetting(const MachineConfig &cfg, const SettingItem &item);
void writeSetting(MachineConfig &cfg, const SettingItem &item, float value);
void normalizeConfig(MachineConfig &cfg);
void sanitizeConfig(MachineConfig &cfg);
void settingLimits(const MachineConfig &cfg, const SettingItem &item, float &minimum, float &maximum);
void formatSettingValue(const SettingItem &item, float value, char *out, size_t size);

const char *const OPT_OFF_ON[] = {"TAT", "BAT"};
const char *const OPT_OFFLINE_ONLINE[] = {"OFFLINE", "ONLINE"};
const char *const OPT_ON_OFF_HOI[] = {"HOI XN", "TU DONG"};
#define ITEM_FLOAT(lbl, member, mn, mx, st, dec, unitText) \
  {lbl, SettingType::Float, offsetof(MachineConfig, member), mn, mx, st, dec, unitText, nullptr, 0}
#define ITEM_U8(lbl, member, mn, mx, st, unitText) \
  {lbl, SettingType::U8, offsetof(MachineConfig, member), mn, mx, st, 0, unitText, nullptr, 0}
#define ITEM_U8_OPTIONS(lbl, member, opts, count) \
  {lbl, SettingType::U8, offsetof(MachineConfig, member), 0, (count) - 1, 1, 0, "", opts, count}
#define ITEM_U16(lbl, member, mn, mx, st, unitText) \
  {lbl, SettingType::U16, offsetof(MachineConfig, member), mn, mx, st, 0, unitText, nullptr, 0}
#define ITEM_BOOL_OPTIONS(lbl, member, opts) \
  {lbl, SettingType::Bool, offsetof(MachineConfig, member), 0, 1, 1, 0, "", opts, 2}
#define ITEM_BOOL(lbl, member) ITEM_BOOL_OPTIONS(lbl, member, OPT_OFF_ON)
// v3.5.0: "CAI DAT ME" chi giu 4 thong so thao tac hang ngay; moi thu con lai
// don sang cac thu muc trong "CAI DAT CHUNG" (xem GROUPS/ChungMenu ben duoi).
const SettingItem SETTINGS[] = {
  // ---- CAI DAT ME (4 muc) ----
  ITEM_FLOAT("Nhiet do ap", targetTemp, TARGET_TEMP_MIN_C,
             TARGET_TEMP_MAX_C, 0.1f, 1, "C"),                                  // 0
  ITEM_FLOAT("Canh bao am", lowHumidityAlarm, 10.0f, 90.0f, 1.0f, 0, "%"),      // 1 (bao het nuoc)
  ITEM_BOOL_OPTIONS("Ap lai", autoResumeOnPowerLoss, OPT_ON_OFF_HOI),           // 2
  ITEM_U8("So ngay ap", totalIncubationDays, 1, 40, 1, "ng"),                   // 3

  // ---- CAI DAT CHUNG > NHIET DO (3 muc) ----
  // Chuoi nhiet tren LCD duoc rang buoc dong bo:
  // Bao thap < SV <= Hut tat < Hut bat <= Bao cao < Bao khan cap.
  ITEM_FLOAT("Bao thap", lowTempAlarm, 25.0f,
             TARGET_TEMP_MAX_C - LOW_ALARM_GAP_C, 0.1f, 1, "C"),               // 4
  ITEM_FLOAT("Bao cao", highTempAlarm,
             TARGET_TEMP_MIN_C + HIGH_ALARM_GAP_C,
             HIGH_ALARM_MAX_C, 0.1f, 1, "C"),                                  // 5
  ITEM_FLOAT("Bao khan cap", emergencyTemp,
             TARGET_TEMP_MIN_C + HIGH_ALARM_GAP_C + EMERGENCY_ABOVE_HIGH_C,
             EMERGENCY_MAX_C, 0.1f, 1, "C"),                                   // 6

  // ---- CAI DAT CHUNG > QUAT HUT (2 muc) ----
  ITEM_FLOAT("Quat hut bat", ventOnTemp, TARGET_TEMP_MIN_C + VENT_ON_ABOVE_SV_C,
             HIGH_ALARM_MAX_C, 0.1f, 1, "C"),                                  // 7
  ITEM_FLOAT("Quat hut tat", ventOffTemp, TARGET_TEMP_MIN_C,
             HIGH_ALARM_MAX_C, 0.1f, 1, "C"),                                  // 8

  // ---- CAI DAT CHUNG > DAO TRUNG (3 muc, + "So lan dao" la dong phu) ----
  ITEM_BOOL("Tu dong dao", turningEnabled),                                    // 9
  ITEM_U16("Chu ky dao", turnIntervalMin, 1, 720, 1, "ph"),                   // 10
  ITEM_U16("Tre loi dao", turnMaxRunSec, 5, 600, 5, "s"),                    // 11

  // ---- CAI DAT CHUNG > KET NOI (1 muc, + "Doi wifi" la dong phu) ----
  ITEM_U8_OPTIONS("Che do ket noi", connectivityMode,
                  OPT_OFFLINE_ONLINE, 2),                                     // 12

  // ---- CAI DAT CHUNG > THONG GIO (2 muc) ----
  // Thong gio CO2: rele du bat/tat theo ngay ap, khong lien quan nhiet do.
  ITEM_BOOL("Thong gio CO2", ventilationEnabled),                             // 13
  ITEM_U8("Bat tu ngay", ventilationStartDay, 1, 40, 1, "ng")                  // 14
};

constexpr uint8_t SETTING_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);
static_assert(SETTING_COUNT == 15, "Bang SETTINGS phai co 15 thong so");

const uint8_t GROUP_SETTING_INDEXES[] = {
  0,1,2,3,                             // Cai dat me
  4,5,6,                               // Nhiet do
  7,8,                                 // Quat hut
  9,10,11,                             // Dao trung
  12,                                   // Ket noi
  13,14                                 // Thong gio
};

struct SettingGroup { const char *label; uint8_t first; uint8_t count; };
// Chi so 0 = Cai dat me (goc tu MainMenu); 1..5 = 5 thu muc con cua
// "CAI DAT CHUNG" (goc tu ChungMenu). Dung chung mot co che SettingList.
const SettingGroup GROUPS[] = {
  {"CAI DAT ME", 0, 4},
  {"NHIET DO", 4, 3},
  {"QUAT HUT", 7, 2},
  {"DAO TRUNG", 9, 3},
  {"KET NOI", 12, 1},
  {"THONG GIO", 13, 2}
};
constexpr uint8_t GROUP_COUNT = sizeof(GROUPS) / sizeof(GROUPS[0]);
static_assert(GROUP_COUNT == 6, "Bang GROUPS phai co 6 nhom");
static_assert(sizeof(GROUP_SETTING_INDEXES) / sizeof(GROUP_SETTING_INDEXES[0]) == SETTING_COUNT,
              "Sai so luong tham chieu setting trong GROUP_SETTING_INDEXES");

// Dong phu (khong phai setting gia tri) duoc gan them vao cuoi mot so nhom.
enum class GroupExtra : uint8_t { None, TurnStats, WifiChange };
GroupExtra groupExtra(uint8_t group) {
  if (group == 3) return GroupExtra::TurnStats;   // DAO TRUNG -> "So lan dao"
  if (group == 4) return GroupExtra::WifiChange;   // KET NOI -> "Doi wifi"
  return GroupExtra::None;
}

float readSetting(const MachineConfig &cfg, const SettingItem &item) {
  const uint8_t *base = reinterpret_cast<const uint8_t *>(&cfg) + item.offset;
  switch (item.type) {
    case SettingType::Bool: return *reinterpret_cast<const bool *>(base) ? 1.0f : 0.0f;
    case SettingType::U8: return *reinterpret_cast<const uint8_t *>(base);
    case SettingType::U16: return *reinterpret_cast<const uint16_t *>(base);
    case SettingType::Float: return *reinterpret_cast<const float *>(base);
  }
  return 0;
}

void writeSetting(MachineConfig &cfg, const SettingItem &item, float value) {
  value = constrain(value, item.minimum, item.maximum);
  uint8_t *base = reinterpret_cast<uint8_t *>(&cfg) + item.offset;
  switch (item.type) {
    case SettingType::Bool: *reinterpret_cast<bool *>(base) = value >= 0.5f; break;
    case SettingType::U8: *reinterpret_cast<uint8_t *>(base) = static_cast<uint8_t>(lroundf(value)); break;
    case SettingType::U16: *reinterpret_cast<uint16_t *>(base) = static_cast<uint16_t>(lroundf(value)); break;
    case SettingType::Float: *reinterpret_cast<float *>(base) = value; break;
  }
}

void normalizeConfig(MachineConfig &cfg) {
  // Chuoi rang buoc truc tiep theo SV, moi muc cach nhau toi thieu 0.1 C.
  cfg.lowTempAlarm = fminf(cfg.lowTempAlarm,
                           cfg.targetTemp - LOW_ALARM_GAP_C);

  cfg.highTempAlarm = fmaxf(cfg.highTempAlarm,
                            cfg.targetTemp + HIGH_ALARM_GAP_C);
  cfg.highTempAlarm = fminf(cfg.highTempAlarm, HIGH_ALARM_MAX_C);

  cfg.emergencyTemp = fmaxf(cfg.emergencyTemp,
                            cfg.highTempAlarm + EMERGENCY_ABOVE_HIGH_C);
  cfg.emergencyTemp = fminf(cfg.emergencyTemp, EMERGENCY_MAX_C);

  // Rang buoc dong bo quat hut voi SV va Bao cao:
  // SV <= Hut tat < Hut bat <= Bao cao.
  const float ventOnMin = cfg.targetTemp + VENT_ON_ABOVE_SV_C;
  const float ventOnMax = cfg.highTempAlarm;
  cfg.ventOnTemp = constrain(cfg.ventOnTemp, ventOnMin, ventOnMax);

  const float ventOffMin = cfg.targetTemp + VENT_OFF_ABOVE_SV_C;
  const float ventOffMax = cfg.ventOnTemp - VENT_HYSTERESIS_C;
  cfg.ventOffTemp = constrain(cfg.ventOffTemp, ventOffMin, ventOffMax);
}

void settingLimits(const MachineConfig &cfg, const SettingItem &item,
                   float &minimum, float &maximum) {
  minimum = item.minimum; maximum = item.maximum;
  const uint16_t offset = item.offset;
  if (offset == offsetof(MachineConfig, lowTempAlarm)) {
    maximum = fminf(maximum, cfg.targetTemp - LOW_ALARM_GAP_C);
  }
  else if (offset == offsetof(MachineConfig, highTempAlarm)) {
    minimum = fmaxf(minimum, cfg.targetTemp + HIGH_ALARM_GAP_C);
    minimum = fmaxf(minimum, cfg.ventOnTemp);
    maximum = fminf(maximum, cfg.emergencyTemp - EMERGENCY_ABOVE_HIGH_C);
  } else if (offset == offsetof(MachineConfig, emergencyTemp)) {
    minimum = fmaxf(minimum, cfg.highTempAlarm + EMERGENCY_ABOVE_HIGH_C);
  } else if (offset == offsetof(MachineConfig, ventOnTemp)) {
    minimum = fmaxf(minimum, cfg.targetTemp + VENT_ON_ABOVE_SV_C);
    minimum = fmaxf(minimum, cfg.ventOffTemp + VENT_HYSTERESIS_C);
    maximum = fminf(maximum, cfg.highTempAlarm);
  } else if (offset == offsetof(MachineConfig, ventOffTemp)) {
    minimum = fmaxf(minimum, cfg.targetTemp + VENT_OFF_ABOVE_SV_C);
    maximum = fminf(maximum, cfg.ventOnTemp - VENT_HYSTERESIS_C);
  }
  if (minimum > maximum) minimum = maximum;
}

void sanitizeConfig(MachineConfig &cfg) {
  // Du lieu hong/NaN/Inf khong duoc phep di vao giao dien hoac PID.
  const MachineConfig defaults{};
  for (uint8_t i = 0; i < SETTING_COUNT; ++i) {
    float value = readSetting(cfg, SETTINGS[i]);
    if (!isfinite(value)) value = readSetting(defaults, SETTINGS[i]);
    writeSetting(cfg, SETTINGS[i], value);
  }

  if (!isfinite(cfg.tempHysteresis)) cfg.tempHysteresis = defaults.tempHysteresis;
  cfg.tempHysteresis = constrain(cfg.tempHysteresis, 0.1f, 1.0f);

  // PID luon la che do dieu khien cua san pham. Nguoi dung chi bam Auto Tune.
  cfg.controlMode = ControlMode::Pid;
  if (!isfinite(cfg.kp)) cfg.kp = defaults.kp;
  if (!isfinite(cfg.ki)) cfg.ki = defaults.ki;
  if (!isfinite(cfg.kd)) cfg.kd = defaults.kd;
  cfg.kp = constrain(cfg.kp, 0.0f, 100.0f);
  cfg.ki = constrain(cfg.ki, 0.0f, 20.0f);
  cfg.kd = constrain(cfg.kd, 0.0f, 200.0f);
  cfg.pidCycleSec = static_cast<uint16_t>(constrain(
      static_cast<int>(cfg.pidCycleSec), 1, 60));
  cfg.maxHeaterPower = static_cast<uint8_t>(constrain(
      static_cast<int>(cfg.maxHeaterPower), 10, 100));

  if (!isfinite(cfg.emergencyTemp)) cfg.emergencyTemp = defaults.emergencyTemp;
  if (!isfinite(cfg.ventOnTemp)) cfg.ventOnTemp = defaults.ventOnTemp;
  if (!isfinite(cfg.ventOffTemp)) cfg.ventOffTemp = defaults.ventOffTemp;
  cfg.emergencyTemp = constrain(
      cfg.emergencyTemp,
      TARGET_TEMP_MIN_C + HIGH_ALARM_GAP_C + EMERGENCY_ABOVE_HIGH_C,
      EMERGENCY_MAX_C);
  cfg.ventOnTemp = constrain(cfg.ventOnTemp, 30.0f, 42.0f);
  cfg.ventOffTemp = constrain(cfg.ventOffTemp, 25.0f, 41.0f);

  cfg.humidityAlarmDelaySec = static_cast<uint16_t>(constrain(
      static_cast<int>(cfg.humidityAlarmDelaySec), 0, 600));
  cfg.powerRestoreDelaySec = static_cast<uint16_t>(constrain(
      static_cast<int>(cfg.powerRestoreDelaySec), 0, 600));
  if (!isfinite(cfg.tempOffset)) cfg.tempOffset = defaults.tempOffset;
  if (!isfinite(cfg.humidityOffset)) cfg.humidityOffset = defaults.humidityOffset;
  cfg.tempOffset = constrain(cfg.tempOffset, -5.0f, 5.0f);
  cfg.humidityOffset = constrain(cfg.humidityOffset, -20.0f, 20.0f);
  cfg.sensorTimeoutSec = static_cast<uint16_t>(constrain(
      static_cast<int>(cfg.sensorTimeoutSec), 5, 30));
  if (static_cast<uint8_t>(cfg.nextDirection) >
      static_cast<uint8_t>(TurnDirection::Right)) {
    cfg.nextDirection = defaults.nextDirection;
  }
  if (static_cast<uint8_t>(cfg.connectivityMode) >
      static_cast<uint8_t>(ConnectivityMode::Online)) {
    cfg.connectivityMode = defaults.connectivityMode;
  }
  cfg.alarmEnabled = true;
  normalizeConfig(cfg);
}

void formatSettingValue(const SettingItem &item, float value, char *out, size_t size) {
  if (item.options && item.optionCount > 0) {
    const uint8_t index = static_cast<uint8_t>(constrain(lroundf(value), 0L,
                                  static_cast<long>(item.optionCount - 1U)));
    snprintf(out, size, "%s", item.options[index]);
  } else if (item.decimals == 0) {
    snprintf(out, size, "%ld%s", lroundf(value), item.unit);
  } else {
    snprintf(out, size, "%.*f%s", item.decimals, value, item.unit);
  }
}

// ============================================================
// 5. TRANG, MENU, HANG DOI LENH
// ============================================================
enum class View : uint8_t {
  Home, MainMenu, ChungMenu, SettingList, EditSetting, TurnStats, AutoTune,
  EventLog, Alarm, TestMode, WifiChange
};

enum class ConfirmAction : uint8_t { None, BatchToggle, AutoTuneStart, ResumeBatch };

// Prototype thu cong: Arduino IDE tu sinh prototype cho ham trong .ino.
// Neu ham dung enum/struct tuy chinh, prototype tu dong co the bi chen
// truoc noi khai bao kieu va gay loi "View was not declared".
void openBatchConfirm(View returnView);
void openResumeConfirm();
void openAlarmView(View returnView);

MachineConfig currentConfig;
MachineRuntime currentRuntime;
HmiEventSnapshot currentEventLog;

bool lcdReady = false;
bool dirty = true;

struct ConfigSaveTransaction {
  bool active = false;
  bool readyForHost = false;
  uint32_t id = 0;
  uint32_t startedAt = 0;
  MachineConfig rollback;
  MachineConfig candidate;
};
ConfigSaveTransaction configSave;
uint32_t nextConfigTransactionId = 1;

View view = View::Home;
uint8_t homePage = 0;
uint8_t mainIndex = 0;
uint8_t chungIndex = 0;
uint8_t chungTop = 0;
uint8_t listIndex = 0;
uint8_t listTop = 0;
uint8_t selectedGroup = 0;
uint32_t wifiPortalLastCommandAt = 0;
uint32_t testModeLastCommandAt = 0;
TestLimitId testLimitSelected = TestLimitId::Left;
uint8_t autoTuneRow = 0;

constexpr uint8_t TEST_MODE_OUTPUT_ROWS = static_cast<uint8_t>(TestOutputId::Count);
constexpr uint8_t TEST_MODE_LIMIT_ROWS = 2U;
constexpr uint8_t TEST_MODE_ITEM_COUNT = static_cast<uint8_t>(
    TEST_MODE_OUTPUT_ROWS + TEST_MODE_LIMIT_ROWS + 1U);  // + Thoat
float editValue = 0;
uint8_t editSettingIndex = 0;
ConfirmAction confirmAction = ConfirmAction::None;
View confirmReturnView = View::Home;
bool confirmYes = true;
bool resumeDecisionSubmitted = false;
View alarmReturnView = View::Home;
uint8_t alarmIndex = 0;
uint8_t eventLogIndex = 0;
uint32_t alarmPresentedMask = 0;

uint32_t lastDrawAt = 0;
uint32_t lastHomeDrawAt = 0;
uint32_t lastAlarmDrawAt = 0;
uint32_t lastInteractionAt = 0;
uint32_t lastLcdRetryAt = 0;
uint32_t lastLcdHealthCheckAt = 0;
uint32_t lastLcdFaultLogAt = 0;
char toastLine[27] = "";
bool toastError = false;
uint32_t toastUntil = 0;
uint32_t lastCommandPollAt = 0;
uint32_t inputGuardUntil = 0;
uint32_t uiNextVerifyDrawAt = 0;
uint8_t uiVerifyFramesRemaining = 0;

HmiCommand commandQueue[COMMAND_QUEUE_SIZE];
uint8_t commandHead = 0, commandTail = 0, commandCount = 0;
uint8_t commandOutstandingCount = 0;
uint32_t nextCommandId = 1;

struct ActiveCommandSlot {
  bool used = false;
  uint32_t takenAt = 0;
  HmiCommand command;
};
ActiveCommandSlot activeCommands[COMMAND_QUEUE_SIZE];

struct CommandAck {
  uint32_t commandId = 0;
  bool ok = false;
  char message[64] = "";
};
CommandAck commandAckQueue[COMMAND_ACK_QUEUE_SIZE];
uint8_t commandAckHead = 0, commandAckTail = 0, commandAckCount = 0;

struct ConfigAckInbox {
  bool pending = false;
  uint32_t transactionId = 0;
  bool ok = false;
  bool hasStoredConfig = false;
  MachineConfig storedConfig;
};

portMUX_TYPE hmiApiMux = portMUX_INITIALIZER_UNLOCKED;
MachineRuntime runtimeInbox;
MachineConfig configInbox;
HmiEventSnapshot eventLogInbox;
bool runtimeInboxPending = false;
bool configInboxPending = false;
bool eventLogInboxPending = false;
bool apiWorkPending = false;
char dateInbox[11] = "--/--/----";
bool dateInboxPending = false;
BuzzerCue cueInbox = BuzzerCue::None;
bool cueInboxPending = false;
uint32_t expiredAlarmAckMask = AlarmNone;
ConfigAckInbox configAckInbox;

bool apiHasPendingWork() {
  return __atomic_load_n(&apiWorkPending, __ATOMIC_ACQUIRE);
}

void markApiWorkPending() {
  __atomic_store_n(&apiWorkPending, true, __ATOMIC_RELEASE);
}

void setApiWorkPending(bool pending) {
  __atomic_store_n(&apiWorkPending, pending, __ATOMIC_RELEASE);
}

// Prototype thu cong de Arduino .ino preprocessor khong chen khai bao
// processConfigAck truoc dinh nghia ConfigAckInbox.
void processConfigAck(const ConfigAckInbox &ack);

HmiI2cLockFn i2cLockCallback = nullptr;
HmiI2cUnlockFn i2cUnlockCallback = nullptr;

// Menu chinh: CAI DAT ME, CAI DAT CHUNG, CHE DO THU NGHIEM, NHAT KY ME, THOAT.
constexpr uint8_t MAIN_COUNT = 5;
enum MainMenuIndex : uint8_t {
  MAIN_CAI_DAT_ME = 0, MAIN_CAI_DAT_CHUNG = 1, MAIN_THU_NGHIEM = 2,
  MAIN_NHAT_KY = 3, MAIN_THOAT = 4
};
const char *mainItemLabel(uint8_t index) {
  switch (index) {
    case MAIN_CAI_DAT_ME: return "CAI DAT ME";
    case MAIN_CAI_DAT_CHUNG: return "CAI DAT CHUNG";
    case MAIN_THU_NGHIEM: return "CHE DO THU NGHIEM";
    case MAIN_NHAT_KY: return "NHAT KY ME";
    default: return "THOAT";
  }
}

// Menu con "CAI DAT CHUNG": 4 thu muc setting (chi so nhom 1..4 trong GROUPS[])
// + TU CHINH PID (mo thang View::AutoTune) + THOAT.
constexpr uint8_t CHUNG_COUNT = (GROUP_COUNT - 1U) + 2U;
const char *chungItemLabel(uint8_t index) {
  if (index < GROUP_COUNT - 1U) return GROUPS[index + 1U].label;
  if (index == GROUP_COUNT - 1U) return "TU CHINH PID";
  return "THOAT";
}

bool groupHasExtraRow(uint8_t group) {
  const GroupExtra extra = groupExtra(group);
  if (extra == GroupExtra::None) return false;
  if (extra == GroupExtra::WifiChange) {
    return currentConfig.connectivityMode == ConnectivityMode::Online;
  }
  return true;
}
const char *groupExtraLabel(uint8_t group) {
  switch (groupExtra(group)) {
    case GroupExtra::TurnStats: return "So lan dao";
    case GroupExtra::WifiChange: return "Doi wifi";
    default: return "";
  }
}
bool settingLockedDuringBatch(uint8_t settingIndex) {
  if (!currentRuntime.batchRunning) return false;
  // Khoa cac tham so lam thay doi lich/chuyen dong dao khi me da bat dau.
  return settingIndex == 3U || settingIndex == 9U ||
         settingIndex == 10U || settingIndex == 11U;
}

uint8_t settingListItemCount(uint8_t group) {
  return static_cast<uint8_t>(
      GROUPS[group].count + (groupHasExtraRow(group) ? 1U : 0U) + 1U);
}

uint8_t settingListExitIndex(uint8_t group) {
  return static_cast<uint8_t>(
      GROUPS[group].count + (groupHasExtraRow(group) ? 1U : 0U));
}

bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void clearToast() {
  toastLine[0] = '\0';
  toastError = false;
  toastUntil = 0U;
}

void showToast(const char *text, bool error = false, uint32_t duration = 0) {
  clearToast();
  if (text && text[0]) {
    constexpr size_t maxChars = HMI_STATUS_TEXT_MAX_CHARS;
    const size_t length = strnlen(text, 64U);
    if (length <= maxChars) {
      memcpy(toastLine, text, length);
      toastLine[length] = '\0';
    } else {
      constexpr size_t body = maxChars - 3U;
      memcpy(toastLine, text, body);
      memcpy(toastLine + body, "...", 4U);
    }
  }
  toastError = error;
  toastUntil = millis() +
      (duration ? duration : (error ? TOAST_ERROR_MS : TOAST_INFO_MS));
  dirty = true;
}

bool confirmationActive() {
  return confirmAction != ConfirmAction::None;
}

void resetRotaryPending() {
  rotary.accumulator = 0;
  rotary.step = 0;
  rotary.button = ButtonEvent::None;
#if MAYAP_HMI_ENCODER_INTERRUPT
  portENTER_CRITICAL(&rotaryIsrMux);
  rotaryPendingTransitions = 0;
  portEXIT_CRITICAL(&rotaryIsrMux);
#endif
}

void armInputGuard(uint32_t durationMs = HMI_INPUT_GUARD_MS) {
  const uint32_t now = millis();
  inputGuardUntil = now + durationMs;
  // Hai frame giong nhau: frame dau chuyen trang, frame hai xac minh lai.
  // Khong tat/bat LCD nen khong tao nhay den.
  uiVerifyFramesRemaining = 2U;
  uiNextVerifyDrawAt = now;
  resetRotaryPending();
}

bool commandTypesConflict(HmiCommandType a, HmiCommandType b) {
  if (a == b) return true;
  const bool aBatch = a == HmiCommandType::BatchStart || a == HmiCommandType::BatchStop;
  const bool bBatch = b == HmiCommandType::BatchStart || b == HmiCommandType::BatchStop;
  if (aBatch && bBatch) return true;
  const bool aTune = a == HmiCommandType::AutoTuneStart;
  const bool bTune = b == HmiCommandType::AutoTuneStart;
  if ((aTune && bBatch) || (bTune && aBatch)) return true;
  const bool aResume = a == HmiCommandType::ResumeYes || a == HmiCommandType::ResumeNo;
  const bool bResume = b == HmiCommandType::ResumeYes || b == HmiCommandType::ResumeNo;
  if (aResume && bResume) return true;
  const bool aTest = a == HmiCommandType::TestModeEnter || a == HmiCommandType::TestModeExit ||
                     a == HmiCommandType::TestOutputPulse || a == HmiCommandType::TestLimitStart ||
                     a == HmiCommandType::TestLimitCancel;
  const bool bTest = b == HmiCommandType::TestModeEnter || b == HmiCommandType::TestModeExit ||
                     b == HmiCommandType::TestOutputPulse || b == HmiCommandType::TestLimitStart ||
                     b == HmiCommandType::TestLimitCancel;
  // Thu nghiem thiet bi khong duoc dan xen voi bat dau me/auto tune.
  return (aTest && (bBatch || bTune)) || (bTest && (aBatch || aTune));
}

bool commandConflictLocked(HmiCommandType type) {
  for (uint8_t i = 0, pos = commandHead; i < commandCount;
       ++i, pos = static_cast<uint8_t>((pos + 1U) % COMMAND_QUEUE_SIZE)) {
    if (commandTypesConflict(type, commandQueue[pos].type)) return true;
  }
  for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
    if (activeCommands[i].used &&
        commandTypesConflict(type, activeCommands[i].command.type)) return true;
  }
  return false;
}

bool queueCommand(HmiCommandType type,
                  uint16_t validForMs = COMMAND_DEFAULT_VALID_MS,
                  uint16_t actuatorLeaseMs = 0,
                  uint32_t alarmMask = AlarmNone,
                  uint32_t *commandId = nullptr) {
  bool full = false;
  bool duplicate = false;
  uint32_t id = 0;
  portENTER_CRITICAL(&hmiApiMux);
  full = commandOutstandingCount >= COMMAND_QUEUE_SIZE;
  duplicate = !full && commandConflictLocked(type);
  if (!full && !duplicate) {
    id = nextCommandId++;
    if (id == 0) id = nextCommandId++;
    commandQueue[commandTail] = {
      id, type, static_cast<uint32_t>(millis()), validForMs,
      actuatorLeaseMs, alarmMask
    };
    commandTail = static_cast<uint8_t>((commandTail + 1U) % COMMAND_QUEUE_SIZE);
    ++commandCount;
    ++commandOutstandingCount;
  }
  portEXIT_CRITICAL(&hmiApiMux);

  if (full || duplicate) {
    showToast(full ? "HE THONG DANG BAN" : "LENH DANG XU LY", true);
    buzzerPlayCue(BuzzerCue::Error);
    return false;
  }
  if (commandId) *commandId = id;
  return true;
}

void setListSelection(int value, uint8_t count) {
  if (count == 0) return;
  listIndex = static_cast<uint8_t>(constrain(value, 0, count - 1));
  if (listIndex < listTop) listTop = listIndex;
  if (listIndex >= listTop + 4) listTop = listIndex - 3;
  dirty = true;
}

void alignMainMenuWindow() {
  listTop = mainIndex >= 3 ? static_cast<uint8_t>(mainIndex - 3) : 0;
}
void alignChungMenuWindow() {
  chungTop = chungIndex >= 3 ? static_cast<uint8_t>(chungIndex - 3) : 0;
}

void goBack() {
  switch (view) {
    case View::Home: break;
    case View::MainMenu: view = View::Home; break;
    case View::ChungMenu:
      view = View::MainMenu;
      mainIndex = MAIN_CAI_DAT_CHUNG;
      alignMainMenuWindow();
      break;
    case View::SettingList:
      if (selectedGroup == 0U) {
        view = View::MainMenu;
        mainIndex = MAIN_CAI_DAT_ME;
        alignMainMenuWindow();
      } else {
        view = View::ChungMenu;
        chungIndex = static_cast<uint8_t>(selectedGroup - 1U);
        alignChungMenuWindow();
      }
      break;
    case View::EditSetting:
      view = View::SettingList;
      break;
    case View::TurnStats:
      view = View::SettingList;
      selectedGroup = 3U;  // DAO TRUNG
      setListSelection(GROUPS[3U].count, settingListItemCount(3U));
      break;
    case View::AutoTune:
      view = View::ChungMenu;
      chungIndex = GROUP_COUNT - 1U;  // "TU CHINH PID"
      alignChungMenuWindow();
      break;
    case View::EventLog:
      view = View::MainMenu;
      mainIndex = MAIN_NHAT_KY;
      alignMainMenuWindow();
      break;
    case View::TestMode:
      queueCommand(HmiCommandType::TestModeExit);
      view = View::MainMenu;
      mainIndex = MAIN_THU_NGHIEM;
      alignMainMenuWindow();
      break;
    case View::WifiChange:
      queueCommand(HmiCommandType::WifiPortalCancel);
      view = View::SettingList;
      selectedGroup = 4U;  // KET NOI
      setListSelection(GROUPS[4U].count, settingListItemCount(4U));
      break;
    case View::Alarm: view = alarmReturnView; break;
  }
  dirty = true;
}

void openGroup(uint8_t group) {
  if (configSave.active) {
    showToast("DANG CHO XAC NHAN LUU", true);
    return;
  }
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    showToast("AUTO TUNE DANG CHAY", true);
    return;
  }
  selectedGroup = group;
  listIndex = listTop = 0;
  view = View::SettingList;
  dirty = true;
}

void openChungMenu() {
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    showToast("AUTO TUNE DANG CHAY", true);
    return;
  }
  chungIndex = chungTop = 0;
  view = View::ChungMenu;
  dirty = true;
}

void selectChungItem() {
  if (chungIndex < GROUP_COUNT - 1U) {
    openGroup(static_cast<uint8_t>(chungIndex + 1U));
  } else if (chungIndex == GROUP_COUNT - 1U) {
    if (currentRuntime.batchRunning) {
      showToast("HAY DUNG ME TRUOC AUTO TUNE", true);
      return;
    }
    view = View::AutoTune;
    autoTuneRow = 0;
    dirty = true;
  } else {
    view = View::MainMenu;
    mainIndex = MAIN_CAI_DAT_CHUNG;
    alignMainMenuWindow();
    dirty = true;
  }
}

void openTestMode() {
  if (currentRuntime.batchRunning) {
    showToast("DANG CO ME - KHONG THU NGHIEM DUOC", true);
    return;
  }
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    showToast("AUTO TUNE DANG CHAY", true);
    return;
  }
  if (queueCommand(HmiCommandType::TestModeEnter)) {
    testModeLastCommandAt = millis();
    view = View::TestMode;
    listIndex = listTop = 0;
    dirty = true;
  }
}

void openWifiChange() {
  if (currentConfig.connectivityMode != ConnectivityMode::Online) {
    showToast("CHI DUNG DUOC KHI ONLINE", true);
    return;
  }
  if (queueCommand(HmiCommandType::WifiPortalStart)) {
    wifiPortalLastCommandAt = millis();
    view = View::WifiChange;
    dirty = true;
  }
}

void openSetting() {
  const SettingGroup &group = GROUPS[selectedGroup];
  editSettingIndex = GROUP_SETTING_INDEXES[group.first + listIndex];
  if (settingLockedDuringBatch(editSettingIndex)) {
    showToast("DANG AP - THONG SO BI KHOA", true);
    return;
  }
  if (configSave.active) {
    showToast("DANG LUU - VUI LONG CHO", true);
    return;
  }
  const SettingItem &item = SETTINGS[editSettingIndex];
  float minimum, maximum;
  settingLimits(currentConfig, item, minimum, maximum);
  editValue = constrain(readSetting(currentConfig, item), minimum, maximum);
  view = View::EditSetting;
  dirty = true;
}

void openBatchConfirm(View returnView) {
  // Dang chay me thi luon cho phep mo xac nhan DUNG ME, ke ca EEPROM dang
  // hoan tat giao dich luu. Chi chan thao tac BAT DAU me moi.
  if (configSave.active && !currentRuntime.batchRunning) {
    showToast("CHO LUU XONG TRUOC KHI CHAY ME", true);
    return;
  }
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    showToast("HAY CHO AUTO TUNE XONG", true);
    return;
  }
  confirmAction = ConfirmAction::BatchToggle;
  confirmReturnView = returnView;
  // Bat dau: mac dinh DONG Y de thao tac nhanh.
  // Dung me: mac dinh HUY de tranh dung nham chu trinh dang chay.
  confirmYes = !currentRuntime.batchRunning;
  clearToast();
  armInputGuard();
  dirty = true;
}


void openResumeConfirm() {
  confirmAction = ConfirmAction::ResumeBatch;
  confirmReturnView = View::Home;
  confirmYes = true;
  view = View::Home;
  homePage = 0;
  clearToast();
  armInputGuard();
  dirty = true;
}

void openAutoTuneConfirm() {
  if (configSave.active) {
    showToast("CHO LUU XONG TRUOC", true);
    return;
  }
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    showToast("AUTO TUNE DANG CHAY");
    return;
  }
  if (currentRuntime.batchRunning) {
    showToast("HAY DUNG ME TRUOC AUTO TUNE", true);
    return;
  }
  if (!currentRuntime.sensorOnline ||
      (currentRuntime.alarmMask & (AlarmSensor | AlarmEmergency | AlarmSystem))) {
    showToast("CAM BIEN/AN TOAN CHUA SAN SANG", true);
    return;
  }
  confirmAction = ConfirmAction::AutoTuneStart;
  confirmReturnView = View::AutoTune;
  confirmYes = false;
  clearToast();
  armInputGuard();
  dirty = true;
}

constexpr uint32_t ALARM_PRIORITY[] = {
  AlarmEmergency, AlarmSystem, AlarmAutoMode, AlarmSensor, AlarmTurning,
  AlarmTempHigh, AlarmTempLow, AlarmHumidityLow
};
constexpr uint8_t ALARM_PRIORITY_COUNT = sizeof(ALARM_PRIORITY) / sizeof(ALARM_PRIORITY[0]);

// ============================================================
// 3A. BO DIEU KHIEN COI KHONG BLOCKING
// ============================================================
void buzzerWrite(bool on) {
  buzzer.outputOn = on;
  digitalWrite(PIN_BUZZER, (on == BUZZER_ACTIVE_HIGH) ? HIGH : LOW);
}

void buzzerBegin() {
  // Dat muc OFF truoc khi OUTPUT de tranh mot xung keu luc khoi dong.
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  pinMode(PIN_BUZZER, OUTPUT);
  buzzerWrite(false);
}

BuzzerPattern buzzerCuePattern(BuzzerCue cue) {
  switch (cue) {
    // Coi la loai active (tu dao dong, khong chinh duoc cao do/am luong qua
    // PWM), nen cach duy nhat de "diu" tieng la rut ngan thoi gian xung. Key
    // vang o moi lan bam nen phai la tieng "tach" that nhe, khong phai tieng "bip".
    case BuzzerCue::Key:   return {7, 0, 1};       // Tieng nhan phim cuc ngan, diu
    case BuzzerCue::Save:  return {16, 0, 1};      // Bip rat ngan, khong gay on
    case BuzzerCue::Ok:    return {55, 70, 2};     // Hai bip ngan
    case BuzzerCue::Error: return {110, 100, 3};   // Ba bip de phan biet loi
    default:               return {0, 0, 0};
  }
}

BuzzerPattern alarmPattern(uint32_t bit) {
  switch (bit) {
    case AlarmEmergency:   return {900, 100, 0};   // Gan lien tuc, uu tien cao nhat
    case AlarmSystem:      return {600, 200, 0};
    case AlarmAutoMode:    return {300, 700, 0};
    case AlarmSensor:      return {450, 300, 0};
    case AlarmTurning:     return {400, 400, 0};
    case AlarmTempHigh:    return {350, 500, 0};
    case AlarmTempLow:     return {220, 900, 0};
    case AlarmHumidityLow: return {120, 2880, 0};  // Het nuoc: nhac nho thua
    default:               return {0, 0, 0};
  }
}

uint32_t highestUnacknowledgedAlarm() {
  const uint32_t pending = currentRuntime.alarmMask & ~buzzer.acknowledgedAlarmMask;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    if (pending & ALARM_PRIORITY[i]) return ALARM_PRIORITY[i];
  }
  return AlarmNone;
}

void buzzerStopTransient() {
  buzzer.transientActive = false;
  buzzer.transientOn = false;
  buzzer.transientMandatory = false;
  buzzer.transientPulsesLeft = 0;
}

void buzzerPlayCue(BuzzerCue cue) {
  // Bao dong va yeu cau phuc hoi co uu tien cao hon tieng UI.
  if (highestUnacknowledgedAlarm() != AlarmNone ||
      currentRuntime.resumeConfirmationRequired ||
      currentRuntime.turnState == TurnState::Left ||
      currentRuntime.turnState == TurnState::Right) return;
  const BuzzerPattern pattern = buzzerCuePattern(cue);
  if (!pattern.pulses) return;
  const bool mandatory = cue == BuzzerCue::Error;
  if (!currentConfig.alarmEnabled && !mandatory) return;
  buzzer.transientPattern = pattern;
  buzzer.transientPulsesLeft = pattern.pulses;
  buzzer.transientActive = true;
  buzzer.transientOn = true;
  buzzer.transientMandatory = mandatory;
  buzzer.deadline = millis() + pattern.onMs;
  buzzerWrite(true);
}

void buzzerAcknowledge(uint32_t alarmMask) {
  const uint32_t now = millis();
  alarmMask &= ALARM_KNOWN_MASK;
  buzzer.acknowledgedAlarmMask |= alarmMask;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    if (alarmMask & ALARM_PRIORITY[i]) buzzer.acknowledgedAt[i] = now;
  }
  buzzer.soundingAlarmBit = AlarmNone;
  buzzerStopTransient();
  buzzerWrite(false);
}

void buzzerUnacknowledge(uint32_t alarmMask) {
  buzzer.acknowledgedAlarmMask &= ~alarmMask;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    if (alarmMask & ALARM_PRIORITY[i]) buzzer.acknowledgedAt[i] = 0;
  }
  buzzer.soundingAlarmBit = AlarmNone;
}

uint32_t alarmRepeatMs(uint32_t bit) {
  if (bit == AlarmEmergency) return EMERGENCY_RESOUND_MS;
  if (bit == AlarmAutoMode) return AUTO_LOST_RESOUND_MS;
  if (bit == AlarmSystem || bit == AlarmSensor || bit == AlarmTurning || bit == AlarmTempHigh) {
    return CRITICAL_RESOUND_MS;
  }
  return 0;
}

void buzzerUpdate(uint32_t now) {
  // Bit da het loi tu dong mat ACK. Loi tai xuat hien se keu lai.
  buzzer.acknowledgedAlarmMask &= currentRuntime.alarmMask;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    const uint32_t bit = ALARM_PRIORITY[i];
    if (!(buzzer.acknowledgedAlarmMask & bit)) {
      buzzer.acknowledgedAt[i] = 0;
      continue;
    }
    const uint32_t repeatMs = alarmRepeatMs(bit);
    if (repeatMs && now - buzzer.acknowledgedAt[i] >= repeatMs) {
      buzzer.acknowledgedAlarmMask &= ~bit;
      buzzer.acknowledgedAt[i] = 0;
      // AUTO OFF trong me phai quay lai man Alarm moi 10 phut cho toi khi
      // dieu kien duoc khac phuc, khong chi keu coi o nen.
      if (bit == AlarmAutoMode) alarmPresentedMask &= ~AlarmAutoMode;
    }
  }

  const uint32_t alarmBit = highestUnacknowledgedAlarm();
  // Moi loi/canh bao moi deu phat am it nhat den khi nguoi dung ACK.
  // alarmEnabled chi tat cac bip giao dien thong thuong, khong che giau loi.
  if (alarmBit != AlarmNone) {
    buzzer.resumePromptActive = false;
    buzzer.turnPatternActive = false;
    buzzer.turnPhase = 0U;
    buzzerStopTransient();
    const BuzzerPattern pattern = alarmPattern(alarmBit);
    if (buzzer.soundingAlarmBit != alarmBit) {
      buzzer.soundingAlarmBit = alarmBit;
      buzzerWrite(true);
      buzzer.deadline = now + pattern.onMs;
      return;
    }
    if (static_cast<int32_t>(now - buzzer.deadline) < 0) return;
    if (buzzer.outputOn) {
      buzzerWrite(false);
      buzzer.deadline = now + pattern.offMs;
    } else {
      buzzerWrite(true);
      buzzer.deadline = now + pattern.onMs;
    }
    return;
  }

  buzzer.soundingAlarmBit = AlarmNone;

  // Mat dien giua me la yeu cau bat buoc nguoi dung ra quyet dinh.
  // Coi nhac lap lai den khi chon TIEP TUC hoac HUY ME.
  if (currentRuntime.resumeConfirmationRequired) {
    buzzer.turnPatternActive = false;
    buzzer.turnPhase = 0U;
    buzzerStopTransient();
    if (!buzzer.resumePromptActive) {
      buzzer.resumePromptActive = true;
      buzzerWrite(true);
      buzzer.deadline = now + RESUME_PROMPT_ON_MS;
      return;
    }
    if (static_cast<int32_t>(now - buzzer.deadline) < 0) return;
    if (buzzer.outputOn) {
      buzzerWrite(false);
      buzzer.deadline = now + RESUME_PROMPT_OFF_MS;
    } else {
      buzzerWrite(true);
      buzzer.deadline = now + RESUME_PROMPT_ON_MS;
    }
    return;
  }
  buzzer.resumePromptActive = false;

  // Khi motor dao dang chay, coi phu phat hai bip ngan theo chu ky.
  // Nhip nay de nhan biet dao, khong bi nham voi canh bao va khong keu lien.
  const bool turningNow = currentRuntime.turnState == TurnState::Left ||
                          currentRuntime.turnState == TurnState::Right;
  if (turningNow) {
    buzzerStopTransient();
    if (!buzzer.turnPatternActive) {
      buzzer.turnPatternActive = true;
      buzzer.turnPhase = 0U;
      buzzerWrite(true);
      buzzer.deadline = now + TURN_BUZZER_ON1_MS;
      return;
    }
    if (static_cast<int32_t>(now - buzzer.deadline) < 0) return;
    switch (buzzer.turnPhase) {
      case 0U:
        buzzerWrite(false);
        buzzer.turnPhase = 1U;
        buzzer.deadline = now + TURN_BUZZER_GAP_MS;
        break;
      case 1U:
        buzzerWrite(true);
        buzzer.turnPhase = 2U;
        buzzer.deadline = now + TURN_BUZZER_ON2_MS;
        break;
      case 2U:
        buzzerWrite(false);
        buzzer.turnPhase = 3U;
        buzzer.deadline = now + TURN_BUZZER_PAUSE_MS;
        break;
      default:
        buzzerWrite(true);
        buzzer.turnPhase = 0U;
        buzzer.deadline = now + TURN_BUZZER_ON1_MS;
        break;
    }
    return;
  }
  if (buzzer.turnPatternActive) {
    buzzer.turnPatternActive = false;
    buzzer.turnPhase = 0U;
    buzzerWrite(false);
  }

  if (!buzzer.transientActive ||
      (!currentConfig.alarmEnabled && !buzzer.transientMandatory)) {
    buzzerStopTransient();
    buzzerWrite(false);
    return;
  }

  if (static_cast<int32_t>(now - buzzer.deadline) < 0) return;
  if (buzzer.transientOn) {
    buzzerWrite(false);
    buzzer.transientOn = false;
    if (buzzer.transientPulsesLeft > 0) --buzzer.transientPulsesLeft;
    if (!buzzer.transientPulsesLeft) {
      buzzerStopTransient();
      return;
    }
    buzzer.deadline = now + buzzer.transientPattern.offMs;
  } else {
    buzzerWrite(true);
    buzzer.transientOn = true;
    buzzer.deadline = now + buzzer.transientPattern.onMs;
  }
}

void openAlarmView(View returnView) {
  if (!currentRuntime.activeFaultDisplayCount) return;
  alarmReturnView = returnView == View::Alarm ? View::Home : returnView;
  alarmIndex = 0;
  view = View::Alarm;
  dirty = true;
}

bool startConfigSave(const MachineConfig &candidate) {
  if (configSave.active) {
    showToast("DANG CHO XAC NHAN LUU", true);
    return false;
  }
  uint32_t id = nextConfigTransactionId++;
  if (id == 0) id = nextConfigTransactionId++;
  portENTER_CRITICAL(&hmiApiMux);
  configSave.active = true;
  configSave.readyForHost = true;
  configSave.id = id;
  configSave.startedAt = millis();
  configSave.rollback = currentConfig;
  configSave.candidate = candidate;
  portEXIT_CRITICAL(&hmiApiMux);
  return true;
}

void commitSetting() {
  if (settingLockedDuringBatch(editSettingIndex)) {
    view = View::SettingList;
    showToast("DANG AP - KHONG DUOC DOI", true);
    return;
  }
  MachineConfig candidate = currentConfig;
  const SettingItem &item = SETTINGS[editSettingIndex];
  float minimum, maximum;
  settingLimits(currentConfig, item, minimum, maximum);
  const float oldTarget = candidate.targetTemp;
  writeSetting(candidate, item, constrain(editValue, minimum, maximum));
  if (currentRuntime.batchRunning &&
      item.offset == offsetof(MachineConfig, connectivityMode) &&
      currentConfig.connectivityMode == ConnectivityMode::Offline &&
      candidate.connectivityMode == ConnectivityMode::Online) {
    view = View::SettingList;
    showToast("DANG AP - KHONG BAT ONLINE", true);
    return;
  }
  if (item.offset == offsetof(MachineConfig, targetTemp)) {
    // Khi doi SV, giu nguyen khoang cach cac nguong so voi SV.
    // Nguoi van hanh khong phai chinh lai tung muc lien quan.
    const float delta = candidate.targetTemp - oldTarget;
    candidate.lowTempAlarm += delta;
    candidate.highTempAlarm += delta;
    candidate.emergencyTemp += delta;
    candidate.ventOnTemp += delta;
    candidate.ventOffTemp += delta;
  }
  sanitizeConfig(candidate);

  const float oldValue = readSetting(currentConfig, item);
  const float newValue = readSetting(candidate, item);
  view = View::SettingList;
  if (fabsf(oldValue - newValue) < 0.0001f) {
    showToast("GIA TRI KHONG DOI");
    return;
  }
  if (!startConfigSave(candidate)) return;
  currentConfig = candidate;
  showToast("DANG LUU...");
}

void exitSettingGroup() {
  if (selectedGroup == 0U) {
    view = View::MainMenu;
    mainIndex = MAIN_CAI_DAT_ME;
    alignMainMenuWindow();
  } else {
    view = View::ChungMenu;
    chungIndex = static_cast<uint8_t>(selectedGroup - 1U);
    alignChungMenuWindow();
  }
  dirty = true;
}

bool requestAlarmAcknowledge() {
  const uint32_t snapshot = currentRuntime.alarmMask & ALARM_KNOWN_MASK;
  if (!snapshot) {
    showToast("KHONG CO CANH BAO");
    return false;
  }
  uint32_t commandId = 0;
  if (!queueCommand(HmiCommandType::AlarmAck, COMMAND_DEFAULT_VALID_MS,
                    0, snapshot, &commandId)) return false;
  buzzerAcknowledge(snapshot);
  showToast("DA TAT COI - LOI VAN CON");
  return true;
}

void selectMainItem() {
  switch (mainIndex) {
    case MAIN_CAI_DAT_ME: openGroup(0U); break;
    case MAIN_CAI_DAT_CHUNG: openChungMenu(); break;
    case MAIN_THU_NGHIEM: openTestMode(); break;
    case MAIN_NHAT_KY:
      eventLogIndex = 0U;
      view = View::EventLog;
      break;
    default:
      view = View::Home;
      homePage = 0;
      break;
  }
  dirty = true;
}

// Quy tac cho nhan ngan/nhan giu tai ca hai trang Home (MAY AP va TRANG THAI):
// 1) Dang co loi + nhan ngan: mo trang chi tiet loi (nhu cu).
// 2) Dang co loi + nhan giu: duong thoat khan cap - mo thang xac nhan
//    KET THUC ME, ke ca loi chua het han/chua tu xoa duoc. Khong the bam
//    nham (van phai xac nhan CO/HUY), nhung khong con bi "ket" trong vong
//    ACK lien tuc khong loi ra duoc.
// 3) Khong co loi, trang MAY AP: mo xac nhan Bat/Dung me (nhu cu).
// 4) Khong co loi, trang TRANG THAI: mo Menu chinh (nhu cu).
void activateHomeContext(bool longPress) {
  if (currentRuntime.activeFaultDisplayCount) {
    if (longPress) {
      openBatchConfirm(View::Home);
    } else {
      openAlarmView(View::Home);
    }
  } else if (homePage == 0U) {
    openBatchConfirm(View::Home);
  } else {
    view = View::MainMenu;
    mainIndex = 0U;
    listTop = 0U;
    dirty = true;
  }
}


void stabilizeViewTransition() {
  static View lastInputView = View::Home;
  if (view == lastInputView) return;
  lastInputView = view;
  armInputGuard();
  dirty = true;
}

void executeConfirmation(bool accepted) {
  const ConfirmAction action = confirmAction;
  const View returnView = confirmReturnView;
  // Xoa trang thai xac nhan truoc khi hien thong bao o thanh day.
  confirmAction = ConfirmAction::None;

  if (accepted) {
    if (action == ConfirmAction::BatchToggle) {
      if (queueCommand(currentRuntime.batchRunning ?
                       HmiCommandType::BatchStop : HmiCommandType::BatchStart)) {
        showToast("DANG KIEM TRA DIEU KIEN");
      }
      view = View::Home;
      homePage = 0;
    } else if (action == ConfirmAction::AutoTuneStart) {
      if (queueCommand(HmiCommandType::AutoTuneStart,
                       COMMAND_AUTOTUNE_VALID_MS)) {
        showToast("DANG KHOI DONG AUTO TUNE");
      }
      view = View::AutoTune;
    } else if (action == ConfirmAction::ResumeBatch) {
      if (queueCommand(HmiCommandType::ResumeYes)) {
        resumeDecisionSubmitted = true;
        showToast("DANG PHUC HOI ME CU");
      }
      view = View::Home;
      homePage = 0;
    }
  } else {
    if (action == ConfirmAction::ResumeBatch) {
      if (queueCommand(HmiCommandType::ResumeNo)) {
        resumeDecisionSubmitted = true;
        showToast("DA HUY ME CU");
      }
      view = View::Home;
      homePage = 0;
    } else {
      view = returnView;
    }
  }
  armInputGuard();
  dirty = true;
}

bool handleInlineConfirmation() {
  if (!confirmationActive() || view == View::Alarm) return false;

  if (rotary.step) {
    confirmYes = !confirmYes;
    dirty = true;
  }
  if (rotary.button == ButtonEvent::ShortPress) {
    buzzerPlayCue(BuzzerCue::Key);
    executeConfirmation(confirmYes);
  } else if (rotary.button == ButtonEvent::LongPress) {
    buzzerPlayCue(BuzzerCue::Key);
    // Phuc hoi sau mat dien bat buoc phai chon CO/KHONG, khong cho bo qua.
    if (confirmAction != ConfirmAction::ResumeBatch) {
      executeConfirmation(false);
    }
  }
  return true;
}

void handleInput() {
  const uint32_t now = millis();
  if (!timeReached(now, inputGuardUntil)) {
    resetRotaryPending();
    return;
  }

  if (handleInlineConfirmation()) return;

  if (rotary.button == ButtonEvent::ShortPress ||
      rotary.button == ButtonEvent::LongPress) {
    buzzerPlayCue(BuzzerCue::Key);
  }

  if (rotary.button == ButtonEvent::LongPress) {
    if (view == View::Home) {
      // Nhan giu tren Home: neu dang co loi thi la duong thoat KET THUC ME,
      // khac voi nhan ngan (mo trang loi). Khong loi thi hanh vi nhu cu.
      activateHomeContext(true);
      return;
    }
    if (view == View::SettingList) {
      exitSettingGroup();
      return;
    }
    goBack();
    return;
  }

  switch (view) {
    case View::Home:
      if (rotary.step) {
        int p = static_cast<int>(homePage) + rotary.step;
        if (p < 0) p = 1;
        if (p > 1) p = 0;
        homePage = static_cast<uint8_t>(p);
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        activateHomeContext(false);
      }
      break;

    case View::MainMenu:
      if (rotary.step) {
        mainIndex = static_cast<uint8_t>(constrain(static_cast<int>(mainIndex) + rotary.step, 0, MAIN_COUNT - 1));
        if (mainIndex < listTop) listTop = mainIndex;
        if (mainIndex >= listTop + 4) listTop = mainIndex - 3;
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) selectMainItem();
      break;

    case View::SettingList: {
      const uint8_t itemCount = settingListItemCount(selectedGroup);
      if (rotary.step) {
        setListSelection(static_cast<int>(listIndex) + rotary.step, itemCount);
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        const SettingGroup &group = GROUPS[selectedGroup];
        if (listIndex < group.count) {
          openSetting();
        } else if (groupHasExtraRow(selectedGroup) && listIndex == group.count) {
          const GroupExtra extra = groupExtra(selectedGroup);
          if (extra == GroupExtra::TurnStats) {
            view = View::TurnStats;
            dirty = true;
          } else if (extra == GroupExtra::WifiChange) {
            openWifiChange();
          }
        } else {
          exitSettingGroup();
        }
      }
      break;
    }

    case View::ChungMenu:
      if (rotary.step) {
        chungIndex = static_cast<uint8_t>(constrain(
            static_cast<int>(chungIndex) + rotary.step, 0, CHUNG_COUNT - 1));
        if (chungIndex < chungTop) chungTop = chungIndex;
        if (chungIndex >= chungTop + 4) chungTop = chungIndex - 3;
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) selectChungItem();
      break;

    case View::EditSetting: {
      const SettingItem &item = SETTINGS[editSettingIndex];
      if (rotary.step) {
        float minimum, maximum;
        settingLimits(currentConfig, item, minimum, maximum);
        editValue = constrain(editValue + rotary.step * item.step, minimum, maximum);
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) commitSetting();
      break;
    }

    case View::TurnStats:
      if (rotary.button == ButtonEvent::ShortPress) goBack();
      break;

    case View::AutoTune:
      // Cuon xuong chon "THOAT" (hang 1) de thoat an toan; hang 0 la thao tac
      // bat dau tu chinh, khong bi bam nham khi chi luot qua man hinh.
      if (rotary.step) {
        autoTuneRow = static_cast<uint8_t>(constrain(
            static_cast<int>(autoTuneRow) + rotary.step, 0, 1));
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        if (autoTuneRow == 0U) openAutoTuneConfirm();
        else goBack();
      }
      break;

    case View::TestMode: {
      if (rotary.step) {
        setListSelection(static_cast<int>(listIndex) + rotary.step,
                         TEST_MODE_ITEM_COUNT);
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        if (listIndex < TEST_MODE_OUTPUT_ROWS) {
          queueCommand(HmiCommandType::TestOutputPulse, COMMAND_DEFAULT_VALID_MS,
                      0, static_cast<uint32_t>(listIndex));
          testModeLastCommandAt = millis();
        } else if (listIndex < TEST_MODE_OUTPUT_ROWS + TEST_MODE_LIMIT_ROWS) {
          testLimitSelected = (listIndex == TEST_MODE_OUTPUT_ROWS)
              ? TestLimitId::Left : TestLimitId::Right;
          queueCommand(HmiCommandType::TestLimitStart, COMMAND_DEFAULT_VALID_MS,
                      0, static_cast<uint32_t>(testLimitSelected));
          testModeLastCommandAt = millis();
        } else {
          goBack();
        }
      }
      break;
    }

    case View::WifiChange:
      if (rotary.button == ButtonEvent::ShortPress) goBack();
      break;

    case View::EventLog: {
      const uint8_t count = currentEventLog.count;
      if (count && rotary.step) {
        int next = static_cast<int>(eventLogIndex) + rotary.step;
        while (next < 0) next += count;
        while (next >= count) next -= count;
        eventLogIndex = static_cast<uint8_t>(next);
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) goBack();
      break;
    }

    case View::Alarm: {
      const uint8_t count = currentRuntime.activeFaultDisplayCount;
      if (!count) {
        view = alarmReturnView;
        dirty = true;
        break;
      }
      if (rotary.step) {
        int next = static_cast<int>(alarmIndex) + rotary.step;
        while (next < 0) next += count;
        while (next >= count) next -= count;
        alarmIndex = static_cast<uint8_t>(next);
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        if (requestAlarmAcknowledge()) view = alarmReturnView;
      }
      break;
    }
  }
}

// ============================================================
// 6. VE GIAO DIEN
// ============================================================
void drawHeader(const char *title) {
  lcd.setDrawColor(1);
  lcd.setFont(u8g2_font_5x8_tf);
  char shortTitle[14];
  snprintf(shortTitle, sizeof(shortTitle), "%.13s", title ? title : "");
  lcd.drawStr(0, 8, shortTitle);
  const char *date = currentRuntime.dateText[0] ? currentRuntime.dateText : "--/--/----";
  const int16_t dateX = max(0, 127 - static_cast<int16_t>(lcd.getStrWidth(date)));
  if (currentRuntime.alarmMask) {
    lcd.drawBox(67, 0, 8, 8);
    lcd.setDrawColor(0);
    lcd.drawStr(69, 8, "!");
    lcd.setDrawColor(1);
  }
  lcd.drawStr(dateX, 8, date);
  lcd.drawHLine(0, 10, 128);
}

uint32_t alarmBitForFaultCode(uint16_t code) {
  switch (code) {
    case 101: case 102: case 103: return AlarmSensor;
    case 110: return AlarmTempLow;
    case 111: return AlarmTempHigh;
    case 112: return AlarmEmergency;
    case 120: return AlarmHumidityLow;
    case 130: case 131: case 132: return AlarmSystem;
    case 133: return AlarmAutoMode;
    case 134: return AlarmTurning;
    case 201: case 202: case 203: case 204: return AlarmTurning;
    case 301: case 302: case 303: case 304: case 305: case 306:
    case 313: case 314: case 315: return AlarmSystem;
    default: return AlarmSystem;
  }
}

const char *faultTitle(uint16_t code) {
  switch (code) {
    case 101: return "MAT CAM BIEN";
    case 102: return "CAM BIEN SAI";
    case 103: return "CAM BIEN BAT THUONG";
    case 110: return "NHIET DO THAP";
    case 111: return "NHIET DO CAO";
    case 112: return "QUA NHIET KHAN";
    case 120: return "DO AM THAP";
    case 130: return "TAT CONG TAC NHIET";
    case 131: return "QUAT TUAN HOAN OFF";
    case 132: return "CAN CHUYEN SANG AUTO";
    case 133: return "AUTO BI TAT GIUA ME";
    case 134: return "TU DONG DAO BI TAT";
    case 201: return "LOI 2 HANH TRINH";
    case 202: return "DAO QUA THOI GIAN";
    case 203: return "HANH TRINH BI KET";
    case 204: return "XUNG DOT LENH DAO";
    case 301: return "MAT EEPROM";
    case 302: return "EEPROM SUY GIAM";
    case 303: return "RESET BAT THUONG";
    case 304: return "XUNG DOT OUTPUT";
    case 305: return "RELAY QUA NHIEU";
    case 306: return "LOI RTC";
    case 313: return "CHUA XOA DU LIEU ME";
    case 314: return "LOI NHAT KY AN TOAN";
    case 315: return "MAT NHAT KY ME";
    default: return "LOI KHONG XAC DINH";
  }
}

void faultDetail(const HmiFaultItem &fault, char *out, size_t size) {
  switch (fault.code) {
    case 101: snprintf(out, size, "KHONG CO DU LIEU RS485"); break;
    case 102: snprintf(out, size, "DU LIEU NGOAI PHAM VI"); break;
    case 103: snprintf(out, size, "MAU NGHI NGO %.1fC", fault.detail * 0.1f); break;
    case 110: snprintf(out, size, "PV %.1f < %.1fC", currentRuntime.temperature,
                       currentConfig.lowTempAlarm); break;
    case 111: snprintf(out, size, "PV %.1f > %.1fC", fault.detail * 0.1f,
                       currentConfig.highTempAlarm); break;
    case 112: snprintf(out, size, "PV %.1f > %.1fC", fault.detail * 0.1f,
                       currentConfig.emergencyTemp); break;
    case 120: snprintf(out, size, "AM %.0f%% < %.0f%%", currentRuntime.humidity,
                       currentConfig.lowHumidityAlarm); break;
    case 130: snprintf(out, size, "NHIET DA BI KHOA"); break;
    case 131: snprintf(out, size, "MANUAL: HAY BAT QUAT"); break;
    case 132: snprintf(out, size, "PHUC HOI ME DANG CHO"); break;
    case 133: snprintf(out, size, "BAT LAI AUTO - DAO DANG KHOA"); break;
    case 134: snprintf(out, size, "DAO TU DONG PHAI LUON ON"); break;
    case 201: snprintf(out, size, "HAI CTHT CUNG TAC DONG"); break;
    case 202: snprintf(out, size, "CHUA CHAM CTHT DICH"); break;
    case 203: snprintf(out, size, "CTHT GOC KHONG NHA"); break;
    case 204: snprintf(out, size, "TRAI VA PHAI CUNG ON"); break;
    case 301: snprintf(out, size, "KHONG DOC/GHI DUOC"); break;
    case 302: snprintf(out, size, "DANG CHAY BANG RAM"); break;
    case 303: snprintf(out, size, "KIEM TRA RESET/WDT"); break;
    case 304: snprintf(out, size, "DA CAT OUTPUT AN TOAN"); break;
    case 305: snprintf(out, size, "%d LAN DONG CAT/GIO", fault.detail); break;
    case 306:
      if (fault.detail == 1) snprintf(out, size, "RTC OSF/THOI GIAN SAI");
      else if (fault.detail == 2) snprintf(out, size, "MAT KET NOI RTC 0x68");
      else snprintf(out, size, "RTC KHONG TANG GIAY");
      break;
    case 313: snprintf(out, size, "DANG THU LAI EEPROM"); break;
    case 314: snprintf(out, size, "NVS NOI BO KHONG SAN SANG"); break;
    case 315: snprintf(out, size, "FLASH LOG KHONG GHI DUOC"); break;
    default: snprintf(out, size, "CHI TIET %d", fault.detail); break;
  }
}

void drawAlarm() {
  const uint8_t count = currentRuntime.activeFaultDisplayCount;
  if (!count) {
    drawHeader("CANH BAO");
    lcd.setFont(u8g2_font_6x12_tf);
    lcd.drawStr(22, 36, "KHONG CO CANH BAO");
    return;
  }
  if (alarmIndex >= count) alarmIndex = 0;
  const HmiFaultItem &fault = currentRuntime.activeFaults[alarmIndex];
  char detail[30];
  char footer[40];
  drawHeader("CANH BAO");
  lcd.setFont(u8g2_font_helvB10_tf);
  const char *title = faultTitle(fault.code);
  const int16_t titleX = max(0, (128 - static_cast<int16_t>(lcd.getStrWidth(title))) / 2);
  lcd.drawStr(titleX, 29, title);
  faultDetail(fault, detail, sizeof(detail));
  lcd.setFont(u8g2_font_5x8_tf);
  const int16_t detailX = max(0, (128 - static_cast<int16_t>(lcd.getStrWidth(detail))) / 2);
  lcd.drawStr(detailX, 42, detail);
  snprintf(footer, sizeof(footer), "E%03u %u/%u  NHAN=ACK",
           fault.code, alarmIndex + 1U, count);
  lcd.setFont(u8g2_font_5x8_tf);
  lcd.drawStr(1, 61, footer);
}

void drawCenteredText(int16_t y, const char *text) {
  if (!text) text = "";
  const int16_t width = static_cast<int16_t>(lcd.getStrWidth(text));
  lcd.drawStr(max(0, (128 - width) / 2), y, text);
}

void drawHomeMain() {
  char text[32];
  drawHeader("MAY AP");

  // Man tong quan: PV la thong tin uu tien so 1. Chia man hinh thanh
  // vung PV ben trai (~70 px) va 3 dong thong tin phu ben phai.
  // Khong dung khung/duong ke de tiet kiem diem anh va giu giao dien thoang.
  constexpr int16_t TEMP_ZONE_WIDTH = 70;
  // Dich cot SV/AM/DAO ra gan mep phai hon de khong bi dinh sat vung nhiet do,
  // giup hai ben man hinh can doi hon.
  constexpr int16_t RIGHT_X = 80;

  snprintf(text, sizeof(text), currentRuntime.sensorOnline ? "%.1f" : "--.-",
           currentRuntime.temperature);
  // Uu tien font 26 px; neu gia tri bat thuong dai hon vung PV thi tu dong
  // ha font de khong cat mat ky tu o bien man hinh.
  lcd.setFont(u8g2_font_logisoso26_tn);
  int16_t tempWidth = static_cast<int16_t>(lcd.getStrWidth(text));
  if (tempWidth > TEMP_ZONE_WIDTH) {
    lcd.setFont(u8g2_font_logisoso24_tn);
    tempWidth = static_cast<int16_t>(lcd.getStrWidth(text));
  }
  if (tempWidth > TEMP_ZONE_WIDTH) {
    lcd.setFont(u8g2_font_logisoso22_tn);
    tempWidth = static_cast<int16_t>(lcd.getStrWidth(text));
  }
  const int16_t tempX = max(0, (TEMP_ZONE_WIDTH - tempWidth) / 2);
  lcd.drawStr(tempX, 40, text);

  // Ba mang thong tin phu duoc dan deu theo chieu doc: SV / AM / DAO TIEP.
  lcd.setFont(u8g2_font_6x12_tf);
  snprintf(text, sizeof(text), "SV %.1fC", currentConfig.targetTemp);
  lcd.drawStr(RIGHT_X, 20, text);
  snprintf(text, sizeof(text), currentRuntime.sensorOnline ? "AM %.0f%%" : "AM --%%",
           currentRuntime.humidity);
  lcd.drawStr(RIGHT_X, 34, text);
  if (currentRuntime.turningLockdown) {
    snprintf(text, sizeof(text), "DAO KHOA");
  } else if (currentRuntime.nextTurnScheduled) {
    snprintf(text, sizeof(text), "DAO %up", currentRuntime.nextTurnMinutes);
  } else {
    snprintf(text, sizeof(text), "DAO --");
  }
  lcd.drawStr(RIGHT_X, 48, text);

  // Ngay ap nam rieng duoi vung PV. Trang thai may nam mot dong rieng,
  // can giua o day man hinh de khong bi "dinh" vao thong tin ngay ap.
  lcd.setFont(u8g2_font_5x8_tf);
  snprintf(text, sizeof(text), "NGAY %u/%u", currentRuntime.currentDay,
           currentConfig.totalIncubationDays);
  const int16_t dayWidth = static_cast<int16_t>(lcd.getStrWidth(text));
  lcd.drawStr(max(0, (TEMP_ZONE_WIDTH - dayWidth) / 2), 52, text);

  if (currentRuntime.activeFaultCount) {
    snprintf(text, sizeof(text), "!E%03u %.18s",
             currentRuntime.primaryFaultCode, currentRuntime.machineState);
  } else {
    snprintf(text, sizeof(text), "%.25s", currentRuntime.machineState);
  }
  drawCenteredText(63, text);
}

const char *turnStateShort(TurnState state) {
  switch (state) {
    case TurnState::Left: return "TRAI";
    case TurnState::Right: return "PHAI";
    case TurnState::Waiting: return "CHO";
    case TurnState::Fault: return "LOI";
    default: return "DUNG";
  }
}

void drawStatusPair(int16_t y, const char *left, const char *right) {
  lcd.drawStr(1, y, left ? left : "");
  if (!right) return;
  const int16_t rightWidth = static_cast<int16_t>(lcd.getStrWidth(right));
  lcd.drawStr(max(1, 127 - rightWidth), y, right);
}

// Cot phai co vi tri X co dinh (khong phu thuoc do dai "ON"/"OFF"), de OUT
// va HUT luon thang cot voi nhau giua cac lan cap nhat.
constexpr int16_t STATUS_RIGHT_COLUMN_X = 68;
void drawStatusPairFixed(int16_t y, const char *left, const char *right) {
  lcd.drawStr(1, y, left ? left : "");
  if (right) lcd.drawStr(STATUS_RIGHT_COLUMN_X, y, right);
}

void drawHomeOutputs() {
  char left[24];
  char right[24];
  char turnLine[28];
  drawHeader("TRANG THAI");

  // Rut gon con 2 hang thiet bi (SSR/OUT, QUAT/HUT) + 1 dong DAO TRUNG lam
  // trong tam duoi day, trinh bay nhu mot chi bao trang thai giong man chinh.
  lcd.setFont(u8g2_font_6x12_tf);
  snprintf(left, sizeof(left), "SSR %3.0f%%", currentRuntime.heaterPower);
  snprintf(right, sizeof(right), "OUT %s", currentRuntime.heaterOn ? "ON" : "OFF");
  drawStatusPairFixed(24, left, right);

  snprintf(left, sizeof(left), "QUAT %s", currentRuntime.circulationFanOn ? "ON" : "OFF");
  snprintf(right, sizeof(right), "HUT %s", currentRuntime.ventFanOn ? "ON" : "OFF");
  drawStatusPairFixed(38, left, right);

  // Duong ngan chi bang nua chieu rong man hinh, can giua, thay cho thanh
  // ngang day duoi truoc day.
  lcd.drawHLine(32, 44, 64);

  if (currentRuntime.turningLockdown) {
    snprintf(turnLine, sizeof(turnLine), "DAO TRUNG: KHOA");
  } else if (currentRuntime.turnState == TurnState::Left ||
            currentRuntime.turnState == TurnState::Right) {
    snprintf(turnLine, sizeof(turnLine), "DAO TRUNG: %s",
             turnStateShort(currentRuntime.turnState));
  } else if (currentRuntime.turnState == TurnState::Fault) {
    snprintf(turnLine, sizeof(turnLine), "DAO TRUNG: LOI");
  } else if (currentRuntime.nextTurnScheduled) {
    snprintf(turnLine, sizeof(turnLine), "DAO TRUNG: %u PHUT",
             currentRuntime.nextTurnMinutes);
  } else {
    snprintf(turnLine, sizeof(turnLine), "DAO TRUNG: --");
  }
  lcd.setFont(u8g2_font_6x12_tf);
  drawCenteredText(58, turnLine);
}

void drawHome() {
  if (homePage == 0) drawHomeMain();
  else drawHomeOutputs();
}

void drawMainMenu() {
  drawHeader("MENU CHINH");
  lcd.setFont(u8g2_font_6x12_tf);
  for (uint8_t row = 0; row < 4 && listTop + row < MAIN_COUNT; ++row) {
    const uint8_t index = listTop + row;
    const int16_t y = 22 + row * 12;
    if (index == mainIndex) {
      lcd.drawBox(0, y - 9, 128, 11);
      lcd.setDrawColor(0);
      lcd.drawStr(2, y, ">");
      lcd.drawStr(12, y, mainItemLabel(index));
      lcd.setDrawColor(1);
    } else {
      lcd.drawStr(12, y, mainItemLabel(index));
    }
  }
}

void drawChungMenu() {
  drawHeader("CAI DAT CHUNG");
  lcd.setFont(u8g2_font_6x12_tf);
  for (uint8_t row = 0; row < 4 && chungTop + row < CHUNG_COUNT; ++row) {
    const uint8_t index = chungTop + row;
    const int16_t y = 22 + row * 12;
    if (index == chungIndex) {
      lcd.drawBox(0, y - 9, 128, 11);
      lcd.setDrawColor(0);
      lcd.drawStr(2, y, ">");
      lcd.drawStr(12, y, chungItemLabel(index));
      lcd.setDrawColor(1);
    } else {
      lcd.drawStr(12, y, chungItemLabel(index));
    }
  }
}

void drawSettingList() {
  const SettingGroup &group = GROUPS[selectedGroup];
  const uint8_t itemCount = settingListItemCount(selectedGroup);
  const uint8_t exitIndex = settingListExitIndex(selectedGroup);
  drawHeader(group.label);
  lcd.setFont(u8g2_font_6x12_tf);
  char value[18];
  for (uint8_t row = 0; row < 4 && listTop + row < itemCount; ++row) {
    const uint8_t local = listTop + row;
    const int16_t y = 22 + row * 12;
    const bool selected = local == listIndex;
    if (selected) {
      lcd.drawBox(0, y - 9, 128, 11);
      lcd.setDrawColor(0);
    }

    if (local < group.count) {
      const uint8_t settingIndex = GROUP_SETTING_INDEXES[group.first + local];
      const SettingItem &item = SETTINGS[settingIndex];
      if (settingLockedDuringBatch(settingIndex)) snprintf(value, sizeof(value), "KHOA");
      else formatSettingValue(item, readSetting(currentConfig, item), value, sizeof(value));
      lcd.drawStr(2, y, item.label);
      const int16_t x = max(86, 126 - static_cast<int16_t>(lcd.getStrWidth(value)));
      lcd.drawStr(x, y, value);
    } else if (groupHasExtraRow(selectedGroup) && local == group.count) {
      // Dong muc phu (thong ke dao/doi wifi) can trai dong bo voi cac muc cai dat.
      lcd.drawStr(2, y, groupExtraLabel(selectedGroup));
    } else if (local == exitIndex) {
      // Nut thoat cuon xuong cuoi danh sach, can trai nhu cac dong con lai.
      lcd.drawStr(2, y, "Thoat");
    }

    if (selected) lcd.setDrawColor(1);
  }
}

void drawTurnStats() {
  char text[28];
  drawHeader("SO LAN DAO");
  lcd.setFont(u8g2_font_6x12_tf);

  snprintf(text, sizeof(text), "HOM NAY: %u LAN",
           currentRuntime.turnCountToday);
  lcd.drawStr(6, 29, text);

  snprintf(text, sizeof(text), "TONG ME: %lu LAN",
           static_cast<unsigned long>(currentRuntime.turnCountBatch));
  lcd.drawStr(6, 43, text);
  if (currentRuntime.turningLockdown) snprintf(text, sizeof(text), "DAO TIEP: KHOA");
  else if (currentRuntime.nextTurnScheduled) snprintf(text, sizeof(text), "DAO TIEP: %u PH", currentRuntime.nextTurnMinutes);
  else snprintf(text, sizeof(text), "DAO TIEP: --");
  lcd.setFont(u8g2_font_5x8_tf);
  lcd.drawStr(6, 58, text);
  lcd.drawStr(max(1, 128 - static_cast<int16_t>(lcd.getStrWidth("NHAN=THOAT"))),
             58, "NHAN=THOAT");
}

void drawEditSetting() {
  const SettingItem &item = SETTINGS[editSettingIndex];
  char value[24];
  drawHeader("CHINH THONG SO");
  lcd.setFont(u8g2_font_6x12_tf);
  const int16_t labelX = max(0, (128 - static_cast<int16_t>(lcd.getStrWidth(item.label))) / 2);
  lcd.drawStr(labelX, 23, item.label);
  formatSettingValue(item, editValue, value, sizeof(value));
  lcd.setFont(u8g2_font_helvB14_tf);
  const int16_t x = max(0, (128 - static_cast<int16_t>(lcd.getStrWidth(value))) / 2);
  lcd.drawStr(x, 48, value);
}

const char *autoTuneStateText(AutoTuneState state) {
  switch (state) {
    case AutoTuneState::Running: return "DANG TU CHINH";
    case AutoTuneState::Success: return "DA HOAN THANH";
    case AutoTuneState::Failed: return "THAT BAI";
    default: return "SAN SANG";
  }
}

void drawAutoTune() {
  char text[28];
  drawHeader("TU CHINH PID");
  lcd.setFont(u8g2_font_helvB12_tf);
  const char *state = autoTuneStateText(currentRuntime.autoTuneState);
  const int16_t stateX = max(
      0, (128 - static_cast<int16_t>(lcd.getStrWidth(state))) / 2);
  lcd.drawStr(stateX, 29, state);

  lcd.setFont(u8g2_font_6x12_tf);
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    snprintf(text, sizeof(text), "TIEN DO %u%%",
             currentRuntime.autoTuneProgress);
  } else if (currentRuntime.batchRunning) {
    snprintf(text, sizeof(text), "HAY DUNG ME TRUOC");
  } else if (!currentRuntime.sensorOnline) {
    snprintf(text, sizeof(text), "CAM BIEN DANG LOI");
  } else {
    snprintf(text, sizeof(text), "MAY SE TU TIM PID");
  }
  const int16_t textX = max(
      0, (128 - static_cast<int16_t>(lcd.getStrWidth(text))) / 2);
  lcd.drawStr(textX, 46, text);

  // Hang thoat rieng, chon bang cuon xuong - tranh bam nham vao khoi dong
  // tu chinh (co dieu khien nhiet That) khi chi dinh luot qua trang.
  lcd.setFont(u8g2_font_5x8_tf);
  const char *exitLabel = "> THOAT <";
  if (autoTuneRow == 1U) {
    const int16_t w = static_cast<int16_t>(lcd.getStrWidth(exitLabel));
    lcd.drawBox(max(0, (128 - w) / 2) - 2, 55, w + 4, 9);
    lcd.setDrawColor(0);
    lcd.drawStr(max(0, (128 - w) / 2), 62, exitLabel);
    lcd.setDrawColor(1);
  } else {
    drawCenteredText(62, "cuon xuong de thoat");
  }
}

const char *testOutputLabel(uint8_t index) {
  switch (static_cast<TestOutputId>(index)) {
    case TestOutputId::HeaterSsr: return "SSR NHIET";
    case TestOutputId::HeatMaster: return "CONTACTOR TONG";
    case TestOutputId::CirculationFan: return "QUAT TUAN HOAN";
    case TestOutputId::VentFan: return "QUAT HUT";
    case TestOutputId::Light: return "DEN";
    case TestOutputId::Siren: return "COI";
    case TestOutputId::TurnLeft: return "DAO TRAI";
    case TestOutputId::TurnRight: return "DAO PHAI";
    default: return "?";
  }
}

const char *testLimitPhaseText(TestLimitPhase phase) {
  switch (phase) {
    case TestLimitPhase::Waiting: return "DANG CHO TAC DONG";
    case TestLimitPhase::Success: return "DA PHAT HIEN - OK";
    case TestLimitPhase::Timeout: return "KHONG PHAT HIEN";
    default: return "";
  }
}

void drawTestMode() {
  drawHeader("THU NGHIEM");
  lcd.setFont(u8g2_font_6x12_tf);
  char line[24];
  for (uint8_t row = 0; row < 4 && listTop + row < TEST_MODE_ITEM_COUNT; ++row) {
    const uint8_t local = listTop + row;
    const int16_t y = 22 + row * 12;
    const bool selected = local == listIndex;
    if (selected) {
      lcd.drawBox(0, y - 9, 128, 11);
      lcd.setDrawColor(0);
    }
    if (local < TEST_MODE_OUTPUT_ROWS) {
      const bool on = (currentRuntime.testOutputMaskActive & (1U << local)) != 0U;
      lcd.drawStr(2, y, testOutputLabel(local));
      lcd.drawStr(max(90, 126 - static_cast<int16_t>(lcd.getStrWidth(on ? "BAT" : "..."))),
                 y, on ? "BAT" : "...");
    } else if (local < TEST_MODE_OUTPUT_ROWS + TEST_MODE_LIMIT_ROWS) {
      const TestLimitId id = local == TEST_MODE_OUTPUT_ROWS
          ? TestLimitId::Left : TestLimitId::Right;
      snprintf(line, sizeof(line), "CTHT %s", id == TestLimitId::Left ? "TRAI" : "PHAI");
      lcd.drawStr(2, y, line);
      if (currentRuntime.testLimitTarget == id &&
          currentRuntime.testLimitPhase != TestLimitPhase::Idle) {
        const char *tag = currentRuntime.testLimitPhase == TestLimitPhase::Success ? "OK"
                         : currentRuntime.testLimitPhase == TestLimitPhase::Timeout ? "!!"
                         : "...";
        lcd.drawStr(max(100, 126 - static_cast<int16_t>(lcd.getStrWidth(tag))), y, tag);
      }
    } else {
      lcd.drawStr(2, y, "Thoat");
    }
    if (selected) lcd.setDrawColor(1);
  }
}

void drawWifiChange() {
  drawHeader("DOI WIFI");
  lcd.setFont(u8g2_font_6x12_tf);
  const WifiPortalState state = currentRuntime.wifiPortalState;
  const char *title = "DANG MO CONG...";
  if (state == WifiPortalState::ApActive) title = "HAY KET NOI VA DOI WIFI";
  else if (state == WifiPortalState::Testing) title = "DANG THU KET NOI...";
  else if (state == WifiPortalState::Success) title = "DA KET NOI!";
  else if (state == WifiPortalState::Failed) title = "KHONG THE KET NOI";
  const int16_t titleX = max(0, (128 - static_cast<int16_t>(lcd.getStrWidth(title))) / 2);
  lcd.drawStr(titleX, 24, title);

  lcd.setFont(u8g2_font_5x8_tf);
  char line[32];
  if (state == WifiPortalState::ApActive || state == WifiPortalState::Testing) {
    snprintf(line, sizeof(line), "Ket noi dien thoai toi:");
    lcd.drawStr(6, 38, line);
    snprintf(line, sizeof(line), "%s (192.168.4.1)", currentRuntime.wifiPortalApName);
    lcd.drawStr(6, 50, line);
  } else if (state == WifiPortalState::Failed) {
    lcd.drawStr(6, 40, "Hay thu lai voi mang/mat khau khac");
  } else if (state == WifiPortalState::Success) {
    lcd.drawStr(6, 40, "May da chuyen sang Wi-Fi moi");
  }
  drawCenteredText(62, "NHAN=THOAT");
}

void formatEventAge(uint32_t ageSec, char *out, size_t size) {
  if (ageSec < 5U) {
    snprintf(out, size, "VUA XONG");
  } else if (ageSec < 60U) {
    snprintf(out, size, "CACH %lus", static_cast<unsigned long>(ageSec));
  } else {
    snprintf(out, size, "CACH %lup%02lus",
             static_cast<unsigned long>(ageSec / 60UL),
             static_cast<unsigned long>(ageSec % 60UL));
  }
}

const char *inputEventName(uint16_t code) {
  switch (code - 100U) {
    case 0: return "HT TRAI";
    case 1: return "HT PHAI";
    case 2: return "CHE DO AUTO";
    case 3: return "CONG TAC NHIET";
    case 4: return "CONG TAC QUAT";
    case 5: return "CONG TAC DEN";
    case 6: return "LENH DAO TRAI";
    case 7: return "LENH DAO PHAI";
    default: return "INPUT";
  }
}

const char *outputEventName(uint16_t code) {
  switch (code - 200U) {
    case 1: return "CONTACTOR NHIET";
    case 2: return "DAO TRAI";
    case 3: return "DAO PHAI";
    case 4: return "QUAT HUT";
    case 5: return "DEN";
    case 6: return "QUAT TUAN HOAN";
    case 7: return "COI KHAN";
    default: return "OUTPUT";
  }
}

void eventText(const HmiEventItem &e, char *title, size_t titleSize,
               char *detail, size_t detailSize) {
  title[0] = detail[0] = '\0';
  if (e.code >= 1100U && e.code < 1400U) {
    const uint16_t faultCode = static_cast<uint16_t>(e.code - 1000U);
    snprintf(title, titleSize, "E%03u %s", faultCode, faultTitle(faultCode));
    switch (e.type) {
      case 4: snprintf(detail, detailSize, "LOI XUAT HIEN"); break;
      case 5: snprintf(detail, detailSize, "LOI DA HET"); break;
      case 6: snprintf(detail, detailSize, "NGUOI DUNG DA XN"); break;
      default: snprintf(detail, detailSize, "CHI TIET %d", e.value); break;
    }
    return;
  }
  if (e.code >= 100U && e.code < 200U) {
    snprintf(title, titleSize, "%s", inputEventName(e.code));
    snprintf(detail, detailSize, "%s", e.value ? "BAT" : "TAT");
    return;
  }
  if (e.code >= 200U && e.code < 300U) {
    snprintf(title, titleSize, "%s", outputEventName(e.code));
    snprintf(detail, detailSize, "%s", e.value ? "BAT" : "TAT");
    return;
  }
  switch (e.code) {
    case 1: snprintf(title, titleSize, "KHOI DONG NGUON"); break;
    case 2: snprintf(title, titleSize, "RESET NGOAI"); break;
    case 3: snprintf(title, titleSize, "RESET PHAN MEM"); break;
    case 4: snprintf(title, titleSize, "RESET PANIC"); break;
    case 5: snprintf(title, titleSize, "RESET WATCHDOG"); break;
    case 6: snprintf(title, titleSize, "RESET BROWNOUT"); break;
    case 20: snprintf(title, titleSize, "BAT DAU ME AP"); break;
    case 21: snprintf(title, titleSize, "DUNG ME AP"); break;
    case 22: snprintf(title, titleSize, "PHUC HOI ME"); break;
    case 23: snprintf(title, titleSize, "CHO XN SAU MAT DIEN"); break;
    case 24: snprintf(title, titleSize, "DA TIEP TUC ME CU"); break;
    case 25: snprintf(title, titleSize, "DA HUY ME CU"); break;
    case 30: snprintf(title, titleSize, "CHUYEN SANG AUTO"); break;
    case 31: snprintf(title, titleSize, "CHUYEN SANG MANUAL"); break;
    case 32: snprintf(title, titleSize, "AUTO OFF GIUA ME"); break;
    case 33: snprintf(title, titleSize, "AUTO DA PHUC HOI"); break;
    case 40: snprintf(title, titleSize, "CAM BIEN PHUC HOI"); break;
    case 41: snprintf(title, titleSize, "MAT CAM BIEN"); break;
    case 42: snprintf(title, titleSize, "RTC DA KET NOI LAI"); break;
    case 43: snprintf(title, titleSize, "RTC TU HIEU CHINH"); break;
    case 50: snprintf(title, titleSize, "DA LUU CAU HINH"); break;
    case 51: snprintf(title, titleSize, "BAT AUTO TUNE"); break;
    case 52: snprintf(title, titleSize, "AUTO TUNE OK"); break;
    case 53: snprintf(title, titleSize, "AUTO TUNE LOI"); break;
    case 54: snprintf(title, titleSize, "EEPROM PHUC HOI"); break;
    case 60: snprintf(title, titleSize, "BAT DAO TRAI"); break;
    case 61: snprintf(title, titleSize, "BAT DAO PHAI"); break;
    case 62: snprintf(title, titleSize, "TIM GOC TRAI"); break;
    case 63: snprintf(title, titleSize, "TIM GOC PHAI"); break;
    case 64: snprintf(title, titleSize, "DA DEN BEN TRAI"); break;
    case 65: snprintf(title, titleSize, "DA DEN BEN PHAI"); break;
    case 70: snprintf(title, titleSize, "MANG DA TAT"); break;
    case 71: snprintf(title, titleSize, "MANG DANG KET NOI"); break;
    case 72:
      snprintf(title, titleSize, "MANG DA KET NOI");
      snprintf(detail, detailSize, "TIN HIEU %d dBm", e.value);
      return;
    case 73: snprintf(title, titleSize, "MANG BI MAT"); break;
    case 74: snprintf(title, titleSize, "MANG CHUA CAU HINH"); break;
    case 80:
      snprintf(title, titleSize, "LENH BI TU CHOI");
      snprintf(detail, detailSize, "MA LENH %d", e.value);
      return;
    default: snprintf(title, titleSize, "SU KIEN %u", e.code); break;
  }
  if (e.value) snprintf(detail, detailSize, "GIA TRI %d", e.value);
  else snprintf(detail, detailSize, "DA GHI NHAN");
}

bool hmiLeapYear(uint16_t year) {
  return (year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U);
}

uint8_t hmiDaysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month < 1U || month > 12U) return 30U;
  return static_cast<uint8_t>(days[month - 1U] +
      ((month == 2U && hmiLeapYear(year)) ? 1U : 0U));
}

void formatEventDateTime(uint32_t epoch, char *out, size_t size) {
  if (!out || size == 0U) return;
  if (epoch == 0U) {
    snprintf(out, size, "--/-- --:--:--");
    return;
  }
  uint32_t days = epoch / 86400UL;
  uint32_t seconds = epoch % 86400UL;
  uint16_t year = 1970U;
  while (year < 2107U) {
    const uint16_t yearDays = hmiLeapYear(year) ? 366U : 365U;
    if (days < yearDays) break;
    days -= yearDays;
    ++year;
  }
  uint8_t month = 1U;
  while (month <= 12U) {
    const uint8_t monthDays = hmiDaysInMonth(year, month);
    if (days < monthDays) break;
    days -= monthDays;
    ++month;
  }
  const uint8_t day = static_cast<uint8_t>(days + 1U);
  const uint8_t hour = static_cast<uint8_t>(seconds / 3600UL);
  const uint8_t minute = static_cast<uint8_t>((seconds % 3600UL) / 60UL);
  const uint8_t second = static_cast<uint8_t>(seconds % 60UL);
  snprintf(out, size, "%02u/%02u %02u:%02u:%02u",
           static_cast<unsigned>(day), static_cast<unsigned>(month),
           static_cast<unsigned>(hour), static_cast<unsigned>(minute),
           static_cast<unsigned>(second));
}

void drawEventLog() {
  drawHeader("NHAT KY ME");
  if (!currentEventLog.count) {
    lcd.setFont(u8g2_font_6x12_tf);
    lcd.drawStr(18, 35, "CHUA CO SU KIEN");
    return;
  }
  if (eventLogIndex >= currentEventLog.count) eventLogIndex = 0U;
  const HmiEventItem &e = currentEventLog.items[eventLogIndex];
  char age[18];
  char timestamp[24];
  char title[28];
  char detail[28];
  char footer[40];
  formatEventAge(e.ageSec, age, sizeof(age));
  formatEventDateTime(e.epoch, timestamp, sizeof(timestamp));
  eventText(e, title, sizeof(title), detail, sizeof(detail));

  lcd.setFont(u8g2_font_5x8_tf);
  char top[40];
  snprintf(top, sizeof(top), "#%04lu %s",
           static_cast<unsigned long>(e.sequence % 10000UL), timestamp);
  lcd.drawStr(1, 20, top);
  lcd.setFont(u8g2_font_6x12_tf);
  lcd.drawStr(1, 34, title);
  lcd.setFont(u8g2_font_5x8_tf);
  lcd.drawStr(1, 46, detail);
  snprintf(footer, sizeof(footer), "%u/%u  %s",
           eventLogIndex + 1U, currentEventLog.totalInWindow, age);
  lcd.drawStr(1, 59, footer);
}

void drawConfirm() {
  char line[27];
  if (confirmAction == ConfirmAction::ResumeBatch) {
    snprintf(line, sizeof(line), confirmYes ? "MAT DIEN? >TIEP< HUY"
                                             : "MAT DIEN? TIEP >HUY<");
  } else if (confirmAction == ConfirmAction::AutoTuneStart) {
    snprintf(line, sizeof(line), confirmYes ? "AUTO PID? >CO< HUY"
                                             : "AUTO PID? CO >HUY<");
  } else if (currentRuntime.batchRunning) {
    snprintf(line, sizeof(line), confirmYes ? "DUNG ME? >CO< HUY"
                                             : "DUNG ME? CO >HUY<");
  } else {
    snprintf(line, sizeof(line), confirmYes ? "BAT ME? >CO< HUY"
                                             : "BAT ME? CO >HUY<");
  }

  // Xac nhan chi thay thanh tac vu 9 px o day, khong che noi dung trang.
  lcd.setDrawColor(1);
  lcd.drawBox(0, 55, 128, 9);
  lcd.setDrawColor(0);
  lcd.setFont(u8g2_font_5x8_tf);
  const int16_t x = max(1, (128 - static_cast<int16_t>(lcd.getStrWidth(line))) / 2);
  lcd.drawStr(x, 63, line);
  lcd.setDrawColor(1);
}

void drawToast(uint32_t now) {
  if (!toastLine[0] || timeReached(now, toastUntil)) return;
  lcd.setDrawColor(1);
  lcd.drawBox(0, 55, 128, 9);
  lcd.setDrawColor(0);
  lcd.setFont(u8g2_font_5x8_tf);
  char line[27];
  snprintf(line, sizeof(line), "%s%s", toastError ? "! " : "", toastLine);
  const int16_t x = max(1, (128 - static_cast<int16_t>(lcd.getStrWidth(line))) / 2);
  lcd.drawStr(x, 63, line);
  lcd.setDrawColor(1);
}

void render(uint32_t now) {
  if (!lcdReady) return;
  const bool periodicHome = view == View::Home &&
                            now - lastHomeDrawAt >= HOME_REFRESH_MS;
  const bool periodicAlarm = view == View::Alarm &&
                             now - lastAlarmDrawAt >= ALARM_REFRESH_MS;
  const bool verificationFrame = uiVerifyFramesRemaining != 0U &&
                                 timeReached(now, uiNextVerifyDrawAt);
  const bool consumeVerifyFrame = uiVerifyFramesRemaining == 2U ||
                                  verificationFrame;
  const bool periodic = periodicHome || periodicAlarm || verificationFrame;
  if (!dirty && !periodic) return;
  if (now - lastDrawAt < DISPLAY_MIN_DRAW_MS) return;

  // Moi frame bat dau tu trang thai do hoa xac dinh. Khong tat/bat LCD khi
  // chuyen trang: framebuffer moi duoc ve trong RAM roi sendBuffer mot lan.
  lcd.setMaxClipWindow();
  lcd.setDrawColor(1);
  lcd.setFontMode(0);
  lcd.setFontDirection(0);
  lcd.clearBuffer();
  switch (view) {
    case View::Home: drawHome(); break;
    case View::MainMenu: drawMainMenu(); break;
    case View::ChungMenu: drawChungMenu(); break;
    case View::SettingList: drawSettingList(); break;
    case View::EditSetting: drawEditSetting(); break;
    case View::TurnStats: drawTurnStats(); break;
    case View::AutoTune: drawAutoTune(); break;
    case View::TestMode: drawTestMode(); break;
    case View::WifiChange: drawWifiChange(); break;
    case View::EventLog: drawEventLog(); break;
    case View::Alarm: drawAlarm(); break;
  }
  if (confirmationActive() && view != View::Alarm) drawConfirm();
  else drawToast(now);
  lcd.setDrawColor(1);
  if (i2cLockCallback && !i2cLockCallback(I2C_TIMEOUT_MS)) {
    dirty = true;
    return;
  }
  lcd.sendBuffer();
  if (i2cUnlockCallback) i2cUnlockCallback();
  dirty = false;
  lastDrawAt = now;
  if (consumeVerifyFrame && uiVerifyFramesRemaining != 0U) {
    --uiVerifyFramesRemaining;
    if (uiVerifyFramesRemaining != 0U) {
      uiNextVerifyDrawAt = now + HMI_VERIFY_REDRAW_DELAY_MS;
    }
  }
  if (view == View::Home) lastHomeDrawAt = now;
  if (view == View::Alarm) lastAlarmDrawAt = now;
}

// ============================================================
// 7. API HMI CHO FIRMWARE TONG
// ============================================================
bool probeLcdUnlocked() {
  Wire.beginTransmission(LCD_I2C_ADDRESS);
  return Wire.endTransmission(true) == 0;
}

void recoverI2cBusUnlocked() {
#if MAYAP_HMI_OWNS_I2C_BUS
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SCL, HIGH);
  if (digitalRead(PIN_I2C_SDA) == LOW) {
    for (uint8_t i = 0; i < 9 && digitalRead(PIN_I2C_SDA) == LOW; ++i) {
      digitalWrite(PIN_I2C_SCL, LOW);
      delayMicroseconds(5);
      digitalWrite(PIN_I2C_SCL, HIGH);
      delayMicroseconds(5);
    }
  }
  pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SDA, HIGH);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
#endif
}

bool beginLcd() {
  if (i2cLockCallback && !i2cLockCallback(I2C_TIMEOUT_MS)) return false;
#if MAYAP_HMI_OWNS_I2C_BUS
  recoverI2cBusUnlocked();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
#endif
  Wire.setTimeOut(I2C_TIMEOUT_MS);
  if (!probeLcdUnlocked()) {
    if (i2cUnlockCallback) i2cUnlockCallback();
    return false;
  }
  lcd.setBusClock(I2C_CLOCK_HZ);
  lcd.setI2CAddress(static_cast<uint8_t>(LCD_I2C_ADDRESS << 1));
  lcd.begin();
  lcd.setContrast(DEFAULT_CONTRAST);
  if (i2cUnlockCallback) i2cUnlockCallback();
  return true;
}

void serviceLcd(uint32_t now) {
  if (!lcdReady) {
    if (now - lastLcdRetryAt < LCD_RETRY_INTERVAL_MS) return;
    lastLcdRetryAt = now;
    lcdReady = beginLcd();
    if (lcdReady) {
      lastLcdHealthCheckAt = now;
      dirty = true;
      showToast("LCD DA TU PHUC HOI");
#if MAYAP_DIAGNOSTIC_SERIAL
      mayapSerialPrintf(false, "[HMI] LCD recovered\n");
#endif
    } else if (now - lastLcdFaultLogAt >= LCD_FAULT_LOG_INTERVAL_MS) {
      lastLcdFaultLogAt = now;
#if MAYAP_DIAGNOSTIC_SERIAL
      mayapSerialPrintf(false, "[HMI] LCD 0x%02X van mat\n", LCD_I2C_ADDRESS);
#endif
    }
    return;
  }

  if (now - lastLcdHealthCheckAt < LCD_HEALTH_CHECK_MS) return;
  lastLcdHealthCheckAt = now;
  if (i2cLockCallback && !i2cLockCallback(I2C_TIMEOUT_MS)) return;
  const bool ok = probeLcdUnlocked();
  if (i2cUnlockCallback) i2cUnlockCallback();
  if (!ok) {
    lcdReady = false;
    lastLcdRetryAt = now;
    dirty = true;
#if MAYAP_DIAGNOSTIC_SERIAL
    mayapSerialPrintf(false, "[HMI] LCD/I2C lost, scheduling recovery\n");
#endif
  }
}

void hmiBegin() {
  buzzerBegin();
  beginRotary();
  lcdReady = beginLcd();
  const uint32_t now = millis();
  lastInteractionAt = now;
  lastCommandPollAt = now;
  lastLcdRetryAt = now;
  lastLcdHealthCheckAt = now;
  dirty = true;
#if MAYAP_DIAGNOSTIC_SERIAL
  mayapSerialPrintf(false, "[HMI] LCD=%s profile=%u contrast=%u\n", lcdReady ? "OK" : "FAIL",
                   LCD_PROFILE, DEFAULT_CONTRAST);
#endif
}

void sanitizeRuntime(MachineRuntime &runtime) {
  runtime.dateText[sizeof(runtime.dateText) - 1U] = '\0';
  runtime.machineState[sizeof(runtime.machineState) - 1U] = '\0';
  runtime.alarmMask &= ALARM_KNOWN_MASK;
  if (!isfinite(runtime.temperature) || !isfinite(runtime.humidity)) {
    runtime.sensorOnline = false;
  }
  if (!isfinite(runtime.temperature)) runtime.temperature = 0.0f;
  if (!isfinite(runtime.humidity)) runtime.humidity = 0.0f;
  runtime.temperature = constrain(runtime.temperature, -40.0f, 125.0f);
  runtime.humidity = constrain(runtime.humidity, 0.0f, 100.0f);
  if (!isfinite(runtime.heaterPower)) runtime.heaterPower = 0.0f;
  runtime.heaterPower = constrain(runtime.heaterPower, 0.0f, 100.0f);
  if (static_cast<uint8_t>(runtime.turnState) >
      static_cast<uint8_t>(TurnState::Fault)) {
    runtime.turnState = TurnState::Fault;
  }
  if (static_cast<uint8_t>(runtime.autoTuneState) >
      static_cast<uint8_t>(AutoTuneState::Failed)) {
    runtime.autoTuneState = AutoTuneState::Failed;
  }
  if (static_cast<uint8_t>(runtime.connectivityMode) >
      static_cast<uint8_t>(ConnectivityMode::Online)) {
    runtime.connectivityMode = ConnectivityMode::Offline;
  }
  if (static_cast<uint8_t>(runtime.networkState) >
      static_cast<uint8_t>(NetworkStateCode::Connected)) {
    runtime.networkState = NetworkStateCode::Offline;
  }
  if (runtime.networkRssiDbm < -127) runtime.networkRssiDbm = -127;
  if (runtime.networkRssiDbm > 0) runtime.networkRssiDbm = 0;
  runtime.networkConnected =
      runtime.connectivityMode == ConnectivityMode::Online &&
      runtime.networkConfigured &&
      runtime.networkState == NetworkStateCode::Connected &&
      runtime.networkConnected;
  runtime.autoTuneProgress = static_cast<uint8_t>(constrain(
      static_cast<int>(runtime.autoTuneProgress), 0, 100));
  if (runtime.activeFaultDisplayCount > HMI_FAULT_DISPLAY_CAPACITY) {
    runtime.activeFaultDisplayCount = HMI_FAULT_DISPLAY_CAPACITY;
  }
  if (runtime.activeFaultCount < runtime.activeFaultDisplayCount) {
    runtime.activeFaultCount = runtime.activeFaultDisplayCount;
  }
  if (runtime.autoTuneState == AutoTuneState::Success) {
    runtime.autoTuneProgress = 100;
  } else if (runtime.autoTuneState == AutoTuneState::Idle ||
             runtime.autoTuneState == AutoTuneState::Failed) {
    runtime.autoTuneProgress = 0;
  }
  if (!runtime.dateText[0]) snprintf(runtime.dateText, sizeof(runtime.dateText),
                                     "--/--/----");
  if (!runtime.machineState[0]) {
    snprintf(runtime.machineState, sizeof(runtime.machineState),
             runtime.batchRunning ? "DANG AP" : "SAN SANG");
  }
}

int16_t displayTenths(float value) {
  return static_cast<int16_t>(lroundf(value * 10.0f));
}

int16_t displayWhole(float value) {
  return static_cast<int16_t>(lroundf(value));
}

bool fixedTextChanged(const char *a, const char *b, size_t size) {
  return strncmp(a, b, size) != 0;
}

bool runtimeHeaderChanged(const MachineRuntime &before,
                          const MachineRuntime &after) {
  return before.alarmMask != after.alarmMask ||
         before.primaryFaultCode != after.primaryFaultCode ||
         before.activeFaultCount != after.activeFaultCount ||
         before.faultNotificationSequence != after.faultNotificationSequence ||
         fixedTextChanged(before.dateText, after.dateText,
                          sizeof(before.dateText));
}

// Chi danh dau ve lai khi du lieu dang nhin thay tren trang hien tai thay doi.
// Chi danh dau dirty khi noi dung dang hien thi thuc su thay doi; khong
// lam LCD gui lai 1024 byte neu trang hien tai khong hien chung.
bool runtimeVisibleChanged(const MachineRuntime &before,
                           const MachineRuntime &after) {
  if (runtimeHeaderChanged(before, after)) return true;

  switch (view) {
    case View::Home:
      if (homePage == 0) {
        return before.sensorOnline != after.sensorOnline ||
               (after.sensorOnline &&
                displayTenths(before.temperature) !=
                    displayTenths(after.temperature)) ||
               (after.sensorOnline &&
                displayWhole(before.humidity) != displayWhole(after.humidity)) ||
               before.batchRunning != after.batchRunning ||
               before.currentDay != after.currentDay ||
               before.nextTurnMinutes != after.nextTurnMinutes ||
               before.nextTurnScheduled != after.nextTurnScheduled ||
               before.turningLockdown != after.turningLockdown ||
               fixedTextChanged(before.machineState, after.machineState,
                                sizeof(before.machineState));
      }
      return before.heaterOn != after.heaterOn ||
             displayWhole(before.heaterPower) != displayWhole(after.heaterPower) ||
             before.circulationFanOn != after.circulationFanOn ||
             before.ventFanOn != after.ventFanOn ||
             before.turnState != after.turnState ||
             before.turningLockdown != after.turningLockdown ||
             before.nextTurnMinutes != after.nextTurnMinutes ||
             before.nextTurnScheduled != after.nextTurnScheduled;

    case View::TurnStats:
      return before.turnCountToday != after.turnCountToday ||
             before.turnCountBatch != after.turnCountBatch ||
             before.nextTurnMinutes != after.nextTurnMinutes ||
             before.nextTurnScheduled != after.nextTurnScheduled ||
             before.turningLockdown != after.turningLockdown;

    case View::AutoTune:
      return before.autoTuneState != after.autoTuneState ||
             before.autoTuneProgress != after.autoTuneProgress ||
             before.batchRunning != after.batchRunning ||
             before.sensorOnline != after.sensorOnline;

    case View::EventLog:
      return before.eventSequence != after.eventSequence;

    case View::Alarm:
      return displayTenths(before.temperature) !=
                 displayTenths(after.temperature) ||
             displayWhole(before.humidity) != displayWhole(after.humidity) ||
             before.primaryFaultCode != after.primaryFaultCode ||
             before.activeFaultDisplayCount != after.activeFaultDisplayCount ||
             before.faultNotificationSequence != after.faultNotificationSequence;

    case View::TestMode:
      return before.testModeActive != after.testModeActive ||
             before.testOutputMaskActive != after.testOutputMaskActive ||
             before.testLimitTarget != after.testLimitTarget ||
             before.testLimitPhase != after.testLimitPhase;

    case View::WifiChange:
      return before.wifiPortalState != after.wifiPortalState ||
             fixedTextChanged(before.wifiPortalApName, after.wifiPortalApName,
                              sizeof(before.wifiPortalApName));

    case View::MainMenu:
    case View::ChungMenu:
    case View::SettingList:
    case View::EditSetting:
      return false;
  }
  return false;
}

void applyRuntime(MachineRuntime runtime) {
  sanitizeRuntime(runtime);
  const AutoTuneState previousAutoTuneState = currentRuntime.autoTuneState;
  const bool newFaultOccurrence =
      runtime.faultNotificationSequence != currentRuntime.faultNotificationSequence &&
      runtime.lastRaisedFaultCode != 0U;
  const uint32_t newFaultAlarmBit = newFaultOccurrence
      ? alarmBitForFaultCode(runtime.lastRaisedFaultCode) : AlarmNone;
  const uint32_t newAlarmBits = runtime.alarmMask & ~alarmPresentedMask;
  alarmPresentedMask &= runtime.alarmMask;

  const bool visibleChange = runtimeVisibleChanged(currentRuntime, runtime);
  currentRuntime = runtime;
  buzzer.acknowledgedAlarmMask &= currentRuntime.alarmMask;
  // Moi ma loi moi deu duoc keu lai, ke ca no dung chung AlarmBit voi mot loi
  // cu da duoc nguoi dung ACK truoc do.
  if (newFaultAlarmBit != AlarmNone) buzzerUnacknowledge(newFaultAlarmBit);

  if (currentRuntime.autoTuneState != previousAutoTuneState) {
    if (currentRuntime.autoTuneState == AutoTuneState::Running) {
      showToast("AUTO TUNE DA BAT DAU");
    } else if (currentRuntime.autoTuneState == AutoTuneState::Success) {
      showToast("AUTO TUNE THANH CONG");
      buzzerPlayCue(BuzzerCue::Ok);
    } else if (currentRuntime.autoTuneState == AutoTuneState::Failed) {
      showToast("AUTO TUNE THAT BAI", true);
      buzzerPlayCue(BuzzerCue::Error);
    }
  }

  if (!currentRuntime.resumeConfirmationRequired) resumeDecisionSubmitted = false;

  if (newFaultOccurrence || newAlarmBits) {
    alarmPresentedMask |= newAlarmBits | newFaultAlarmBit;
    if (view != View::Alarm) alarmReturnView = view;
    alarmIndex = 0;
    view = View::Alarm;
    dirty = true;
  } else if (!runtime.alarmMask && view == View::Alarm) {
    view = alarmReturnView;
    dirty = true;
  }

  // Sau khi xu ly man hinh loi, neu day la khoi dong lai sau mat dien thi
  // luon dua nguoi dung den man hinh xac nhan. Khong de trang Alarm cu che
  // mat yeu cau tiep tuc/huy me.
  if (currentRuntime.resumeConfirmationRequired && !resumeDecisionSubmitted &&
      confirmAction != ConfirmAction::ResumeBatch && view != View::Alarm) {
    openResumeConfirm();
  }
  if (visibleChange) dirty = true;
}

void applyHostConfig(MachineConfig config) {
  sanitizeConfig(config);
  // Dong bo tu firmware khong duoc tu y day nguoi dung ra khoi trang dang mo.
  // ACK luu se xu ly transaction; mailbox config chi cap nhat gia tri nen.
  currentConfig = config;
  if (view == View::EditSetting && !configSave.active) {
    const SettingItem &item = SETTINGS[editSettingIndex];
    float minimum, maximum;
    settingLimits(currentConfig, item, minimum, maximum);
    editValue = constrain(editValue, minimum, maximum);
  }
  dirty = true;
}

void processConfigAck(const ConfigAckInbox &ack) {
  MachineConfig rollback;
  MachineConfig accepted;
  bool matched = false;
  portENTER_CRITICAL(&hmiApiMux);
  if (configSave.active && configSave.id == ack.transactionId) {
    rollback = configSave.rollback;
    accepted = ack.hasStoredConfig ? ack.storedConfig : configSave.candidate;
    configSave.active = false;
    configSave.readyForHost = false;
    matched = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  if (!matched) return;

  if (ack.ok) {
    sanitizeConfig(accepted);
    currentConfig = accepted;
    showToast("DA LUU CAU HINH");
    buzzerPlayCue(BuzzerCue::Save);
  } else {
    currentConfig = rollback;
    showToast("LOI LUU - DA HOAN TAC", true);
    buzzerPlayCue(BuzzerCue::Error);
  }
  dirty = true;
}

void processCommandAcks() {
  for (uint8_t budget = 0U; budget < COMMAND_ACK_QUEUE_SIZE; ++budget) {
    CommandAck ack;
    HmiCommand command;
    bool haveAck = false;
    bool matched = false;
    portENTER_CRITICAL(&hmiApiMux);
    if (commandAckCount) {
      ack = commandAckQueue[commandAckHead];
      commandAckHead = static_cast<uint8_t>(
          (commandAckHead + 1U) % COMMAND_ACK_QUEUE_SIZE);
      --commandAckCount;
      haveAck = true;
      for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
        if (activeCommands[i].used &&
            activeCommands[i].command.id == ack.commandId) {
          command = activeCommands[i].command;
          activeCommands[i].used = false;
          if (commandOutstandingCount) --commandOutstandingCount;
          matched = true;
          break;
        }
      }
    }
    portEXIT_CRITICAL(&hmiApiMux);
    if (!haveAck) break;
    if (!matched) continue;

    if (command.type == HmiCommandType::AlarmAck) {
      if (!ack.ok) buzzerUnacknowledge(command.alarmMask);
    } else {
      buzzerPlayCue(ack.ok ? BuzzerCue::Ok : BuzzerCue::Error);
    }
    showToast(ack.message[0] ? ack.message :
              (ack.ok ? "LENH DA THUC HIEN" : "LENH BI TU CHOI"), !ack.ok);

    // Neu yeu cau mo cong Wi-Fi/vao thu nghiem bi tu choi ngay tai firmware
    // tong, dua nguoi dung tro lai man hinh truoc do thay vi ket ket qua cho.
    if (!ack.ok && command.type == HmiCommandType::WifiPortalStart &&
        view == View::WifiChange) {
      goBack();
    }
    if (!ack.ok && command.type == HmiCommandType::TestModeEnter &&
        view == View::TestMode) {
      view = View::MainMenu;
      mainIndex = MAIN_THU_NGHIEM;
      alignMainMenuWindow();
      dirty = true;
    }
  }
}

void serviceApiMailboxes() {
  // hmiUpdate() co the duoc goi rat nhanh. Khong vao critical section neu
  // firmware tong khong gui du lieu/ACK moi.
  if (!apiHasPendingWork()) return;

  // Scratch tinh: HMI chi cho phep mot task goi hmiUpdate(), tranh khoi tao
  // lai MachineConfig/MachineRuntime lon tren stack o moi vong loop.
  static MachineRuntime runtime;
  static MachineConfig config;
  static HmiEventSnapshot eventLog;
  static ConfigAckInbox configAck;
  static char date[11];
  bool hasRuntime = false;
  bool hasConfig = false;
  bool hasEventLog = false;
  bool hasConfigAck = false;
  bool hasDate = false;
  bool hasCue = false;
  BuzzerCue cue = BuzzerCue::None;

  portENTER_CRITICAL(&hmiApiMux);
  if (runtimeInboxPending) {
    runtime = runtimeInbox;
    runtimeInboxPending = false;
    hasRuntime = true;
  }
  if (configInboxPending) {
    config = configInbox;
    configInboxPending = false;
    hasConfig = true;
  }
  if (eventLogInboxPending) {
    eventLog = eventLogInbox;
    eventLogInboxPending = false;
    hasEventLog = true;
  }
  if (configAckInbox.pending) {
    configAck = configAckInbox;
    configAckInbox.pending = false;
    hasConfigAck = true;
  }
  if (dateInboxPending) {
    memcpy(date, dateInbox, sizeof(date));
    dateInboxPending = false;
    hasDate = true;
  }
  if (cueInboxPending) {
    cue = cueInbox;
    cueInboxPending = false;
    hasCue = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);

  if (hasConfigAck) processConfigAck(configAck);
  if (hasConfig) applyHostConfig(config);
  if (hasRuntime) applyRuntime(runtime);
  if (hasEventLog) {
    currentEventLog = eventLog;
    if (eventLogIndex >= currentEventLog.count) eventLogIndex = 0U;
    if (view == View::EventLog) dirty = true;
  }
  if (hasCue) buzzerPlayCue(cue);
  if (hasDate &&
      strncmp(date, currentRuntime.dateText, sizeof(currentRuntime.dateText)) != 0) {
    memcpy(currentRuntime.dateText, date, sizeof(currentRuntime.dateText));
    currentRuntime.dateText[sizeof(currentRuntime.dateText) - 1U] = '\0';
    dirty = true;
  }
  processCommandAcks();

  portENTER_CRITICAL(&hmiApiMux);
  const bool stillPending = runtimeInboxPending || configInboxPending ||
                            eventLogInboxPending || configAckInbox.pending ||
                            dateInboxPending || cueInboxPending ||
                            commandAckCount != 0;
  setApiWorkPending(stillPending);
  portEXIT_CRITICAL(&hmiApiMux);
}

void serviceCommandTimeouts(uint32_t now) {
  uint32_t alarmMaskToUnack = AlarmNone;
  bool anyTimeout = false;
  portENTER_CRITICAL(&hmiApiMux);

  HmiCommand retained[COMMAND_QUEUE_SIZE];
  uint8_t retainedCount = 0;
  while (commandCount) {
    const HmiCommand command = commandQueue[commandHead];
    commandHead = static_cast<uint8_t>((commandHead + 1U) % COMMAND_QUEUE_SIZE);
    --commandCount;
    if (now - command.createdAt >= command.validForMs) {
      if (commandOutstandingCount) --commandOutstandingCount;
      if (command.type == HmiCommandType::AlarmAck) {
        alarmMaskToUnack |= command.alarmMask;
      }
      anyTimeout = true;
    } else {
      retained[retainedCount++] = command;
    }
  }
  commandHead = 0;
  commandTail = retainedCount % COMMAND_QUEUE_SIZE;
  commandCount = retainedCount;
  for (uint8_t i = 0; i < retainedCount; ++i) commandQueue[i] = retained[i];

  for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
    if (!activeCommands[i].used) continue;
    if (now - activeCommands[i].takenAt < COMMAND_CONFIRM_TIMEOUT_MS) {
      continue;
    }
    if (activeCommands[i].command.type == HmiCommandType::AlarmAck) {
      alarmMaskToUnack |= activeCommands[i].command.alarmMask;
    }
    activeCommands[i].used = false;
    if (commandOutstandingCount) --commandOutstandingCount;
    anyTimeout = true;
  }
  if (expiredAlarmAckMask) anyTimeout = true;
  alarmMaskToUnack |= expiredAlarmAckMask;
  expiredAlarmAckMask = AlarmNone;
  portEXIT_CRITICAL(&hmiApiMux);

  if (alarmMaskToUnack) buzzerUnacknowledge(alarmMaskToUnack);
  if (anyTimeout) {
    showToast("LENH QUA THOI GIAN", true);
    buzzerPlayCue(BuzzerCue::Error);
  }
}

void serviceConfigSaveTimeout(uint32_t now) {
  MachineConfig rollback;
  bool timedOut = false;
  portENTER_CRITICAL(&hmiApiMux);
  if (configSave.active &&
      now - configSave.startedAt >= SAVE_CONFIRM_TIMEOUT_MS) {
    rollback = configSave.rollback;
    configSave.active = false;
    configSave.readyForHost = false;
    timedOut = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  if (!timedOut) return;
  currentConfig = rollback;
  showToast("LUU QUA THOI GIAN - DA HOAN TAC", true);
  buzzerPlayCue(BuzzerCue::Error);
}

void hmiUpdate(uint32_t now) {
  serviceApiMailboxes();
  stabilizeViewTransition();
  if (now - lastCommandPollAt >= HMI_COMMAND_POLL_MS) {
    lastCommandPollAt = now;
    serviceCommandTimeouts(now);
    serviceConfigSaveTimeout(now);
  }

  updateRotary(now);
  if (rotary.step || rotary.button != ButtonEvent::None) {
    lastInteractionAt = now;
  }
  handleInput();
  stabilizeViewTransition();

  if (!confirmationActive() && view != View::Home && view != View::Alarm &&
      now - lastInteractionAt >= MENU_IDLE_TIMEOUT_MS) {
    // Roi Che do thu nghiem/Doi Wi-Fi do khong thao tac phai dong hang han
    // ngay tren firmware tong, khong chi tam roi man hinh.
    if (view == View::TestMode) queueCommand(HmiCommandType::TestModeExit);
    else if (view == View::WifiChange) queueCommand(HmiCommandType::WifiPortalCancel);
    view = View::Home;
    homePage = 0;
    showToast("TU DONG VE MAN HINH CHINH");
    lastInteractionAt = now;
  }

  buzzerUpdate(now);
  if (toastLine[0] && timeReached(now, toastUntil)) {
    clearToast();
    dirty = true;
  }
  serviceLcd(now);
  render(now);
}

void hmiSetConfig(const MachineConfig &config) {
  portENTER_CRITICAL(&hmiApiMux);
  configInbox = config;
  configInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}

void hmiSetDate(const char *dateText) {
  if (!dateText || !dateText[0]) dateText = "--/--/----";
  portENTER_CRITICAL(&hmiApiMux);
  snprintf(dateInbox, sizeof(dateInbox), "%.10s", dateText);
  dateInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}

const MachineConfig &hmiGetConfig() { return currentConfig; }

void hmiSetRuntime(const MachineRuntime &runtime) {
  portENTER_CRITICAL(&hmiApiMux);
  const uint32_t accumulatedAlarmMask =
      runtimeInboxPending ? runtimeInbox.alarmMask : AlarmNone;
  runtimeInbox = runtime;
  runtimeInbox.alarmMask |= accumulatedAlarmMask;
  runtimeInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}

void hmiSetEventLog(const HmiEventSnapshot &snapshot) {
  portENTER_CRITICAL(&hmiApiMux);
  eventLogInbox = snapshot;
  eventLogInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}

bool hmiTakeSavedConfig(MachineConfig &out, uint32_t &transactionId) {
  bool available = false;
  portENTER_CRITICAL(&hmiApiMux);
  if (configSave.active && configSave.readyForHost) {
    out = configSave.candidate;
    transactionId = configSave.id;
    configSave.readyForHost = false;
    available = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  return available;
}

bool hmiConfirmConfigSave(uint32_t transactionId, bool ok,
                          const MachineConfig *storedConfig) {
  bool accepted = false;
  portENTER_CRITICAL(&hmiApiMux);
  if (!configAckInbox.pending && configSave.active &&
      configSave.id == transactionId) {
    configAckInbox.pending = true;
    configAckInbox.transactionId = transactionId;
    configAckInbox.ok = ok;
    configAckInbox.hasStoredConfig = storedConfig != nullptr;
    if (storedConfig) configAckInbox.storedConfig = *storedConfig;
    markApiWorkPending();
    accepted = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  return accepted;
}

bool hmiTakeCommand(HmiCommand &out) {
  const uint32_t now = millis();
  bool available = false;
  portENTER_CRITICAL(&hmiApiMux);
  while (commandCount && !available) {
    const HmiCommand command = commandQueue[commandHead];
    commandHead = static_cast<uint8_t>((commandHead + 1U) % COMMAND_QUEUE_SIZE);
    --commandCount;
    if (now - command.createdAt >= command.validForMs) {
      if (commandOutstandingCount) --commandOutstandingCount;
      if (command.type == HmiCommandType::AlarmAck) {
        expiredAlarmAckMask |= command.alarmMask;
      }
      continue;
    }
    for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
      if (activeCommands[i].used) continue;
      activeCommands[i].used = true;
      activeCommands[i].takenAt = now;
      activeCommands[i].command = command;
      out = command;
      available = true;
      break;
    }
    if (!available) {
      if (commandOutstandingCount) --commandOutstandingCount;
      if (command.type == HmiCommandType::AlarmAck) {
        expiredAlarmAckMask |= command.alarmMask;
      }
    }
  }
  portEXIT_CRITICAL(&hmiApiMux);
  return available;
}

bool hmiConfirmCommand(uint32_t commandId, bool ok, const char *message) {
  bool exists = false;
  bool accepted = false;
  portENTER_CRITICAL(&hmiApiMux);
  for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
    if (activeCommands[i].used &&
        activeCommands[i].command.id == commandId) {
      exists = true;
      break;
    }
  }
  if (exists && commandAckCount < COMMAND_ACK_QUEUE_SIZE) {
    CommandAck &ack = commandAckQueue[commandAckTail];
    ack.commandId = commandId;
    ack.ok = ok;
    snprintf(ack.message, sizeof(ack.message), "%s", message ? message : "");
    commandAckTail = static_cast<uint8_t>(
        (commandAckTail + 1U) % COMMAND_ACK_QUEUE_SIZE);
    ++commandAckCount;
    markApiWorkPending();
    accepted = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  return accepted;
}

void hmiSetI2cLockCallbacks(HmiI2cLockFn lockFn,
                            HmiI2cUnlockFn unlockFn) {
  portENTER_CRITICAL(&hmiApiMux);
  if (lockFn && unlockFn) {
    i2cLockCallback = lockFn;
    i2cUnlockCallback = unlockFn;
  } else {
    i2cLockCallback = nullptr;
    i2cUnlockCallback = nullptr;
  }
  portEXIT_CRITICAL(&hmiApiMux);
}

void hmiPlayCue(BuzzerCue cue) {
  portENTER_CRITICAL(&hmiApiMux);
  cueInbox = cue;
  cueInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}
