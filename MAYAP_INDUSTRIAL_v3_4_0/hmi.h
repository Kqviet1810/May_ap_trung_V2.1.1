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

// ============================================================================
// QR CODE (Version 1, EC level Q, che do Alphanumeric, mask co dinh = 0) -
// CHI ma hoa dung dinh dang Device ID "MAP-XXXXXXXXXXXX" (16 ky tu), khong
// phai bo ma hoa QR tong quat. Man LCD 128x64 mono qua nho de ve QR "day du"
// (nhieu phien ban/muc do loi hon) van con doc duoc - Version 1 la kich
// thuoc NHO NHAT chuan QR cho phep (21x21 o), toi da hoa so pixel/o tren
// man hinh chat hep nay. EC level Q (30% du thua, khong phai muc cao nhat
// H) la muc CAO NHAT van du cho du 16 ky tu ID nam trong 1 Version 1 -
// dung H se buoc phai nhay len Version 2 (25x25), lam pixel/o NHO DI, phan
// tac dung. Mask co dinh = 0 (bo qua buoc "chon mask toi uu" ma cac bo ma
// hoa QR day du hay lam) vi ket qua van hop chuan/quet duoc binh thuong,
// chi khac ve tham my phan bo diem den/trang, doi lai giam dang ke code
// (khong can tinh diem phat 8 mask).
//
// Thuat toan da duoc KIEM CHUNG rieng bang Python + pyzbar (thu vien giai
// ma doc lap, khong lien quan gi code o day) truoc khi dich sang C++: 34
// ID ngau nhien (gom ca bien MAP-000000000000/MAP-FFFFFFFFFFFF) deu quet
// ra dung chuoi goc. Sai du chi 1 bit o day la QR khong quet duoc ma
// khong co canh bao gi khi bien dich hay chay thu - nen giu dung cong thuc
// da kiem chung, KHONG "toi uu lai" cho gon hon neu khong kiem chung lai.
namespace QrMapId {
constexpr uint8_t SIZE = 21;
constexpr const char *ALPHANUM = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
// Format info 15-bit (EC=Q, mask=0) la HANG SO co dinh, khong phu thuoc du
// lieu - tinh san bang BCH thay vi tinh lai moi lan.
constexpr uint16_t FORMAT_BITS = 0b011010101011111;
// Da thuc sinh (generator polynomial) cho 13 codeword sua loi - CO DINH cho
// moi truong hop 13 EC codeword, khong phu thuoc du lieu dau vao.
constexpr uint8_t GEN_POLY[14] = {1, 137, 73, 227, 17, 177, 17, 52, 13, 46, 43, 83, 132, 120};

// Bang GF(256) cho phep nhan Reed-Solomon (da thuc nguyen thuy QR chuan
// x^8+x^4+x^3+x^2+1 = 0x11D) - tinh san luc build thay vi tinh runtime.
constexpr uint8_t GF_EXP[256] = {
  1,2,4,8,16,32,64,128,29,58,116,232,205,135,19,38,76,152,45,90,180,117,234,201,
  143,3,6,12,24,48,96,192,157,39,78,156,37,74,148,53,106,212,181,119,238,193,
  159,35,70,140,5,10,20,40,80,160,93,186,105,210,185,111,222,161,95,190,97,194,
  153,47,94,188,101,202,137,15,30,60,120,240,253,231,211,187,107,214,177,127,
  254,225,223,163,91,182,113,226,217,175,67,134,17,34,68,136,13,26,52,104,208,
  189,103,206,129,31,62,124,248,237,199,147,59,118,236,197,151,51,102,204,133,
  23,46,92,184,109,218,169,79,158,33,66,132,21,42,84,168,77,154,41,82,164,85,
  170,73,146,57,114,228,213,183,115,230,209,191,99,198,145,63,126,252,229,215,
  179,123,246,241,255,227,219,171,75,150,49,98,196,149,55,110,220,165,87,174,
  65,130,25,50,100,200,141,7,14,28,56,112,224,221,167,83,166,81,162,89,178,121,
  242,249,239,195,155,43,86,172,69,138,9,18,36,72,144,61,122,244,245,247,243,
  251,235,203,139,11,22,44,88,176,125,250,233,207,131,27,54,108,216,173,71,142,1
};
constexpr uint8_t GF_LOG[256] = {
  0,0,1,25,2,50,26,198,3,223,51,238,27,104,199,75,4,100,224,14,52,141,239,129,
  28,193,105,248,200,8,76,113,5,138,101,47,225,36,15,33,53,147,142,218,240,18,
  130,69,29,181,194,125,106,39,249,185,201,154,9,120,77,228,114,166,6,191,139,
  98,102,221,48,253,226,152,37,179,16,145,34,136,54,208,148,206,143,150,219,
  189,241,210,19,92,131,56,70,64,30,66,182,163,195,72,126,110,107,58,40,84,250,
  133,186,61,202,94,155,159,10,21,121,43,78,212,229,172,115,243,167,87,7,112,
  192,247,140,128,99,13,103,74,222,237,49,197,254,24,227,165,153,119,38,184,
  180,124,17,68,146,217,35,32,137,46,55,63,209,91,149,188,207,205,144,135,151,
  178,220,252,190,97,242,86,211,171,20,42,93,158,132,60,57,83,71,109,65,162,31,
  45,67,216,183,123,164,118,196,23,73,236,127,12,111,246,108,161,59,82,41,157,
  85,170,251,96,134,177,187,204,62,90,203,89,95,176,156,169,160,81,11,245,22,
  235,122,117,44,215,79,174,213,233,230,231,173,232,116,214,244,234,168,80,88,175
};

uint8_t gfMul(uint8_t a, uint8_t b) {
  if (a == 0 || b == 0) return 0;
  const uint16_t sum = static_cast<uint16_t>(GF_LOG[a]) + GF_LOG[b];
  return GF_EXP[sum % 255];
}

int8_t alphanumericValue(char c) {
  const char *p = strchr(ALPHANUM, c);
  return p ? static_cast<int8_t>(p - ALPHANUM) : -1;
}

// text PHAI dung 16 ky tu alphanumeric hop le (dinh dang Device ID). Tra ve
// false neu khong dung dinh dang - goi noi dung KHONG duoc ve QR sai.
bool buildDataCodewords(const char *text, uint8_t out[13]) {
  if (!text || strlen(text) != 16U) return false;
  bool bits[104] = {false};
  int pos = 0;
  auto pushBits = [&](uint32_t value, int count) {
    for (int i = count - 1; i >= 0; --i) bits[pos++] = (value >> i) & 1U;
  };
  pushBits(0b0010U, 4);   // mode indicator: Alphanumeric
  pushBits(16U, 9);       // character count indicator (V1, alphanumeric = 9 bit)
  for (int i = 0; i < 16; i += 2) {
    const int8_t v1 = alphanumericValue(text[i]);
    const int8_t v2 = alphanumericValue(text[i + 1]);
    if (v1 < 0 || v2 < 0) return false;
    pushBits(static_cast<uint32_t>(v1) * 45U + static_cast<uint32_t>(v2), 11);
  }
  // 4+9+8*11 = 101 bit dung 16 ky tu; 3 bit terminator con lai da la 0 san
  // trong mang (khoi tao {false}) - vua khop 104 bit = 13 byte, khong can
  // pad codeword them.
  if (pos != 101) return false;
  for (int i = 0; i < 13; ++i) {
    uint8_t byte = 0;
    for (int b = 0; b < 8; ++b) byte = static_cast<uint8_t>((byte << 1) | (bits[i * 8 + b] ? 1 : 0));
    out[i] = byte;
  }
  return true;
}

void rsEcCodewords(const uint8_t data[13], uint8_t out[13]) {
  uint8_t remainder[26] = {0};
  for (int i = 0; i < 13; ++i) remainder[i] = data[i];
  for (int i = 0; i < 13; ++i) {
    const uint8_t coeff = remainder[i];
    if (coeff == 0) continue;
    for (int j = 0; j < 14; ++j) {
      remainder[i + j] ^= gfMul(GEN_POLY[j], coeff);
    }
  }
  for (int i = 0; i < 13; ++i) out[i] = remainder[13 + i];
}

bool isFunctionModule(int8_t r, int8_t c) {
  if (r < 9 && c < 9) return true;
  if (r < 9 && c >= SIZE - 8) return true;
  if (r >= SIZE - 8 && c < 9) return true;
  if (r == 6 || c == 6) return true;
  return false;
}

void setFinder(bool m[SIZE][SIZE], int8_t top, int8_t left) {
  for (int8_t r = -1; r <= 7; ++r) {
    for (int8_t c = -1; c <= 7; ++c) {
      const int rr = top + r, cc = left + c;
      if (rr < 0 || rr >= SIZE || cc < 0 || cc >= SIZE) continue;
      if (r >= 0 && r <= 6 && c >= 0 && c <= 6) {
        m[rr][cc] = (r == 0 || r == 6 || c == 0 || c == 6 ||
                     (r >= 2 && r <= 4 && c >= 2 && c <= 4));
      } else {
        m[rr][cc] = false;  // vanh trang (separator) quanh finder
      }
    }
  }
}

void buildMatrix(const uint8_t data[13], const uint8_t ec[13], bool m[SIZE][SIZE]) {
  setFinder(m, 0, 0);
  setFinder(m, 0, SIZE - 7);
  setFinder(m, SIZE - 7, 0);
  for (int8_t i = 8; i < SIZE - 8; ++i) {
    m[6][i] = (i % 2 == 0);
    m[i][6] = (i % 2 == 0);
  }
  m[SIZE - 8][8] = true;  // dark module co dinh cua V1

  bool allBits[208];
  int bi = 0;
  for (int k = 0; k < 13; ++k) for (int b = 7; b >= 0; --b) allBits[bi++] = (data[k] >> b) & 1U;
  for (int k = 0; k < 13; ++k) for (int b = 7; b >= 0; --b) allBits[bi++] = (ec[k] >> b) & 1U;

  int bitI = 0;
  int8_t col = SIZE - 1;
  bool goingUp = true;
  while (col > 0) {
    if (col == 6) col -= 1;
    for (int8_t i = 0; i < SIZE; ++i) {
      const int8_t row = goingUp ? static_cast<int8_t>(SIZE - 1 - i) : i;
      for (int8_t cc = col; cc >= col - 1; --cc) {
        if (isFunctionModule(row, cc)) continue;
        const bool bit = (bitI < 208) ? allBits[bitI] : false;
        ++bitI;
        m[row][cc] = bit ^ ((row + cc) % 2 == 0);  // mask 0
      }
    }
    goingUp = !goingUp;
    col -= 2;
  }

  // Format info (co dinh EC=Q/mask=0) - 2 ban sao theo dung vi tri chuan QR.
  static const int8_t FA_R[15] = {8,8,8,8,8,8,8,8,7,5,4,3,2,1,0};
  static const int8_t FA_C[15] = {0,1,2,3,4,5,7,8,8,8,8,8,8,8,8};
  static const int8_t FB_R[15] = {20,19,18,17,16,15,14,8,8,8,8,8,8,8,8};
  static const int8_t FB_C[15] = {8,8,8,8,8,8,8,13,14,15,16,17,18,19,20};
  for (int i = 0; i < 15; ++i) {
    const bool bit = (FORMAT_BITS >> (14 - i)) & 1U;
    m[FA_R[i]][FA_C[i]] = bit;
    m[FB_R[i]][FB_C[i]] = bit;
  }
}

// Ma hoa "text" (PHAI dung 16 ky tu, dinh dang Device ID) vao "m". Tra ve
// false (khong dung "m") neu text sai dinh dang - nguoi goi phai tu kiem
// tra gia tri tra ve, TRANH ve QR rong/sai neu deviceId chua san sang.
bool encode(const char *text, bool m[SIZE][SIZE]) {
  uint8_t data[13], ec[13];
  if (!buildDataCodewords(text, data)) return false;
  rsEcCodewords(data, ec);
  buildMatrix(data, ec, m);
  return true;
}
}  // namespace QrMapId

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
  uint8_t emergencyPhase = 0;
  uint8_t transientPulsesLeft = 0;
  BuzzerPattern transientPattern{0, 0, 0};
  // Giai dieu ngan phat 1 lan luc khoi dong (xem buzzerPlayStartupChime()).
  // Coi la loai active (chi ON/OFF, khong doi duoc cao do) nen "giai dieu"
  // o day la mot nhip do dai/khoang lang khac nhau, khong phai cac not nhac
  // that su - xem chu thich STARTUP_CHIME_MS.
  bool startupActive = false;
  uint8_t startupPhase = 0;
  // "Coi thong minh" cho AlarmTempHigh (xem buzzerUpdate()): thay vi keu lai
  // theo 1 hen gio co dinh (CRITICAL_RESOUND_MS) bat ke nhiet do dang lam gi,
  // theo doi xu huong nhiet tu luc ACK - con dang GIAM thi giu im (ma loi van
  // hien tren man hinh), nhiet dung yen hoac tang tro lai thi keu lai NGAY.
  float tempHighAckBestTemp = NAN;
  uint32_t tempHighAckLastSampleAt = 0;
};

