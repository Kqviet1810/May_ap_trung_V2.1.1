#pragma once

#include "config.h"
#include <Arduino.h>

// ============================================================================
// BUS GIAO TIEP 2 CHIEU VOI ATTINY13A (mach bao mat dien doc lap, xem
// doc/attiny_power_alarm.md) - giao thuc DEM XUNG tren PIN_ATTINY_BUS
// (GPIO41), kieu "ho tro" (open-drain): ca 2 ben CHI duoc keo LOW hoac tha
// noi (INPUT, dua vao dien tro keo len R8 phia ATtiny) - KHONG BAO GIO ghi
// HIGH truc tiep, tranh dung do neu ca 2 ben cung "noi" cung luc.
//
// HAI CO CHE KHAC NHAU trong file nay:
//  1) GUI (mayapAttinyBusSend): BLOCKING - ESP32 chu dong gui 1 ban tin, cho
//     ACK, tu thu lai neu can. Chi goi tu cac diem HIEM KHI xay ra (bat/ket
//     thuc me, doi trang thai coi khan cap, ping dinh ky) - KHONG goi trong
//     vong lap dieu khien tan so cao. Toi da block ~(MAX_RETRY x (n*2*PULSE_MS
//     + ACK_TIMEOUT_MS)) ~ duoi 2 giay o truong hop xau nhat (ATtiny khong
//     phan hoi ca 3 lan thu).
//  2) NHAN (mayapAttinyBusPollIncoming): NON-BLOCKING - danh cho ban tin
//     ATtiny CHU DONG gui toi (ATTINY_MSG_9V_LOW/9V_RECOVERED). Dung ngat
//     phan cung ghi lai THOI DIEM CHINH XAC (micros()) cua tung canh tin
//     hieu vao 1 bo dem vong - ISR cuc ky nhanh, KHONG lam gi ngoai ghi thoi
//     diem, nen khong phu thuoc vong lap dieu khien chay nhanh hay cham (neu
//     chi doc "muc hien tai" luc vong lap ranh se de bo lo xung dau tien vi
//     xung chi rong 30ms trong khi 1 chu ky dieu khien co the mat toi
//     ~100ms - dung bo dem thoi diem giai quyet dung van de nay).
// ============================================================================

namespace MayapAttinyBusInternal {

inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return static_cast<uint32_t>(now - then);
}

// Bo dem canh tin hieu (ghi boi ngat, doc/xoa boi mayapAttinyBusPollIncoming).
// Kich thuoc du cho 1 ban tin toi da (ATTINY_MSG_MAX_CODE xung = 12 canh) +
// du phong nhieu - neu tran, cac canh thua bi bo qua (thoai lui an toan,
// ban tin do se khong doc duoc va ben gui se tu thu lai).
constexpr uint8_t EDGE_BUF_SIZE = 20U;
static volatile uint32_t edgeAtUs_[EDGE_BUF_SIZE];
static volatile uint8_t edgeCount_ = 0U;

// Khi ESP32 dang tu chiem bus (dang gui ban tin/cho ACK - xem
// mayapAttinyBusSend), TOAN BO canh tin hieu trong luc do (ke ca ACK cua
// ATtiny) deu bi bo qua o day - waitForAck() ben duoi doc bus truc tiep
// (khong qua bo dem nay), tranh de sot lai canh cua ACK lam sai lech ban
// tin ke tiep ma ATtiny co the tu gui.
static volatile bool busBusy_ = false;

void IRAM_ATTR busIsr() {
  if (busBusy_) return;
  if (edgeCount_ >= EDGE_BUF_SIZE) return;  // tran bo dem - bo qua, xem chu thich tren
  edgeAtUs_[edgeCount_] = micros();
  ++edgeCount_;
}

inline void busRelease() {
  pinMode(PIN_ATTINY_BUS, INPUT);  // tha noi - R8 phia ATtiny keo len HIGH
}
inline void busDriveLow() {
  pinMode(PIN_ATTINY_BUS, OUTPUT);
  digitalWrite(PIN_ATTINY_BUS, LOW);
}
inline bool busIsLow() {
  return digitalRead(PIN_ATTINY_BUS) == LOW;
}

inline void resetEdgeBuffer() {
  noInterrupts();
  edgeCount_ = 0U;
  interrupts();
}

// Gui N xung LOW (moi xung ATTINY_BUS_PULSE_MS, cach nhau cung tung do).
// PHAI goi trong luc busBusy_ == true (xem mayapAttinyBusSend).
inline void sendPulses(uint8_t n) {
  busRelease();
  delay(5);  // dam bao duong day dang ranh (HIGH) truoc khi bat dau
  for (uint8_t i = 0; i < n; ++i) {
    busDriveLow();
    delay(ATTINY_BUS_PULSE_MS);
    busRelease();
    delay(ATTINY_BUS_PULSE_MS);
  }
}