BuzzerState buzzer;

void buzzerBegin();
void buzzerUpdate(uint32_t now);
void buzzerPlayCue(BuzzerCue cue);
void buzzerAcknowledge(uint32_t alarmMask);
void buzzerPlayStartupChime();
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

  // ---- CAI DAT CHUNG > NHIET DO (5 muc) ----
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
  // Bu sai so cam bien (config_.tempOffset) - da co san tren web (form Cam
  // bien) va da luu EEPROM tu truoc, chi moi them vao HMI de chinh truc tiep
  // tren may khong can mo web. Dung chung field/gioi han voi machine_control.h
  // sanitizeConfig() (clampFloat -5..5).
  ITEM_FLOAT("Bu nhiet do", tempOffset, -5.0f, 5.0f, 0.1f, 1, "C"),            // 7
  // BAT (mac dinh): Bao cao/Bao khan cap hoat dong moi luc, ke ca khong co
  // me ap. TAT: 2 canh bao nay CHI kiem tra khi dang co me (giong Bao thap).
  ITEM_BOOL("Bao nhiet ngoai me", highTempAlarmWithoutBatch),                  // 8

  // ---- CAI DAT CHUNG > QUAT HUT (2 muc) ----
  ITEM_FLOAT("Quat hut bat", ventOnTemp, TARGET_TEMP_MIN_C + VENT_ON_ABOVE_SV_C,
             HIGH_ALARM_MAX_C, 0.1f, 1, "C"),                                  // 9
  ITEM_FLOAT("Quat hut tat", ventOffTemp, TARGET_TEMP_MIN_C,
             HIGH_ALARM_MAX_C, 0.1f, 1, "C"),                                  // 10

  // ---- CAI DAT CHUNG > DAO TRUNG (3 muc, + "So lan dao" la dong phu) ----
  ITEM_BOOL("Tu dong dao", turningEnabled),                                    // 11
  ITEM_U16("Chu ky dao", turnIntervalMin, 1, 720, 1, "ph"),                   // 12
  ITEM_U16("Tre loi dao", turnMaxRunSec, 5, 600, 5, "s"),                    // 13

  // ---- CAI DAT CHUNG > KET NOI (1 muc, + "Doi wifi" la dong phu) ----
  ITEM_U8_OPTIONS("Che do ket noi", connectivityMode,
                  OPT_OFFLINE_ONLINE, 2)                                      // 14
};

constexpr uint8_t SETTING_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);
static_assert(SETTING_COUNT == 15, "Bang SETTINGS phai co 15 thong so");

const uint8_t GROUP_SETTING_INDEXES[] = {
  0,1,2,3,                             // Cai dat me
  4,5,6,7,8,                           // Nhiet do
  9,10,                                // Quat hut
  11,12,13,                            // Dao trung
  14                                    // Ket noi
};

struct SettingGroup { const char *label; uint8_t first; uint8_t count; };
// Chi so 0 = Cai dat me (goc tu MainMenu); 1..4 = 4 thu muc con cua
// "CAI DAT CHUNG" (goc tu ChungMenu). Dung chung mot co che SettingList.
const SettingGroup GROUPS[] = {
  {"CAI DAT ME", 0, 4},
  {"NHIET DO", 4, 5},
  {"QUAT HUT", 9, 2},
  {"DAO TRUNG", 11, 3},
  {"KET NOI", 14, 1}
};
constexpr uint8_t GROUP_COUNT = sizeof(GROUPS) / sizeof(GROUPS[0]);
static_assert(GROUP_COUNT == 5, "Bang GROUPS phai co 5 nhom");
static_assert(sizeof(GROUP_SETTING_INDEXES) / sizeof(GROUP_SETTING_INDEXES[0]) == SETTING_COUNT,
              "Sai so luong tham chieu setting trong GROUP_SETTING_INDEXES");