// Doi dung 1 xung ACK trong vong ATTINY_BUS_ACK_TIMEOUT_MS (doc bus truc
// tiep, khong qua bo dem ngat). Tra ve true neu nhan duoc 1 xung hop le.
inline bool waitForAck() {
  const uint32_t start = millis();
  while (elapsedMs(millis(), start) < ATTINY_BUS_ACK_TIMEOUT_MS) {
    if (busIsLow()) {
      const uint32_t pulseStart = millis();
      while (busIsLow() &&
             elapsedMs(millis(), pulseStart) < ATTINY_BUS_PULSE_MS * 3UL) {
        delayMicroseconds(500);
      }
      const uint32_t lowMs = elapsedMs(millis(), pulseStart);
      return lowMs >= ATTINY_BUS_MIN_PULSE_MS;
    }
    delayMicroseconds(500);
  }
  return false;
}

}  // namespace MayapAttinyBusInternal

// Goi 1 lan luc khoi dong (trong begin() cua MachineController).
inline void mayapAttinyBusBegin() {
  using namespace MayapAttinyBusInternal;
  pinMode(PIN_ATTINY_BUS, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_ATTINY_BUS), busIsr, CHANGE);
}

// Gui 1 ban tin toi ATtiny, cho ACK, tu dong gui lai toi da
// ATTINY_BUS_MAX_RETRY lan neu khong thay ACK. BLOCKING - xem chu thich dau
// file de biet thoi gian toi da va noi duoc phep goi ham nay.
// Tra ve true neu duoc ACK (tuc ATtiny con song va da nhan lenh).
inline bool mayapAttinyBusSend(uint8_t code) {
  using namespace MayapAttinyBusInternal;
  busBusy_ = true;
  bool acked = false;
  for (uint8_t attempt = 0; attempt < ATTINY_BUS_MAX_RETRY && !acked; ++attempt) {
    sendPulses(code);
    acked = waitForAck();
  }
  busRelease();
  resetEdgeBuffer();  // xoa moi tan du canh (vd cua chinh ACK) truoc khi
                       // quay lai lang nghe ban tin ATtiny tu gui
  busBusy_ = false;
  return acked;
}

// Goi moi chu ky dieu khien (KHONG BLOCK neu chua co gi de xu ly) - kiem tra
// bo dem canh tin hieu, neu da nhan du 1 ban tin HOAN CHINH (im lang du lau
// sau canh cuoi) thi giai ma, ACK lai, roi tra ve ma ban tin. Tra ve 0 neu
// chua co gi/con dang nhan do/khong hop le.
inline uint8_t mayapAttinyBusPollIncoming() {
  using namespace MayapAttinyBusInternal;
  if (busBusy_) return 0;  // dang ban gui/cho ACK, bo qua vong nay
  noInterrupts();
  const uint8_t n = edgeCount_;
  interrupts();
  if (n == 0U) return 0;
  // Chua qua ATTINY_BUS_END_GAP_MS ke tu canh cuoi cung -> ban tin co the
  // con dang toi, doi them, chua ket luan gi ca.
  uint32_t lastEdgeAt;
  noInterrupts();
  lastEdgeAt = edgeAtUs_[n - 1U];
  interrupts();
  if ((micros() - lastEdgeAt) < (ATTINY_BUS_END_GAP_MS * 1000UL)) return 0;

  // Ban tin da "chin" (im lang du lau) - dem so xung LOW hop le. Canh trong
  // bo dem xen ke rieng-len (falling=bat dau LOW, rising=ket thuc LOW) vi
  // duong day luon ranh o muc HIGH truoc khi ban tin bat dau.
  uint8_t pulses = 0U;
  for (uint8_t i = 0U; static_cast<uint8_t>(i + 1U) < n; i = static_cast<uint8_t>(i + 2U)) {
    uint32_t tFall, tRise;
    noInterrupts();
    tFall = edgeAtUs_[i];
    tRise = edgeAtUs_[i + 1U];
    interrupts();
    const uint32_t widthUs = tRise - tFall;
    if (widthUs >= (ATTINY_BUS_MIN_PULSE_MS * 1000UL)) ++pulses;
  }
  resetEdgeBuffer();

  if (pulses == 0U || pulses > ATTINY_MSG_MAX_CODE) return 0;
  busBusy_ = true;
  sendPulses(1U);  // ACK
  busRelease();
  busBusy_ = false;
  return pulses;
}