// Dong phu (khong phai setting gia tri) duoc gan them vao cuoi mot so nhom.
// Moi nhom co toi da 3 dong phu (hien tai chi KET NOI dung ca 3) - liet ke
// theo THU TU CO DINH qua groupExtraSlot(), roi loc bot dong dang AN (vd
// "Doi wifi" chi hien khi Online) qua visibleGroupExtraAt() de ra danh sach
// LIEN TUC hien thi tren man hinh. (groupExtraSlotVisible/visibleGroupExtraAt/
// groupVisibleExtraCount dat o DUOI, sau khai bao currentConfig - xem do.)
enum class GroupExtra : uint8_t { None, TurnStats, WifiChange, ConnectionInfo, QrCode };
GroupExtra groupExtraSlot(uint8_t group, uint8_t slot) {
  if (group == 3 && slot == 0) return GroupExtra::TurnStats;      // DAO TRUNG -> "So lan dao"
  if (group == 4 && slot == 0) return GroupExtra::ConnectionInfo; // KET NOI -> "Thong tin ket noi"
  if (group == 4 && slot == 1) return GroupExtra::QrCode;         // KET NOI -> "Ma QR ID"
  if (group == 4 && slot == 2) return GroupExtra::WifiChange;     // KET NOI -> "Doi wifi"
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

// Chi rang buoc theo SV (targetTemp) - gia tri LUON moi nhat - thay vi rang
// buoc CHEO giua cac nguong voi nhau (vd Bao cao truoc day bi khoa toi thieu
// bang Hut bat). Kieu rang buoc cheo cu co 1 loi thuc te: neu nguoi dung doi
// SV qua trang wed bang form "nhanh" (chi doi rieng SV, khong keo theo cac
// nguong con lai), cac nguong con lai tro thanh "cu" - va sau do tren HMI,
// nguoi dung KHONG THE chinh 1 nguong nao do ve gan SV moi duoc nua vi bi
// chinh cai nguong lac hau do khoa lai (vd: SV=30 nhung Bao cao chi cho
// chinh xuong toi 38.1, dung ra phai xuong duoc 30.1). Thu tu tuong doi
// giua cac nguong (Bao thap<SV<=Hut tat<Hut bat<=Bao cao<Khan cap) van duoc
// dam bao dung tai thoi diem LUU thuc su qua sanitizeMachineConfig() trong
// machine_control.h - ham do luon tinh lai toan bo chuoi tu SV moi nhat va
// tu keo cac nguong con lai ve dung vi tri, khong can khoa nguoi dung lai o
// day. Xem thêm buildConfig() trong app.js - form "nhanh" tren wed cung da
// duoc sua de tu keo theo cac nguong nay khi doi SV, tranh loi lac hau xay
// ra tu dau.
void settingLimits(const MachineConfig &cfg, const SettingItem &item,
                   float &minimum, float &maximum) {
  minimum = item.minimum; maximum = item.maximum;
  const uint16_t offset = item.offset;
  if (offset == offsetof(MachineConfig, lowTempAlarm)) {
    maximum = fminf(maximum, cfg.targetTemp - LOW_ALARM_GAP_C);
  } else if (offset == offsetof(MachineConfig, highTempAlarm)) {
    minimum = fmaxf(minimum, cfg.targetTemp + HIGH_ALARM_GAP_C);
  } else if (offset == offsetof(MachineConfig, emergencyTemp)) {
    minimum = fmaxf(minimum,
        cfg.targetTemp + HIGH_ALARM_GAP_C + EMERGENCY_ABOVE_HIGH_C);
  } else if (offset == offsetof(MachineConfig, ventOnTemp)) {
    minimum = fmaxf(minimum, cfg.targetTemp + VENT_ON_ABOVE_SV_C);
  } else if (offset == offsetof(MachineConfig, ventOffTemp)) {
    minimum = fmaxf(minimum, cfg.targetTemp + VENT_OFF_ABOVE_SV_C);
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
  EventLog, Alarm, TestMode, TestSummary, WifiChange, ConnectionInfo, QrCode
};

enum class ConfirmAction : uint8_t { None, BatchToggle, AutoTuneStart, ResumeBatch, TurningToggle };

// Prototype thu cong: Arduino IDE tu sinh prototype cho ham trong .ino.
// Neu ham dung enum/struct tuy chinh, prototype tu dong co the bi chen
// truoc noi khai bao kieu va gay loi "View was not declared".
void openBatchConfirm(View returnView);
void openResumeConfirm();
void openAlarmView(View returnView);

MachineConfig currentConfig;
MachineRuntime currentRuntime;
HmiEventSnapshot currentEventLog;

// Tiep tuc dinh nghia GroupExtra (xem groupExtraSlot() o tren) - 3 ham nay
// doc currentConfig nen phai dat SAU khai bao no.
bool groupExtraSlotVisible(GroupExtra extra) {
  if (extra == GroupExtra::WifiChange) {
    return currentConfig.connectivityMode == ConnectivityMode::Online;
  }
  return extra != GroupExtra::None;
}
GroupExtra visibleGroupExtraAt(uint8_t group, uint8_t visibleIdx) {
  uint8_t seen = 0;
  for (uint8_t slot = 0; slot < 3U; ++slot) {
    const GroupExtra extra = groupExtraSlot(group, slot);
    if (!groupExtraSlotVisible(extra)) continue;
    if (seen == visibleIdx) return extra;
    ++seen;
  }
  return GroupExtra::None;
}
uint8_t groupVisibleExtraCount(uint8_t group) {
  uint8_t count = 0;
  while (visibleGroupExtraAt(group, count) != GroupExtra::None) ++count;
  return count;
}

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

constexpr uint8_t TEST_MODE_OUTPUT_ROWS = static_cast<uint8_t>(TestOutputId::Count);
constexpr uint8_t TEST_MODE_LIMIT_ROWS = 2U;
constexpr uint8_t TEST_MODE_ITEM_COUNT = static_cast<uint8_t>(
    TEST_MODE_OUTPUT_ROWS + TEST_MODE_LIMIT_ROWS + 1U);  // + Ket thuc
constexpr uint8_t TEST_SUMMARY_ITEM_COUNT = static_cast<uint8_t>(
    TEST_MODE_OUTPUT_ROWS + TEST_MODE_LIMIT_ROWS);

// Ket qua nguoi dung tu xac nhan cho tung thiet bi/cong tac hanh trinh, dung
// de tong ket lai trong View::TestSummary. Rieng cua HMI, khong can dong bo
// nguoc ve firmware tong vi day la tu danh gia bang mat/tai cua nguoi lap dat.
enum class TestResult : uint8_t { Untested, Pass, Fail };
TestResult testDeviceResult[TEST_MODE_OUTPUT_ROWS] = {};
TestResult testLimitResult[TEST_MODE_LIMIT_ROWS] = {};
TestLimitPhase lastObservedTestLimitPhase = TestLimitPhase::Idle;
bool testDeviceConfirmActive = false;
uint8_t testDeviceConfirmIndex = 0;
bool testDeviceConfirmYes = true;
uint8_t testSummaryIndex = 0;
float editValue = 0;
uint8_t editSettingIndex = 0;
ConfirmAction confirmAction = ConfirmAction::None;
View confirmReturnView = View::Home;
bool confirmYes = true;
// Cau hinh dang cho xac nhan CO/HUY cho ConfirmAction::TurningToggle (bat/tat
// dao tu dong) - chi ap dung khi thao tac nay, khong dung cho cac truong khac.
MachineConfig pendingTurningConfig;
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
// Man hinh khoi dong: thoat khi DA nhan du ca runtime lan config that (de man
// chinh hien ra la da day du so lieu, khong con o trong) va da qua
// SPLASH_MIN_MS; hoac het SPLASH_MAX_MS thi thoat du chua nhan duoc gi.
bool splashActive = true;
uint32_t splashStartedAt = 0;
bool splashHadRuntime = false;
bool splashHadConfig = false;
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
    case MAIN_THU_NGHIEM: return "CHE DO TEST";
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

const char *groupExtraLabelFor(GroupExtra extra) {
  switch (extra) {
    case GroupExtra::TurnStats: return "So lan dao";
    case GroupExtra::WifiChange: return "Doi wifi";
    case GroupExtra::ConnectionInfo: return "Thong tin ket noi";
    case GroupExtra::QrCode: return "Ma QR ID";
    default: return "";
  }
}
bool settingLockedDuringBatch(uint8_t settingIndex) {
  if (!currentRuntime.batchRunning) return false;
  // Chi con khoa so ngay ap tong (thay doi giua chung se lam sai lich/ngay
  // du kien no). Cac tham so dao (bat/tat dao tu dong, chu ky, thoi gian
  // hanh trinh) KHONG con bi khoa khi dang ap nua - cho phep chinh nhu binh
  // thuong, rieng bat/tat dao tu dong se hoi CO/HUY truoc khi ap dung (xem
  // openTurningToggleConfirm()) vi day la thay doi anh huong truc tiep den
  // dao trung dang chay. So sanh theo offset field (khong phai chi so cung
  // trong mang SETTINGS[]) de khong vo tinh khoa nham muc khac neu sau nay
  // them/xoa/doi cho thong so trong bang (da tung la loi thuc te khi them
  // "Bu nhiet do").
  const uint16_t offset = SETTINGS[settingIndex].offset;
  return offset == offsetof(MachineConfig, totalIncubationDays);
}

uint8_t settingListItemCount(uint8_t group) {
  return static_cast<uint8_t>(
      GROUPS[group].count + groupVisibleExtraCount(group) + 1U);
}

uint8_t settingListExitIndex(uint8_t group) {
  return static_cast<uint8_t>(
      GROUPS[group].count + groupVisibleExtraCount(group));
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
                     a == HmiCommandType::TestOutputPulse || a == HmiCommandType::TestOutputStop ||
                     a == HmiCommandType::TestLimitStart || a == HmiCommandType::TestLimitCancel;
  const bool bTest = b == HmiCommandType::TestModeEnter || b == HmiCommandType::TestModeExit ||
                     b == HmiCommandType::TestOutputPulse || b == HmiCommandType::TestOutputStop ||
                     b == HmiCommandType::TestLimitStart || b == HmiCommandType::TestLimitCancel;
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
    case View::TestSummary:
      queueCommand(HmiCommandType::TestModeExit);
      view = View::MainMenu;
      mainIndex = MAIN_THU_NGHIEM;
      alignMainMenuWindow();
      break;
    case View::WifiChange:
      queueCommand(HmiCommandType::WifiPortalCancel);
      view = View::SettingList;
      selectedGroup = 4U;  // KET NOI
      // "Doi wifi" la dong phu CUOI cung cua nhom (sau "Thong tin ket noi").
      setListSelection(static_cast<int>(GROUPS[4U].count + groupVisibleExtraCount(4U)) - 1,
                        settingListItemCount(4U));
      break;
    case View::ConnectionInfo:
      view = View::SettingList;
      selectedGroup = 4U;  // KET NOI
      setListSelection(GROUPS[4U].count, settingListItemCount(4U));
      break;
    case View::QrCode:
      view = View::SettingList;
      selectedGroup = 4U;  // KET NOI
      // "Ma QR ID" la dong phu THU 2 (sau "Thong tin ket noi", truoc "Doi wifi").
      setListSelection(static_cast<int>(GROUPS[4U].count) + 1, settingListItemCount(4U));
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
    showToast("DANG CO ME - KHONG TEST DUOC", true);
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
    testDeviceConfirmActive = false;
    for (auto &r : testDeviceResult) r = TestResult::Untested;
    for (auto &r : testLimitResult) r = TestResult::Untested;
    lastObservedTestLimitPhase = TestLimitPhase::Idle;
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

// Bat/tat "Dao tu dong" gio hoi CO/HUY truoc khi ap dung, giong het thao tac
// BAT DAU/DUNG ME - vi day la thay doi anh huong truc tiep den viec dao
// trung dang chay, khong nen ap ngay chi tu 1 lan xoay/bam nham. `candidate`
// da duoc tinh san (gia tri turningEnabled moi) boi commitSetting().
void openTurningToggleConfirm(const MachineConfig &candidate, View returnView) {
  pendingTurningConfig = candidate;
  confirmAction = ConfirmAction::TurningToggle;
  confirmReturnView = returnView;
  // Bat dao: mac dinh DONG Y. Tat dao giua luc dang co the dang ap: mac dinh
  // HUY de tranh vo tinh tat mat dao trung.
  confirmYes = candidate.turningEnabled;
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

// "Giai dieu" khoi dong: coi la loai active (xem ghi chu tren) nen KHONG the
// doi cao do - "giai dieu" o day la mot NHIP rieng (do dai bip/khoang lang
// tang dan) de nghe khac han moi tin hieu coi khac (Key/Save/Ok/Error/canh
// bao), phat DUNG 1 LAN luc bat may. Ngan (~800ms) theo dung yeu cau.
constexpr uint8_t STARTUP_CHIME_STEPS = 7U;
constexpr bool STARTUP_CHIME_ON[STARTUP_CHIME_STEPS] =
    {true, false, true, false, true, false, true};
constexpr uint16_t STARTUP_CHIME_MS[STARTUP_CHIME_STEPS] =
    {70, 70, 70, 70, 140, 90, 280};

void buzzerPlayStartupChime() {
  buzzer.startupActive = true;
  buzzer.startupPhase = 0U;
  buzzerWrite(true);
  buzzer.deadline = millis() + STARTUP_CHIME_MS[0];
}

BuzzerPattern buzzerCuePattern(BuzzerCue cue) {
  switch (cue) {
    // Coi la loai active (tu dao dong, khong chinh duoc cao do/am luong qua
    // PWM), nen cach duy nhat de "diu" tieng la rut ngan thoi gian xung. Key
    // vang o moi lan bam nen phai la tieng "tach" that nhe, khong phai tieng "bip".
    // pulses=0 -> buzzerPlayCue() tu bo qua (xem "if (!pattern.pulses) return;")
    // - bo han tieng khi nhan phim theo yeu cau, khong can dung tung noi goi.
    case BuzzerCue::Key:   return {0, 0, 0};
    case BuzzerCue::Save:  return {10, 0, 1};      // Bip cuc ngan, khong gay on
    case BuzzerCue::Ok:    return {35, 50, 2};     // Hai bip nhe
    case BuzzerCue::Error: return {90, 90, 3};     // Van giu 3 bip ro rang de phan biet loi (an toan)
    default:               return {0, 0, 0};
  }
}

BuzzerPattern alarmPattern(uint32_t bit) {
  // Coi HMI la loai active (tu dao dong san, xem ghi chu o buzzerCuePattern())
  // nen KHONG chinh duoc do to nho/dB qua phan mem - da rut ngan het muc thoi
  // gian xung (onMs) con lai gan mot nua so voi truoc, giam tong thoi gian
  // keu that su moi lan bao (van du de nghe thay va phan biet cac muc do).
  switch (bit) {
    case AlarmEmergency:   return {450, 100, 0};   // Gan lien tuc, uu tien cao nhat
    case AlarmSystem:      return {300, 200, 0};
    case AlarmAutoMode:    return {150, 700, 0};
    case AlarmSensor:      return {220, 300, 0};
    case AlarmTurning:     return {200, 400, 0};
    case AlarmTempHigh:    return {175, 500, 0};
    case AlarmTempLow:     return {110, 900, 0};
    case AlarmHumidityLow: return {60, 2880, 0};  // Het nuoc: nhac nho thua
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
  if (alarmMask & AlarmTempHigh) {
    // Moc goc cho "coi thong minh" (xem buzzerUpdate()) - lay nhiet do ngay
    // luc ACK lam moc so sanh dau tien.
    buzzer.tempHighAckBestTemp = (currentRuntime.sensorOnline &&
                                  isfinite(currentRuntime.temperature))
        ? currentRuntime.temperature : NAN;
    buzzer.tempHighAckLastSampleAt = now;
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
  if (alarmMask & AlarmTempHigh) {
    buzzer.tempHighAckBestTemp = NAN;
    buzzer.tempHighAckLastSampleAt = 0;
  }
  buzzer.soundingAlarmBit = AlarmNone;
}

// LUU Y: AlarmTempHigh KHONG dung ham nay - no co logic rieng theo doi xu
// huong nhiet ("coi thong minh", xem nhanh rieng trong buzzerUpdate()) thay
// vi hen gio co dinh.
uint32_t alarmRepeatMs(uint32_t bit) {
  if (bit == AlarmEmergency) return EMERGENCY_RESOUND_MS;
  if (bit == AlarmAutoMode) return AUTO_LOST_RESOUND_MS;
  if (bit == AlarmSystem || bit == AlarmSensor || bit == AlarmTurning) {
    return CRITICAL_RESOUND_MS;
  }
  return 0;
}

void buzzerUpdate(uint32_t now) {
  // Giai dieu khoi dong duoc uu tien tuyet doi trong luc con hieu luc, NHUNG
  // luon dung ngay khi roi man khoi dong (vao View::Home / SPLASH_MAX_MS ep
  // thoat som) - khong bao gio de no lan sang canh bao that hoac coi thao
  // tac binh thuong.
  if (buzzer.startupActive) {
    if (!splashActive) {
      buzzer.startupActive = false;
      buzzerWrite(false);
    } else if (static_cast<int32_t>(now - buzzer.deadline) >= 0) {
      const uint8_t next = static_cast<uint8_t>(buzzer.startupPhase + 1U);
      if (next >= STARTUP_CHIME_STEPS) {
        buzzer.startupActive = false;
        buzzerWrite(false);
      } else {
        buzzer.startupPhase = next;
        buzzerWrite(STARTUP_CHIME_ON[next]);
        buzzer.deadline = now + STARTUP_CHIME_MS[next];
      }
    }
    return;
  }
  // Bit da het loi tu dong mat ACK. Loi tai xuat hien se keu lai.
  buzzer.acknowledgedAlarmMask &= currentRuntime.alarmMask;
  if (!(buzzer.acknowledgedAlarmMask & AlarmTempHigh)) {
    buzzer.tempHighAckBestTemp = NAN;
    buzzer.tempHighAckLastSampleAt = 0;
  }
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    const uint32_t bit = ALARM_PRIORITY[i];
    if (!(buzzer.acknowledgedAlarmMask & bit)) {
      buzzer.acknowledgedAt[i] = 0;
      continue;
    }
    if (bit == AlarmTempHigh) {
      // "Coi thong minh": khac moi bit khac (hen gio co dinh CRITICAL_
      // RESOUND_MS ben duoi), rieng nhiet do cao theo doi XU HUONG - con dang
      // giam thi giu im (ma loi E111 van hien tren man hinh binh thuong,
      // khong bi xoa), dung yen hoac tang tro lai thi keu lai NGAY khong doi
      // het gio. Mat cam bien giua luc dang im lang thi khong the biet xu
      // huong nua - an toan hon la keu lai ngay.
      const bool haveTemp = currentRuntime.sensorOnline &&
                            isfinite(currentRuntime.temperature);
      bool resoundNow = !haveTemp;
      if (haveTemp && timeReached(now, buzzer.tempHighAckLastSampleAt +
                                       TEMP_HIGH_SMART_MUTE_SAMPLE_MS)) {
        buzzer.tempHighAckLastSampleAt = now;
        if (!isfinite(buzzer.tempHighAckBestTemp)) {
          buzzer.tempHighAckBestTemp = currentRuntime.temperature;
        } else if (currentRuntime.temperature <=
                   buzzer.tempHighAckBestTemp - TEMP_HIGH_SMART_MUTE_EPSILON_C) {
          buzzer.tempHighAckBestTemp = currentRuntime.temperature;  // van giam that
        } else {
          resoundNow = true;  // dung yen hoac tang tro lai
        }
      }
      if (resoundNow) {
        buzzer.acknowledgedAlarmMask &= ~bit;
        buzzer.acknowledgedAt[i] = 0;
        buzzer.tempHighAckBestTemp = NAN;
        buzzer.tempHighAckLastSampleAt = 0;
      }
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

    if (alarmBit == AlarmEmergency) {
      // Nhip "Temporal-3" (ISO 8201) - chuan quoc te cho coi bao chay/khan
      // cap: 3 tieng bip ngan + 1 khoang lang dai hon, lap lai. Nghe RO la
      // canh bao that su ngay lap tuc, thay vi 1 tieng bip don deu deu nhu
      // cac muc canh bao khac (van giu nguyen alarmPattern() cho cac muc do
      // thap hon - chi rieng Khan cap moi dang nhip nay).
      constexpr uint8_t T3_STEPS = 6U;
      constexpr bool T3_ON[T3_STEPS]      = {true, false, true, false, true, false};
      constexpr uint16_t T3_MS[T3_STEPS]  = {450, 450, 450, 450, 450, 1300};
      if (buzzer.soundingAlarmBit != alarmBit) {
        buzzer.soundingAlarmBit = alarmBit;
        buzzer.emergencyPhase = 0U;
        buzzerWrite(true);
        buzzer.deadline = now + T3_MS[0];
        return;
      }
      if (static_cast<int32_t>(now - buzzer.deadline) < 0) return;
      buzzer.emergencyPhase = static_cast<uint8_t>((buzzer.emergencyPhase + 1U) % T3_STEPS);
      buzzerWrite(T3_ON[buzzer.emergencyPhase]);
      buzzer.deadline = now + T3_MS[buzzer.emergencyPhase];
      return;
    }

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
  // Gia tri khong doi thi thoat lang le, khong can bao gi ca - nguoi dung
  // chi xem/luot qua thong so thi khong nen bi lam phien bang 1 dong toast.
  if (fabsf(oldValue - newValue) < 0.0001f) return;
  // Bat/tat "Dao tu dong" hoi CO/HUY truoc khi ap dung (giong Bat dau/Dung
  // me) thay vi luu ngay - danh gia sai lech co the lam mat dao trung giua
  // luc dang ap ma khong ai de y.
  if (item.offset == offsetof(MachineConfig, turningEnabled)) {
    openTurningToggleConfirm(candidate, View::SettingList);
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
// 1) Dang co loi + nhan ngan (ca hai trang): mo trang chi tiet loi (nhu cu).
// 2) Dang co loi + nhan giu TREN TRANG MAY AP: duong thoat khan cap - mo
//    thang xac nhan KET THUC ME, ke ca loi chua het han/chua tu xoa duoc.
// 3) Dang co loi + nhan giu TREN TRANG TRANG THAI: van vao Menu chinh nhu
//    binh thuong - loi da tat coi (ACK) khong duoc chan duong vao menu,
//    chi trang MAY AP moi co loi thoat khan cap rieng.
// 4) Khong co loi, trang MAY AP: mo xac nhan Bat/Dung me (nhu cu).
// 5) Khong co loi, trang TRANG THAI: mo Menu chinh (nhu cu).
void activateHomeContext(bool longPress) {
  if (currentRuntime.activeFaultDisplayCount) {
    if (!longPress) {
      openAlarmView(View::Home);
      return;
    }
    if (homePage == 0U) {
      openBatchConfirm(View::Home);
      return;
    }
    // homePage == 1 (TRANG THAI) + nhan giu: roi xuong nhanh Menu chinh ben duoi.
  }
  if (homePage == 0U) {
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
    } else if (action == ConfirmAction::TurningToggle) {
      if (startConfigSave(pendingTurningConfig)) {
        currentConfig = pendingTurningConfig;
        showToast("DANG LUU...");
      }
      view = returnView;
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
  // Dang o man khoi dong: nuot moi thao tac (khong dieu huong mu trong luc
  // man hinh chua hien menu), tranh vua bat may lo xoay num la da nhay vao
  // mot trang nao do ma khong biet.
  if (splashActive) {
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
        const uint8_t extraCount = groupVisibleExtraCount(selectedGroup);
        if (listIndex < group.count) {
          openSetting();
        } else if (listIndex < group.count + extraCount) {
          const GroupExtra extra = visibleGroupExtraAt(selectedGroup,
              static_cast<uint8_t>(listIndex - group.count));
          if (extra == GroupExtra::TurnStats) {
            view = View::TurnStats;
            dirty = true;
          } else if (extra == GroupExtra::WifiChange) {
            openWifiChange();
          } else if (extra == GroupExtra::ConnectionInfo) {
            view = View::ConnectionInfo;
            dirty = true;
          } else if (extra == GroupExtra::QrCode) {
            view = View::QrCode;
            dirty = true;
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

    case View::ConnectionInfo:
      if (rotary.button == ButtonEvent::ShortPress) goBack();
      break;

    case View::QrCode:
      if (rotary.button == ButtonEvent::ShortPress) goBack();
      break;

    case View::AutoTune:
      if (rotary.button == ButtonEvent::ShortPress) openAutoTuneConfirm();
      break;

    case View::TestMode: {
      // Sau khi xung mot thiet bi, hoi nguoi lap dat CO/KHONG thay no chay -
      // day la buoc "kiem thu" that su thay vi tu tat sau vai giay ma khong
      // ai xac nhan gi. Cong tac hanh trinh khong can hoi vi phan cung tu
      // bao ket qua khach quan (da xu ly rieng qua testLimitPhase).
      if (testDeviceConfirmActive) {
        if (rotary.step) {
          testDeviceConfirmYes = !testDeviceConfirmYes;
          dirty = true;
        }
        if (rotary.button == ButtonEvent::ShortPress) {
          testDeviceResult[testDeviceConfirmIndex] =
              testDeviceConfirmYes ? TestResult::Pass : TestResult::Fail;
          queueCommand(HmiCommandType::TestOutputStop, COMMAND_DEFAULT_VALID_MS,
                      0, static_cast<uint32_t>(testDeviceConfirmIndex));
          testModeLastCommandAt = millis();
          testDeviceConfirmActive = false;
          dirty = true;
        }
        break;
      }
      if (rotary.step) {
        setListSelection(static_cast<int>(listIndex) + rotary.step,
                         TEST_MODE_ITEM_COUNT);
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        if (listIndex < TEST_MODE_OUTPUT_ROWS) {
          queueCommand(HmiCommandType::TestOutputPulse, COMMAND_DEFAULT_VALID_MS,
                      0, static_cast<uint32_t>(listIndex));
          testModeLastCommandAt = millis();
          testDeviceConfirmActive = true;
          testDeviceConfirmIndex = listIndex;
          testDeviceConfirmYes = true;
        } else if (listIndex < TEST_MODE_OUTPUT_ROWS + TEST_MODE_LIMIT_ROWS) {
          testLimitSelected = (listIndex == TEST_MODE_OUTPUT_ROWS)
              ? TestLimitId::Left : TestLimitId::Right;
          queueCommand(HmiCommandType::TestLimitStart, COMMAND_DEFAULT_VALID_MS,
                      0, static_cast<uint32_t>(testLimitSelected));
          testModeLastCommandAt = millis();
        } else {
          // Dong "Ket thuc": xem tong ket ai chay ai khong truoc khi thoat hAn.
          testSummaryIndex = 0;
          view = View::TestSummary;
        }
        dirty = true;
      }
      break;
    }

    case View::TestSummary:
      if (rotary.step) {
        testSummaryIndex = static_cast<uint8_t>(constrain(
            static_cast<int>(testSummaryIndex) + rotary.step, 0,
            TEST_SUMMARY_ITEM_COUNT - 1));
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) goBack();
      break;

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
// showDate=false cho cac man hinh trong luong "cai dat" (CAI DAT CHUNG va
// cac man con cua no) - ngay thang khong can thiet khi dang chinh thong so,
// bot roi mat va nhuong cho tieu de dai hon (xem drawEditSetting()).
void drawHeader(const char *title, bool showDate) {
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
  if (showDate) lcd.drawStr(dateX, 8, date);
  lcd.drawHLine(0, 10, 128);
}
void drawHeader(const char *title) { drawHeader(title, true); }

// ---------------------- Chong tran chu dung chung ----------------------
// Man hinh chi rong 128px. Moi cho ve chuoi co do dai THAY DOI theo du lieu
// (ten loi, ten Wi-Fi, ten thiet bi, noi dung su kien...) deu phai di qua
// day thay vi drawStr() thang, neu khong se bi cat mat chu o bien man hinh
// ma khong bao gi - da tung xay ra o nhieu man khac nhau.
//
// Chon font LON NHAT trong 3 muc ma chuoi van vua maxWidth, va dat font do
// lam font hien hanh. Tra ve be rong that de goi ben ngoai tu dat vi tri.
int16_t fitFontWidth(const char *text, int16_t maxWidth,
                     const uint8_t *big, const uint8_t *mid,
                     const uint8_t *small) {
  if (!text) text = "";
  lcd.setFont(big);
  int16_t w = static_cast<int16_t>(lcd.getStrWidth(text));
  if (w <= maxWidth) return w;
  lcd.setFont(mid);
  w = static_cast<int16_t>(lcd.getStrWidth(text));
  if (w <= maxWidth) return w;
  lcd.setFont(small);
  return static_cast<int16_t>(lcd.getStrWidth(text));
}

// Ve can giua, tu ha co font neu qua dai. maxWidth chua le 2px moi ben.
void drawCenteredFit(int16_t y, const char *text, const uint8_t *big,
                     const uint8_t *mid, const uint8_t *small,
                     int16_t maxWidth = 124) {
  if (!text) text = "";
  const int16_t w = fitFontWidth(text, maxWidth, big, mid, small);
  lcd.drawStr(max(0, (128 - w) / 2), y, text);
}

// Ve can trai tai x, tu ha co font neu qua dai so voi phan man hinh con lai.
void drawLeftFit(int16_t x, int16_t y, const char *text, const uint8_t *big,
                 const uint8_t *mid, const uint8_t *small) {
  if (!text) text = "";
  fitFontWidth(text, static_cast<int16_t>(126 - x), big, mid, small);
  lcd.drawStr(x, y, text);
}

// Nhu drawLeftFit nhung chi dinh RO be rong toi da cho phep (dung khi ben
// phai da co noi dung khac chiem cho, vd cot gia tri trong danh sach cai dat).
void drawLeftFit2(int16_t x, int16_t y, const char *text, int16_t maxRight,
                  const uint8_t *big, const uint8_t *small) {
  if (!text) text = "";
  fitFontWidth(text, static_cast<int16_t>(maxRight - x), big, small, small);
  lcd.drawStr(x, y, text);
}

uint32_t alarmBitForFaultCode(uint16_t code) {
  switch (code) {
    case 101: case 102: case 103: return AlarmSensor;
    case 110: return AlarmTempLow;
    case 111: return AlarmTempHigh;
    case 112: return AlarmEmergency;
    case 113: case 114: return AlarmTempHigh;
    case 115: return AlarmSystem;
    case 120: return AlarmHumidityLow;
    case 121: return AlarmHumidityLow;
    case 130: case 131: case 132: return AlarmSystem;
    case 133: return AlarmAutoMode;
    case 134: return AlarmTurning;
    case 135: case 136: return AlarmSystem;
    case 201: case 202: case 203: case 204: case 205: return AlarmTurning;
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
    case 113: return "NHIET DO BIEN THIEN NHANH";
    case 114: return "NHIET DO KHONG ON DINH";
    case 115: return "THANH NHIET KHONG NONG";
    case 120: return "DO AM THAP";
    case 121: return "DO AM CAO";
    case 130: return "TAT CONG TAC NHIET";
    case 131: return "QUAT TUAN HOAN OFF";
    case 132: return "CAN CHUYEN SANG AUTO";
    case 133: return "AUTO BI TAT GIUA ME";
    case 134: return "TU DONG DAO BI TAT";
    case 135: return "CHO XAC NHAN AP LAI";
    case 136: return "ME QUA HAN AP";
    case 201: return "LOI 2 HANH TRINH";
    case 202: return "DAO QUA THOI GIAN";
    case 203: return "HANH TRINH BI KET";
    case 204: return "XUNG DOT LENH DAO";
    case 205: return "CAN KIEM TRA CO KHI DAO";
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
    case 113: snprintf(out, size, "PV %.1fC LECH NHANH", fault.detail * 0.1f); break;
    case 114: snprintf(out, size, "%d LAN DOI CHIEU/10P", fault.detail); break;
    case 115: snprintf(out, size, "TANG %.1fC/15P (THAP)", fault.detail * 0.1f); break;
    case 120: snprintf(out, size, "AM %.0f%% < %.0f%%", currentRuntime.humidity,
                       currentConfig.lowHumidityAlarm); break;
    case 121: snprintf(out, size, "AM %.0f%% > %.0f%%", currentRuntime.humidity,
                       HUMIDITY_HIGH_ALARM_C); break;
    case 130: snprintf(out, size, "NHIET DA BI KHOA"); break;
    case 131: snprintf(out, size, "MANUAL: HAY BAT QUAT"); break;
    case 132: snprintf(out, size, "PHUC HOI ME DANG CHO"); break;
    case 133: snprintf(out, size, "HAY BAT LAI AUTO"); break;
    case 134: snprintf(out, size, "DAO TU DONG PHAI LUON ON"); break;
    case 135: snprintf(out, size, "%d PHUT CHUA XAC NHAN", fault.detail); break;
    case 136: snprintf(out, size, "QUA %d NGAY", fault.detail); break;
    case 201: snprintf(out, size, "HAI CTHT CUNG TAC DONG"); break;
    case 202: snprintf(out, size, "CHUA CHAM CTHT DICH"); break;
    case 203: snprintf(out, size, "CTHT GOC KHONG NHA"); break;
    case 204: snprintf(out, size, "TRAI VA PHAI CUNG ON"); break;
    case 205: snprintf(out, size, "%d LAN LOI - VAO TEST MODE", fault.detail); break;
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

  // Tu co font neu tieu de qua dai cho 128px (mot vai ten loi - ke ca vai ma
  // co tu truoc, khong chi ma moi them - dai hon du de tran man o co dinh
  // helvB10 truoc day, gay mat chu). Thu tu uu tien: dam/lon truoc, nho dan.
  drawCenteredFit(29, faultTitle(fault.code), u8g2_font_helvB10_tf,
                  u8g2_font_6x12_tf, u8g2_font_5x8_tf);

  faultDetail(fault, detail, sizeof(detail));
  drawCenteredFit(42, detail, u8g2_font_5x8_tf, u8g2_font_5x8_tf,
                  u8g2_font_5x8_tf);
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

// Man hinh khoi dong - chu nho, 3 dong can giua theo chieu doc, khong khung
// vien de nhin gon gang. Hien tu luc bat may den khi da co du so lieu that
// (xem splashActive trong render()).
void drawSplash() {
  char line[24];
  lcd.setDrawColor(1);
  lcd.setFont(u8g2_font_6x12_tf);
  drawCenteredText(22, "DIEU KHIEN MAY AP");

  lcd.setFont(u8g2_font_5x8_tf);
  snprintf(line, sizeof(line), "V%s", MAYAP_FIRMWARE_VERSION);
  drawCenteredText(38, line);

  drawCenteredText(54, "Dang khoi dong...");
}

void drawHomeMain() {
  char text[32];
  // Bo chu "MAY AP" (khong mang thong tin gi them), thay bang gio:phut
  // ngay tai vi tri tieu de - truoc day gio bi nhet vao 1 khe rat hep
  // [36,64] giua tieu de va ngay thang, gio co ca vi tri tieu de rong rai.
  drawHeader(currentRuntime.timeText);

  // Man tong quan: PV la thong tin uu tien so 1. Chia man hinh thanh
  // vung PV ben trai (~70 px) va 3 dong thong tin phu ben phai.
  // Khong dung khung/duong ke de tiet kiem diem anh va giu giao dien thoang.
  constexpr int16_t TEMP_ZONE_WIDTH = 70;
  // Dich cot SV/AM/DAO ra gan mep phai hon de khong bi dinh sat vung nhiet do,
  // giup hai ben man hinh can doi hon. 76 (khong phai 80): cac dong dai nhat
  // ("SV 37.5C", "DAO 720p", "DAO KHOA" - 8 ky tu font 6x12 = 48px) o x=80 se
  // ket thuc dung diem anh cuoi cung 127, dinh sat mep man hinh; 76 chua lai
  // le phai 4px cho can doi va van cach vung nhiet do (rong 70) 6px.
  constexpr int16_t RIGHT_X = 76;

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
  drawHeader("CAI DAT CHUNG", false);
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
  drawHeader(group.label, false);
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
      // Ve GIA TRI truoc (can phai, khong bi ep vao cot cung x=86 nhu truoc),
      // roi moi ve NHAN vao dung phan con lai voi khe ho 4px. Cach cu ep gia
      // tri bat dau tu x=86 co dinh nen nhan dai nhat ("Che do ket noi", ket
      // thuc dung x=86) dinh sat vao gia tri, nhin nhu bi cham chu.
      lcd.setFont(u8g2_font_6x12_tf);
      const int16_t valueW = static_cast<int16_t>(lcd.getStrWidth(value));
      const int16_t valueX = max(2, 126 - valueW);
      lcd.drawStr(valueX, y, value);
      drawLeftFit2(2, y, item.label, static_cast<int16_t>(valueX - 4),
                   u8g2_font_6x12_tf, u8g2_font_5x8_tf);
    } else if (local >= group.count && local < group.count + groupVisibleExtraCount(selectedGroup)) {
      // Dong muc phu (thong tin ket noi/doi wifi/thong ke dao) can trai dong
      // bo voi cac muc cai dat.
      const GroupExtra extra = visibleGroupExtraAt(selectedGroup,
          static_cast<uint8_t>(local - group.count));
      lcd.drawStr(2, y, groupExtraLabelFor(extra));
    } else if (local == exitIndex) {
      // Nut thoat cuon xuong cuoi danh sach, can trai nhu cac dong con lai.
      lcd.drawStr(2, y, "Thoat");
    }

    if (selected) lcd.setDrawColor(1);
  }
}

// Chuyen do manh Wi-Fi (dBm) thanh so vach de nguoi dung khong quen thang do
// dBm van doc duoc nhanh: 4 vach = rat manh, 1 vach = rat yeu, 0 = mat song.
uint8_t rssiToBars(int8_t dbm) {
  if (dbm <= -90) return 0U;
  if (dbm <= -80) return 1U;
  if (dbm <= -70) return 2U;
  if (dbm <= -60) return 3U;
  return 4U;
}

void drawConnectionInfo() {
  char text[28];
  drawHeader("KET NOI", false);
  lcd.setFont(u8g2_font_6x12_tf);

  // ID may: dung chung ham voi network_service.h (mayapDeviceIdText())
  // thay vi doc lai bien deviceId cua realtime_link.h - file nay duoc
  // include TRUOC realtime_link.h trong .ino nen bien do chua khai bao
  // luc bien dich hmi.h (xem comment tai mayapDeviceIdText()).
  snprintf(text, sizeof(text), "ID: %s", mayapDeviceIdText().c_str());
  drawLeftFit(6, 27, text, u8g2_font_6x12_tf, u8g2_font_5x8_tf,
              u8g2_font_5x8_tf);

  if (currentRuntime.networkConnected) {
    snprintf(text, sizeof(text), "WIFI: %s", WiFi.SSID().c_str());
  } else {
    snprintf(text, sizeof(text), "WIFI: CHUA KET NOI");
  }
  drawLeftFit(6, 41, text, u8g2_font_5x8_tf, u8g2_font_5x8_tf,
              u8g2_font_5x8_tf);

  if (currentRuntime.networkConnected) {
    char bars[5];
    const uint8_t filled = rssiToBars(currentRuntime.networkRssiDbm);
    for (uint8_t i = 0; i < 4U; ++i) bars[i] = (i < filled) ? '#' : '.';
    bars[4] = '\0';
    snprintf(text, sizeof(text), "SONG: %s (%d dBm)", bars,
             currentRuntime.networkRssiDbm);
  } else {
    snprintf(text, sizeof(text), "SONG: --");
  }
  drawLeftFit(6, 55, text, u8g2_font_5x8_tf, u8g2_font_5x8_tf,
              u8g2_font_5x8_tf);
}

void drawQrCode() {
  // Device ID khong doi trong suot phien chay (tinh 1 lan tu MAC luc khoi
  // dong) nen chi ma hoa QR MOT LAN duy nhat, khong lam lai moi khung hinh.
  static bool matrix[QrMapId::SIZE][QrMapId::SIZE];
  static bool ok = false;
  static bool computed = false;
  if (!computed) {
    ok = QrMapId::encode(mayapDeviceIdText().c_str(), matrix);
    computed = true;
  }
  if (!ok) {
    drawHeader("MA QR ID", false);
    drawCenteredFit(36, "LOI TAO MA QR", u8g2_font_6x12_tf, u8g2_font_5x8_tf,
                    u8g2_font_5x8_tf);
    return;
  }

  // QR sat canh PHAI, chiem tron chieu cao 64px (khong con le tren/duoi -
  // du thua cua phep chia nguyen don het xuong duoi thay vi chia deu 2 ben,
  // de canh tren QR cham dinh man hinh). Cot con lai ben TRAI danh cho
  // tieu de + ID may (giong nhan thiet bi that: 1 dong huong dan, gach
  // phan cach, roi ma so) - vua giup nguoi dung biet man nay dung de lam
  // gi, vua cho doi chieu bang mat khi khong tien quet. pxSize=2 la toi da
  // co the tren man 64px cao voi vanh trang chuan 4-module bat buoc de dau
  // doc dinh vi duoc (xem giai thich chi tiet tai namespace QrMapId dau
  // file). Thoat man hinh van bang short-press nhu moi man phu khac.
  constexpr uint8_t QUIET = 4;   // vanh trang toi thieu theo chuan QR
  constexpr uint8_t PX = 2;      // pixel LCD / 1 o QR
  constexpr uint8_t TOTAL_MODULES = QrMapId::SIZE + QUIET * 2;
  constexpr uint8_t TOTAL_PX = TOTAL_MODULES * PX;  // 58
  constexpr int16_t OFFSET_X = 128 - TOTAL_PX;      // sat canh phai (x=128)
  constexpr int16_t OFFSET_Y = 0;                   // sat canh tren

  lcd.setDrawColor(1);
  for (uint8_t r = 0; r < QrMapId::SIZE; ++r) {
    for (uint8_t c = 0; c < QrMapId::SIZE; ++c) {
      if (!matrix[r][c]) continue;
      const int16_t x = OFFSET_X + (QUIET + c) * PX;
      const int16_t y = OFFSET_Y + (QUIET + r) * PX;
      lcd.drawBox(x, y, PX, PX);
    }
  }

  // Cot trai (rong OFFSET_X = 70px): tieu de 1 dong NGANG, gach phan cach,
  // roi ID may chia 2 dong 8 ky tu (vua khop font 6x12 trong 70px, de doc
  // hon xep doc tung chu roi rac truoc day). Can giua CUA RIENG COT DO
  // (khong phai giua ca man 128px nhu drawCenteredFit() mac dinh).
  const int16_t labelMaxWidth = static_cast<int16_t>(OFFSET_X - 4);
  const auto drawColCentered = [&](int16_t y, const char *text) {
    const int16_t w = fitFontWidth(text, labelMaxWidth, u8g2_font_6x12_tf,
                                   u8g2_font_6x12_tf, u8g2_font_5x8_tf);
    lcd.drawStr(max(0, (OFFSET_X - w) / 2), y, text);
  };

  drawColCentered(14, "QUET MA QR");
  lcd.drawHLine(6, 19, OFFSET_X - 12);

  // Device ID luon dung 16 ky tu hop le tai day (QrMapId::encode() da tra
  // ve true o tren, nen khong can kiem tra lai) - chia doi 8+8 de vua font.
  const String idStr = mayapDeviceIdText();
  char idLine1[9];
  char idLine2[9];
  snprintf(idLine1, sizeof(idLine1), "%.8s", idStr.c_str());
  snprintf(idLine2, sizeof(idLine2), "%.8s", idStr.c_str() + 8);
  drawColCentered(38, idLine1);
  drawColCentered(52, idLine2);
}

void drawTurnStats() {
  char text[28];
  drawHeader("SO LAN DAO", false);
  lcd.setFont(u8g2_font_6x12_tf);

  // Cac dong nay chua so dem co the rat dai (turnCountBatch la uint32) - di
  // qua drawLeftFit de du du lieu bat thuong cung khong tran ra ngoai man.
  snprintf(text, sizeof(text), "HOM NAY: %u LAN",
           currentRuntime.turnCountToday);
  drawLeftFit(6, 29, text, u8g2_font_6x12_tf, u8g2_font_5x8_tf,
              u8g2_font_5x8_tf);

  snprintf(text, sizeof(text), "TONG ME: %lu LAN",
           static_cast<unsigned long>(currentRuntime.turnCountBatch));
  drawLeftFit(6, 43, text, u8g2_font_6x12_tf, u8g2_font_5x8_tf,
              u8g2_font_5x8_tf);

  if (currentRuntime.turningLockdown) snprintf(text, sizeof(text), "DAO TIEP: KHOA");
  else if (currentRuntime.nextTurnScheduled) snprintf(text, sizeof(text), "DAO TIEP: %u PH", currentRuntime.nextTurnMinutes);
  else snprintf(text, sizeof(text), "DAO TIEP: --");
  drawLeftFit(6, 58, text, u8g2_font_5x8_tf, u8g2_font_5x8_tf,
              u8g2_font_5x8_tf);
}

void drawEditSetting() {
  const SettingItem &item = SETTINGS[editSettingIndex];
  char value[24];
  // "SUA THONG SO" (khong phai "CHINH THONG SO") - tieu de cu dai 14 ky tu,
  // vuot gioi han cat chuoi 13 ky tu cua drawHeader() nen bi rung mat chu
  // "O" cuoi cung ("...THONG S"), day chinh la loi nguoi dung bao.
  drawHeader("SUA THONG SO", false);
  drawCenteredFit(23, item.label, u8g2_font_6x12_tf, u8g2_font_5x8_tf,
                  u8g2_font_5x8_tf);
  formatSettingValue(item, editValue, value, sizeof(value));

  // Bo icon cong tac do hoa (toggle switch) khoi toan bo HMI - moi muc Bool
  // (ke ca "Ap lai") deu chi hien chu "BAT"/"TAT" to, ro rang, dong nhat
  // cach hien thi voi cac loai gia tri khac (khong con nhanh rieng).
  drawCenteredFit(48, value, u8g2_font_helvB14_tf, u8g2_font_helvB12_tf,
                  u8g2_font_6x12_tf);
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
  drawHeader("TU CHINH PID", false);
  drawCenteredFit(29, autoTuneStateText(currentRuntime.autoTuneState),
                  u8g2_font_helvB12_tf, u8g2_font_6x12_tf, u8g2_font_5x8_tf);

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
  drawCenteredFit(46, text, u8g2_font_6x12_tf, u8g2_font_5x8_tf,
                  u8g2_font_5x8_tf);
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

const char *testResultTag(TestResult result) {
  switch (result) {
    case TestResult::Pass: return "OK";
    case TestResult::Fail: return "KHONG";
    default: return "...";
  }
}

// 2 nut lon dung chung cho moi man xac nhan Co/Khong tren HMI (dung lai o
// day va drawConfirmScreen() ben duoi) - chu nhan nam gon giua o, o bo goc
// tron cho mem mat, de nhan dien nhanh.
void drawYesNoButtons(bool yesSelected, const char *yesLabel, const char *noLabel) {
  constexpr int16_t btnY = 40, btnH = 20, margin = 4, gap = 6, radius = 4;
  constexpr int16_t btnW = (128 - 2 * margin - gap) / 2;
  static_assert(btnW >= 2 * radius + 1 && btnH >= 2 * radius + 1,
                "Nut qua nho so voi ban kinh bo goc (drawRBox se ve sai)");
  const int16_t yesX = margin;
  const int16_t noX = margin + btnW + gap;

  // Font nho hon truoc (helvB10 thay helvB12) va can giua CA HAI CHIEU trong
  // o: baseline tinh theo getAscent() cua chinh font dang dung thay vi hang
  // so btnY+15 co dinh - doi font sau nay van tu can dung, khong bi lech.
  lcd.setFont(u8g2_font_helvB10_tf);
  const int16_t ascent = static_cast<int16_t>(lcd.getAscent());
  const int16_t baseline = btnY + (btnH + ascent) / 2;

  lcd.setDrawColor(1);
  if (yesSelected) lcd.drawRBox(yesX, btnY, btnW, btnH, radius);
  else lcd.drawRFrame(yesX, btnY, btnW, btnH, radius);
  lcd.setDrawColor(yesSelected ? 0 : 1);
  {
    const int16_t tw = static_cast<int16_t>(lcd.getStrWidth(yesLabel));
    lcd.drawStr(yesX + max(0, (btnW - tw) / 2), baseline, yesLabel);
  }

  lcd.setDrawColor(1);
  if (!yesSelected) lcd.drawRBox(noX, btnY, btnW, btnH, radius);
  else lcd.drawRFrame(noX, btnY, btnW, btnH, radius);
  lcd.setDrawColor(yesSelected ? 1 : 0);
  {
    const int16_t tw = static_cast<int16_t>(lcd.getStrWidth(noLabel));
    lcd.drawStr(noX + max(0, (btnW - tw) / 2), baseline, noLabel);
  }
  lcd.setDrawColor(1);
}

void drawTestDeviceConfirm() {
  drawHeader("TEST");
  lcd.setFont(u8g2_font_6x12_tf);
  drawCenteredText(24, testOutputLabel(testDeviceConfirmIndex));
  drawCenteredText(36, "CO CHAY KHONG?");
  drawYesNoButtons(testDeviceConfirmYes, "CO", "KHONG");
}

void drawTestMode() {
  if (testDeviceConfirmActive) { drawTestDeviceConfirm(); return; }
  drawHeader("TEST");
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
      const char *tag = on ? "BAT" : testResultTag(testDeviceResult[local]);
      lcd.drawStr(2, y, testOutputLabel(local));
      lcd.drawStr(max(80, 126 - static_cast<int16_t>(lcd.getStrWidth(tag))), y, tag);
    } else if (local < TEST_MODE_OUTPUT_ROWS + TEST_MODE_LIMIT_ROWS) {
      const uint8_t limitIdx = static_cast<uint8_t>(local - TEST_MODE_OUTPUT_ROWS);
      const TestLimitId id = static_cast<TestLimitId>(limitIdx);
      snprintf(line, sizeof(line), "CTHT %s", id == TestLimitId::Left ? "TRAI" : "PHAI");
      lcd.drawStr(2, y, line);
      const char *tag = (currentRuntime.testLimitTarget == id &&
                         currentRuntime.testLimitPhase == TestLimitPhase::Waiting)
          ? "..." : testResultTag(testLimitResult[limitIdx]);
      lcd.drawStr(max(80, 126 - static_cast<int16_t>(lcd.getStrWidth(tag))), y, tag);
    } else {
      // Doi tu "Thoat" sang "Ket thuc": bam vao day de xem tong ket thiet bi
      // nao da chay, thiet bi nao chua/khong chay truoc khi roi han.
      lcd.drawStr(2, y, "Ket thuc");
    }
    if (selected) lcd.setDrawColor(1);
  }
}

void drawTestSummary() {
  drawHeader("TONG KET TEST");
  lcd.setFont(u8g2_font_6x12_tf);
  char line[24];
  for (uint8_t row = 0; row < 4 && testSummaryIndex / 4 * 4 + row < TEST_SUMMARY_ITEM_COUNT; ++row) {
    const uint8_t local = static_cast<uint8_t>(testSummaryIndex / 4 * 4 + row);
    const int16_t y = 22 + row * 12;
    const bool selected = local == testSummaryIndex;
    if (selected) {
      lcd.drawBox(0, y - 9, 128, 11);
      lcd.setDrawColor(0);
    }
    const char *tag;
    if (local < TEST_MODE_OUTPUT_ROWS) {
      lcd.drawStr(2, y, testOutputLabel(local));
      tag = testResultTag(testDeviceResult[local]);
    } else {
      const uint8_t limitIdx = static_cast<uint8_t>(local - TEST_MODE_OUTPUT_ROWS);
      snprintf(line, sizeof(line), "CTHT %s", limitIdx == 0U ? "TRAI" : "PHAI");
      lcd.drawStr(2, y, line);
      tag = testResultTag(testLimitResult[limitIdx]);
    }
    lcd.drawStr(max(80, 126 - static_cast<int16_t>(lcd.getStrWidth(tag))), y, tag);
    if (selected) lcd.setDrawColor(1);
  }
}

void drawWifiChange() {
  drawHeader("DOI WIFI");
  lcd.setFont(u8g2_font_6x12_tf);
  const WifiPortalState state = currentRuntime.wifiPortalState;
  const char *title = "DANG MO CONG...";
  if (state == WifiPortalState::ApActive) title = "KET NOI VA DOI WIFI";
  else if (state == WifiPortalState::Testing) title = "DANG THU KET NOI...";
  else if (state == WifiPortalState::Success) title = "DA KET NOI!";
  else if (state == WifiPortalState::Failed) title = "KHONG THE KET NOI";
  drawCenteredFit(24, title, u8g2_font_6x12_tf, u8g2_font_5x8_tf,
                  u8g2_font_5x8_tf);

  // Cac dong huong dan ben duoi truoc day ve thang bang drawStr() o font
  // 6x12/5x8 co dinh, mot so cau dai toi ~170px (vd "Hay thu lai voi mang/
  // mat khau khac") nen bi cat cut o mep phai. Gio tach thanh 2 dong ngan va
  // van di qua drawCenteredFit() de chac chan khong bao gio tran.
  char line[40];
  if (state == WifiPortalState::ApActive || state == WifiPortalState::Testing) {
    drawCenteredFit(38, "Ket noi dien thoai toi:", u8g2_font_5x8_tf,
                    u8g2_font_5x8_tf, u8g2_font_5x8_tf);
    snprintf(line, sizeof(line), "%s (192.168.4.1)", currentRuntime.wifiPortalApName);
    drawCenteredFit(50, line, u8g2_font_5x8_tf, u8g2_font_5x8_tf,
                    u8g2_font_5x8_tf);
  } else if (state == WifiPortalState::Failed) {
    drawCenteredFit(38, "Hay thu lai voi mang", u8g2_font_5x8_tf,
                    u8g2_font_5x8_tf, u8g2_font_5x8_tf);
    drawCenteredFit(50, "hoac mat khau khac", u8g2_font_5x8_tf,
                    u8g2_font_5x8_tf, u8g2_font_5x8_tf);
  } else if (state == WifiPortalState::Success) {
    drawCenteredFit(38, "May da chuyen sang", u8g2_font_5x8_tf,
                    u8g2_font_5x8_tf, u8g2_font_5x8_tf);
    drawCenteredFit(50, "Wi-Fi moi", u8g2_font_5x8_tf, u8g2_font_5x8_tf,
                    u8g2_font_5x8_tf);
  }
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
  // Tieu de su kien loi co dang "E113 NHIET DO BIEN THIEN NHANH" - dai hon
  // han 128px o font 6x12, truoc day bi cat mat duoi. Tu ha co font cho vua.
  drawLeftFit(1, 34, title, u8g2_font_6x12_tf, u8g2_font_5x8_tf,
              u8g2_font_5x8_tf);
  drawLeftFit(1, 46, detail, u8g2_font_5x8_tf, u8g2_font_5x8_tf,
              u8g2_font_5x8_tf);
  lcd.setFont(u8g2_font_5x8_tf);
  snprintf(footer, sizeof(footer), "%u/%u  %s",
           eventLogIndex + 1U, currentEventLog.totalInWindow, age);
  lcd.drawStr(1, 59, footer);
}

// Man hinh xac nhan RIENG, chiem toan bo LCD - truoc day chi la 1 dai 9px o
// cuoi man de khong che noi dung, nhung qua nho de doc/de bam nham. Gio thay
// han noi dung man dang xem (xem render(): bo qua ve view khi dang confirm),
// nen co the dung het khong gian cho cau hoi + 2 nut CO/HUY to, ro rang.
void drawConfirmScreen() {
  drawHeader("XAC NHAN");

  const char *line1;
  const char *line2 = "";
  if (confirmAction == ConfirmAction::ResumeBatch) {
    line1 = "MAT DIEN - TIEP TUC";
    line2 = "ME DANG AP?";
  } else if (confirmAction == ConfirmAction::AutoTuneStart) {
    line1 = "CHAY AUTO TUNE PID?";
  } else if (confirmAction == ConfirmAction::TurningToggle) {
    line1 = pendingTurningConfig.turningEnabled ? "BAT DAO TU DONG?" : "TAT DAO TU DONG?";
  } else if (currentRuntime.batchRunning) {
    line1 = "DUNG ME DANG CHAY?";
  } else {
    line1 = "BAT DAU ME AP MOI?";
  }

  const int16_t y1 = line2[0] ? 24 : 28;
  drawCenteredFit(y1, line1, u8g2_font_6x12_tf, u8g2_font_5x8_tf,
                  u8g2_font_5x8_tf);
  if (line2[0]) {
    drawCenteredFit(static_cast<int16_t>(y1 + 13), line2, u8g2_font_6x12_tf,
                    u8g2_font_5x8_tf, u8g2_font_5x8_tf);
  }

  drawYesNoButtons(confirmYes, "CO", "HUY");
}

void drawToast(uint32_t now) {
  if (!toastLine[0] || timeReached(now, toastUntil)) return;
  lcd.setDrawColor(1);
  lcd.drawBox(0, 55, 128, 9);
  lcd.setDrawColor(0);
  lcd.setFont(u8g2_font_5x8_tf);
  char line[32];
  snprintf(line, sizeof(line), "%s%s", toastError ? "! " : "", toastLine);
  // 5x8 la font nho nhat dang co nen khong the ha co them - cat bot duoi chuoi
  // cho vua 126px. showToast() da cat theo SO KY TU, nhung them tien to "! "
  // luc bao loi van co the day chuoi vuot qua be rong man hinh.
  while (line[0] && static_cast<int16_t>(lcd.getStrWidth(line)) > 126) {
    line[strlen(line) - 1U] = '\0';
  }
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

  // Man khoi dong: tu ve lai deu (khong ai set dirty ho) va tu thoat khi da
  // co du du lieu that. Xu ly TRUOC moi thu khac de khong bi cac view khac
  // (ke ca man canh bao/xac nhan) chen vao giua luc dang khoi dong.
  if (splashActive) {
    if (splashStartedAt == 0U) splashStartedAt = now;
    const uint32_t elapsed = now - splashStartedAt;
    // Truoc day dataReady chi doi CO snapshot runtime/config (co the la
    // snapshot dau tien luc cam bien CHUA doc xong - man chinh vua vao da
    // hien "--.-" cho nhiet do/do am, giong nhu con "loading" lo ra sau
    // man khoi dong). Man khoi dong la noi setup, nen phai doi luon ca cam
    // bien that su da co so lieu (currentRuntime.sensorOnline) truoc khi
    // thoat - man chinh hien ra la co du lieu ngay, khong con khoang trong
    // "--.-" nao nua. SPLASH_MAX_MS (6 s) van la tran an toan neu cam bien
    // that su cham/mat: khong bao gio giu man khoi dong vo han.
    const bool sensorReady = currentRuntime.sensorOnline &&
                             isfinite(currentRuntime.temperature);
    const bool dataReady = splashHadRuntime && splashHadConfig && sensorReady;
    if ((dataReady && elapsed >= SPLASH_MIN_MS) || elapsed >= SPLASH_MAX_MS) {
      splashActive = false;
      dirty = true;
    }
  }
  const bool periodicSplash = splashActive;
  const bool periodic = periodicHome || periodicAlarm || verificationFrame ||
                        periodicSplash;
  if (!dirty && !periodic) return;
  if (now - lastDrawAt < DISPLAY_MIN_DRAW_MS) return;

  // Moi frame bat dau tu trang thai do hoa xac dinh. Khong tat/bat LCD khi
  // chuyen trang: framebuffer moi duoc ve trong RAM roi sendBuffer mot lan.
  lcd.setMaxClipWindow();
  lcd.setDrawColor(1);
  lcd.setFontMode(0);
  lcd.setFontDirection(0);
  lcd.clearBuffer();
  const bool showConfirmScreen = !splashActive && confirmationActive() &&
                                 view != View::Alarm;
  if (splashActive) {
    drawSplash();
  } else if (showConfirmScreen) {
    // Man xac nhan thay HAN cho view hien tai (khong ve chong len) - xem
    // drawConfirmScreen() de biet ly do doi tu dai 9px cuoi man sang ca man.
    drawConfirmScreen();
  } else {
    switch (view) {
      case View::Home: drawHome(); break;
      case View::MainMenu: drawMainMenu(); break;
      case View::ChungMenu: drawChungMenu(); break;
      case View::SettingList: drawSettingList(); break;
      case View::EditSetting: drawEditSetting(); break;
      case View::TurnStats: drawTurnStats(); break;
      case View::ConnectionInfo: drawConnectionInfo(); break;
      case View::QrCode: drawQrCode(); break;
      case View::AutoTune: drawAutoTune(); break;
      case View::TestMode: drawTestMode(); break;
      case View::TestSummary: drawTestSummary(); break;
      case View::WifiChange: drawWifiChange(); break;
      case View::EventLog: drawEventLog(); break;
      case View::Alarm: drawAlarm(); break;
    }
    drawToast(now);
  }
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
  buzzerPlayStartupChime();
  beginRotary();
  lcdReady = beginLcd();
  const uint32_t now = millis();
  lastInteractionAt = now;
  lastCommandPollAt = now;
  lastLcdRetryAt = now;
  lastLcdHealthCheckAt = now;
  // Moc thoi gian man khoi dong tinh tu luc BAT MAY, khong phai tu frame ve
  // dau tien: neu LCD chua nhan duoc luc khoi dong (dang tu do tim lai), den
  // khi no phuc hoi thi SPLASH_MAX_MS da qua tu lau va may vao thang man
  // chinh - khong bat nguoi dung xem lai man khoi dong giua chung.
  splashStartedAt = now;
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

    case View::ConnectionInfo:
      return before.networkConnected != after.networkConnected ||
             before.networkRssiDbm != after.networkRssiDbm;

    // QR ma hoa ID may - hang so trong suot phien chay, khong bao gio can
    // ve lai vi runtime thay doi.
    case View::QrCode:
      return false;

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
    case View::TestSummary:
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

  // Ket qua kiem tra cong tac hanh trinh la khach quan (phan cung tu phat
  // hien), khac voi thiet bi (nguoi dung tu xac nhan). Ghi lai khi vua co
  // ket qua moi de hien trong bang tong ket.
  if (currentRuntime.testLimitPhase != lastObservedTestLimitPhase) {
    if (currentRuntime.testLimitPhase == TestLimitPhase::Success ||
        currentRuntime.testLimitPhase == TestLimitPhase::Timeout) {
      const uint8_t idx = static_cast<uint8_t>(currentRuntime.testLimitTarget);
      if (idx < TEST_MODE_LIMIT_ROWS) {
        testLimitResult[idx] = currentRuntime.testLimitPhase == TestLimitPhase::Success
            ? TestResult::Pass : TestResult::Fail;
      }
      // Xac nhan am thanh bang coi NOI BO cua HMI, khong phai relay coi that
      // (truoc day dung req.siren ben firmware - da sua vi lam keu coi that
      // ngoai y muon moi lan test cong tac hanh trinh, xem machine_control.h).
      buzzerPlayCue(currentRuntime.testLimitPhase == TestLimitPhase::Success
          ? BuzzerCue::Ok : BuzzerCue::Error);
    }
    lastObservedTestLimitPhase = currentRuntime.testLimitPhase;
  }

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
  // Danh dau da co so lieu that de man khoi dong biet luc nao duoc thoat
  // (xem splashActive trong render()) - chi can moi loai den it nhat 1 lan.
  if (hasConfig) splashHadConfig = true;
  if (hasRuntime) splashHadRuntime = true;
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
