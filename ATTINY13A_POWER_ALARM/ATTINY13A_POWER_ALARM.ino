// ATTINY13A_POWER_ALARM.ino
// Mach bao mat dien doc lap cho MAYAP - chay tren ATtiny13A-PU (U6 trong so
// do Schematic_ESP32_MAT_HMI_V1), nguon rieng qua pin CR2032 (B2), hoan toan
// doc lap voi ESP32 va nguon 9V chinh nuoi coi.
//
// BAN THIET KE NAY LA BAN SUA LON (thay toan bo ban dau dung WDT 1s poll +
// bus muc tinh) theo yeu cau: giao thuc 2 chieu co hoi/dap (ACK) dem xung
// tren 1 day, va NGU TOAN PHAN (Power-down) - CHI thuc day khi co ngat thay
// doi muc (Pin Change Interrupt), khong con thuc dinh ky nua. Day la kien
// truc tiet kiem pin toi da: dong tieu thu luc ngu Power-down chi con vai uA,
// va ATtiny hoan toan khong lam gi (khong ADC, khong tinh toan) tru khi that
// su co su kien (thay doi muc bus/3.3V/9V).
//
// XEM doc/attiny_power_alarm.md DE BIET DAY DU: so do chan, bang ma ban tin,
// cach nap code, va danh gia rui ro.
//
// ============================================================================
// NGUYEN LY TONG QUAN
// ============================================================================
//  - Nguon: net "3.3V" rieng (backup qua CR2032/B2) - KHONG chung nguon voi
//    ESP32 (net "3.3V_ESP") hay nguon 9V chinh (cap cho coi) - nen ATtiny
//    van song binh thuong ke ca khi ca 2 nguon do mat dien.
//  - PB0 = BUS 1 DAY 2 CHIEU voi ESP32 (GPIO41, hang so PIN_ATTINY_BUS trong
//    config.h cua firmware chinh). Ca 2 ben CHI duoc keo LOW hoac tha noi
//    (INPUT) - KHONG BAO GIO chu dong ghi HIGH - duong day duoc keo len muc
//    nghi (HIGH) qua dien tro R8 (10k) phia ESP32. Giao thuc DEM XUNG (xem
//    phan duoi) - chon vi khong can dong bo toc do bit giua 2 vi xu ly co
//    dao dong lech nhau nhieu (ESP32 dung thach anh, ATtiny dung dao dong
//    noi RC sai so +-10%) nhu kieu UART thuong gap.
//  - PB1 = dieu khien coi (qua R7 100k -> Q2 -> coi dung nguon 9V, U21).
//  - PB2 = do muc dien ap "3.3V_ESP" qua cau phan ap (R1/R2) - CHI DOC MUC SO
//    (khong dung ADC): con dien = muc cao, mat dien = muc thap. Khong can do
//    chinh xac dien ap - chi can phat hien CO/MAT la du (yeu cau nguoi dung).
//  - PB3 = do muc dien ap nguon 9V (nuoi coi) qua cau phan ap (R4/R5/R6),
//    cau phan ap da duoc thiet ke san de nut nguong roi vao khoang ~7V tren
//    nguon that (xem doc/attiny_power_alarm.md) - cung CHI DOC MUC SO.
//  - PB4 = noi thang len 3.3V tren mach (khong dung lam GPIO, tranh tha noi -
//    da duoc nguon ngoai giu muc, khong can pull-up noi bo).
//  - PB5 = RESET, keo len qua R10 - KHONG dung lam GPIO thuong (xem ghi chu
//    nap code o duoi).
//
// ============================================================================
// GIAO THUC BUS (DEM XUNG, khop 1-1 voi attiny_bus.h phia ESP32)
// ============================================================================
//  Mot ban tin = N xung LOW lien tiep (moi xung PULSE_MS, cach nhau cung tung
//  do), ket thuc bang khoang lang (HIGH) >= END_GAP_MS. N = ma ban tin (xem
//  bang duoi). Xung ngan hon MIN_PULSE_MS bi coi la nhieu, bo qua (khong
//  tang bo dem xung that). Ben nhan phai gui lai DUNG 1 xung ACK trong vong
//  ACK_TIMEOUT_MS sau khi ket luan ban tin da xong; ben gui tu thu lai toi
//  da MAX_RETRY lan neu khong thay ACK.
//
//  Ma ban tin (chieu ESP32 -> ATtiny, tru ma 6 la ATtiny -> ESP32):
//    1 = BAT DAU ME (ghi nho batchRunning = true)
//    2 = KET THUC ME (ghi nho batchRunning = false, tat coi bao mat dien neu
//        dang bat vi khong con ly do giu bao nua)
//    3 = BAT COI (lenh khan cap TRUC TIEP tu ESP32 - CHI dung khi bao nhiet
//        khan cap ben ESP32, luon uu tien hon moi trang thai khac ATtiny tu
//        nho - mirror y het chu ky tat/keu lai cua coi khan cap ESP32)
//    4 = TAT COI (lenh khan cap TRUC TIEP tu ESP32, tat phan mirror cua ma 3)
//    5 = PING (kiem tra ATtiny con song - dung luc bat dau me VA dinh ky moi
//        6 gio trong luc me dang chay; ATtiny chi can ACK, khong lam gi them)
//    6 = PIN 9V YEU (ATtiny tu phat hien va gui bao ve ESP32, can ACK, tu
//        thu lai neu khong duoc ACK - vd luc ca 2 nguon deu mat thi se
//        khong ai ACK ca, ATtiny bo qua sau MAX_RETRY, khong lam gi them)
//
// ============================================================================
// LOGIC BAT/TAT COI TREN ATTINY (bien RAM, KHONG mat khi ESP32 mat dien vi
// ATtiny chay hoan toan doc lap)
// ============================================================================
//  coiKhauCap   = TRUE/FALSE truc tiep theo lenh 3/4 tu ESP32 - LUON UU TIEN,
//                 khong phu thuoc dangCoMe.
//  coiMatDien   = TRUE khi PB2 bao mat dien (3.3V_ESP mat) VA dangCoMe==true;
//                 TU DONG VE FALSE ngay khi PB2 bao co dien lai (yeu cau
//                 nguoi dung: "tu tat khi co dien lai") - khong can lenh gi
//                 tu ESP32 ca vi ESP32 dang mat dien, khong the gui lenh.
//  Coi thuc te bat = coiKhauCap OR coiMatDien (2 co che doc lap, khong loai
//                 tru nhau - vd dang bao mat dien ma dong thoi co bao nhiet
//                 khan cap tu truoc do thi coi van keu, chi tat khi CA 2 dieu
//                 kien deu het).
//
// ============================================================================
// NAP CODE
// ============================================================================
//  - Dung mach nap ISP ngoai (vd USBasp) noi vao J1: MOSI=PB0, MISO=PB1,
//    SCK=PB2, RESET=PB5, GND=GND. J1 KHONG cap nguon qua ISP (chan VCC da bo
//    trong thiet ke) - PHAI de mach da gan san pin CR2032 luc nap.
//  - KHONG DUNG toi fuse RSTDISBL (giai phong PB5 lam GPIO thuong) - mach chi
//    dung PB5/RESET voi pull-up thuong (R10), doi fuse nay se can mach nap
//    dien ap cao (HVSP) moi khoi phuc duoc neu lam sai/muon nap lai.
//  - Dung FUSE MAC DINH cua ATtiny13A moi xuat xuong (dao dong noi 9.6MHz,
//    chia 8 -> 1.2MHz he thong) la du, KHONG can chinh fuse gi them. Fuse BOD
//    (Brown-out Detection) mac dinh la DISABLED tren chip moi - NEN GIU
//    NGUYEN nhu vay de toi da hoa tuoi tho pin CR2032 (BOD bat se ton them
//    dong ro ri dang ke ngay ca luc ngu Power-down, vi ATtiny13A KHONG co
//    tinh nang tu tat BOD luc ngu nhu cac AVR doi moi hon).
//  - Bien dich: Arduino IDE + board package "ATTinyCore" (Spence Konde), chon
//    board "ATtiny13", clock "1.2 MHz (Internal, 9.6MHz Osc, CKDIV8)". Code o
//    day chi dung avr-libc thuan (avr/io.h, util/delay.h...), KHONG goi ham
//    nao cua Arduino (digitalWrite/analogRead/delay...) de giu chac chan vua
//    1KB flash va hanh vi de xac dinh thoi gian. Cai board package van bat
//    buoc de IDE biet cau hinh bien dich + nap dung cho ATtiny13A.

#define F_CPU 1200000UL  // 9.6MHz noi / 8 (CKDIV8, fuse mac dinh) - PHAI khai
                          // bao TRUOC khi include util/delay.h.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <util/delay.h>

// ---- Chan (theo so do U6 ATTINY13A-PU) ----
constexpr uint8_t PIN_BUS   = PB0;  // bus 1 day 2 chieu voi ESP32
constexpr uint8_t PIN_SIREN = PB1;  // dieu khien coi qua R7 -> Q2
constexpr uint8_t PIN_3V3   = PB2;  // muc so, cau phan ap tu 3.3V_ESP
constexpr uint8_t PIN_9V    = PB3;  // muc so, cau phan ap tu nguon 9V

// ---- Thong so giao thuc (PHAI khop voi attiny_bus.h phia ESP32) ----
constexpr uint16_t PULSE_MS       = 30U;
constexpr uint16_t MIN_PULSE_MS   = 15U;
constexpr uint16_t END_GAP_MS     = 150U;
constexpr uint16_t ACK_TIMEOUT_MS = 200U;
constexpr uint8_t  MAX_RETRY      = 3U;

constexpr uint8_t MSG_BATCH_START = 1U;
constexpr uint8_t MSG_BATCH_END   = 2U;
constexpr uint8_t MSG_SIREN_ON    = 3U;
constexpr uint8_t MSG_SIREN_OFF   = 4U;
constexpr uint8_t MSG_PING        = 5U;
constexpr uint8_t MSG_9V_LOW       = 6U;
constexpr uint8_t MSG_9V_RECOVERED = 7U;

// Trang thai me song sot qua reset/thay nguon ATtiny. Hai byte dao nhau de
// phat hien EEPROM chua khoi tao/hong. Chi ghi khi bat dau/ket thuc me.
uint8_t EEMEM eeBatchState;
uint8_t EEMEM eeBatchStateInv;

static bool loadBatchState() {
  const uint8_t value = eeprom_read_byte(&eeBatchState);
  const uint8_t inverse = eeprom_read_byte(&eeBatchStateInv);
  return ((value ^ inverse) == 0xFFU) && value == 1U;
}
static void saveBatchState(bool active) {
  const uint8_t value = active ? 1U : 0U;
  eeprom_update_byte(&eeBatchState, value);
  eeprom_update_byte(&eeBatchStateInv, static_cast<uint8_t>(~value));
}
constexpr uint8_t MSG_MAX_CODE     = 7U;

// Thoi gian doi on dinh muc (chong nhieu/gon song thoang qua) truoc khi tin
// la 3.3V/9V THAT SU vua doi trang thai - ngan hon nhieu so voi ban WDT cu
// (5 giay) vi gio day day la lan doc DUY NHAT sau khi vua co canh tin hieu
// that (PCINT), khong phai doc lai lien tuc nhu kieu poll dinh ky.
constexpr uint16_t POWER_DEBOUNCE_MS = 50U;

// ============================================================================
// Cac ham chan muc thap (thuan avr-libc, khong dung Arduino core)
// ============================================================================
static inline void busRelease() {
  DDRB &= static_cast<uint8_t>(~(1 << PIN_BUS));   // input
  PORTB &= static_cast<uint8_t>(~(1 << PIN_BUS));  // khong bat pull-up noi bo
                                                    // (da co R8 phia ESP32)
}
static inline void busDriveLow() {
  DDRB |= (1 << PIN_BUS);
  PORTB &= static_cast<uint8_t>(~(1 << PIN_BUS));
}
static inline bool busIsLow() {
  return (PINB & (1 << PIN_BUS)) == 0U;
}

static inline void sirenSet(bool on) {
  if (on) {
    PORTB |= (1 << PIN_SIREN);
  } else {
    PORTB &= static_cast<uint8_t>(~(1 << PIN_SIREN));
  }
}

static inline bool esp32PowerOk() {
  return (PINB & (1 << PIN_3V3)) != 0U;
}
static inline bool nineVOk() {
  return (PINB & (1 << PIN_9V)) != 0U;
}

static inline void delayMs(uint16_t ms) {
  while (ms-- > 0U) _delay_ms(1);
}

// ============================================================================
// Giao thuc bus - do/gui xung
// ============================================================================

// Do bus dang o muc LOW trong bao lau (tinh bang ms), toi da maxMs. Goi khi
// DA BIET truoc bus dang LOW luc goi ham.
static uint16_t measureLowDurationMs(uint16_t maxMs) {
  uint16_t ms = 0U;
  while (busIsLow() && ms < maxMs) {
    _delay_ms(1);
    ++ms;
  }
  return ms;
}
// Do bus dang o muc HIGH trong bao lau (ms), toi da maxMs.
static uint16_t measureHighDurationMs(uint16_t maxMs) {
  uint16_t ms = 0U;
  while (!busIsLow() && ms < maxMs) {
    _delay_ms(1);
    ++ms;
  }
  return ms;
}

// Gui N xung LOW (moi xung PULSE_MS, cach nhau cung tung do).
static void sendPulses(uint8_t n) {
  busRelease();
  delayMs(5U);  // dam bao duong day dang ranh (HIGH) truoc khi bat dau
  for (uint8_t i = 0U; i < n; ++i) {
    busDriveLow();
    delayMs(PULSE_MS);
    busRelease();
    delayMs(PULSE_MS);
  }
}

// Doi dung 1 xung ACK trong vong ACK_TIMEOUT_MS. Dung khi ATTINY LA BEN GUI
// (vd bao 9V yeu).
static bool waitForAck() {
  uint16_t waited = 0U;
  while (waited < ACK_TIMEOUT_MS) {
    if (busIsLow()) {
      const uint16_t lowMs = measureLowDurationMs(PULSE_MS * 3U);
      return lowMs >= MIN_PULSE_MS;
    }
    _delay_ms(1);
    ++waited;
  }
  return false;
}

// Gui 1 ban tin, cho ACK, tu dong thu lai toi da MAX_RETRY lan. BLOCKING -
// chi goi tu vong lap chinh (hiem khi xay ra: bao pin 9V yeu/phuc hoi).
static bool sendMessageBlocking(uint8_t code) {
  bool acked = false;
  for (uint8_t attempt = 0U; attempt < MAX_RETRY && !acked; ++attempt) {
    sendPulses(code);
    acked = waitForAck();
  }
  busRelease();
  return acked;
}

// Nhan 1 ban tin ESP32 dang gui toi (goi khi DA THAY bus dang LOW - tuc ban
// tin da bat dau). Dem xung hop le, cho den khi im lang du END_GAP_MS thi
// ket luan xong, gui lai 1 xung ACK, tra ve ma ban tin (0 = khong hop le/loi
// bus - vd nhieu hoac duong day bi ket LOW that su).
static uint8_t receiveMessage() {
  uint8_t pulses = 0U;
  for (;;) {
    const uint16_t lowMs = measureLowDurationMs(PULSE_MS * 3U);
    if (lowMs >= MIN_PULSE_MS) ++pulses;
    // Neu vuot qua MAX_CODE+2 xung ma van chua thay khoang lang ket thuc ->
    // coi la loi/nhieu bat thuong, bo cuoc ngay (khong ACK, ben gui se tu
    // thu lai hoac bao loi AttinyBusUnresponsive ben ESP32) - tranh treo vo
    // han neu duong day vi ly do gi do bi ket muc LOW that su.
    if (pulses > (MSG_MAX_CODE + 2U)) return 0U;

    const uint16_t highMs = measureHighDurationMs(END_GAP_MS + 20U);
    if (highMs >= END_GAP_MS) break;  // du lau im lang -> ban tin da xong
    // nguoc lai: khoang cach ngan giua 2 xung -> tiep tuc doc xung ke tiep
  }
  if (pulses == 0U || pulses > MSG_MAX_CODE) return 0U;
  sendPulses(1U);  // ACK
  busRelease();
  return pulses;
}

// ============================================================================
// Ngat thay doi muc (Pin Change Interrupt) - CHI dung de danh thuc CPU khoi
// che do ngu Power-down, khong lam gi khac trong ISR (moi xu ly thuc su nam
// o vong lap chinh sau khi thuc day) - giu ISR toi gian nhat co the.
// ============================================================================
ISR(PCINT0_vect) {
  // rong - chi can ngat xay ra la du de CPU thoat sleep_mode()
}

int main(void) {
  // Ngo mac dinh AVR/ATTinyCore co the de lai WDT bat tu lan chay truoc (vd
  // sau reset do WDT) - tat han de tranh reset ngoai y muon, ban thiet ke
  // nay khong dung WDT (dung PCINT lam co che thuc).
  MCUSR = 0U;
  wdt_disable();

  // ---- Cau hinh chan luc khoi dong ----
  busRelease();                                     // PB0 = input, khong pull-up
  DDRB |= (1 << PIN_SIREN);
  sirenSet(false);                                  // an toan: coi TAT khi khoi dong
  DDRB &= static_cast<uint8_t>(~(1 << PIN_3V3));    // PB2 = input
  PORTB &= static_cast<uint8_t>(~(1 << PIN_3V3));   // khong pull-up (da co cau phan ap)
  DDRB &= static_cast<uint8_t>(~(1 << PIN_9V));     // PB3 = input
  PORTB &= static_cast<uint8_t>(~(1 << PIN_9V));    // khong pull-up
  DDRB &= static_cast<uint8_t>(~(1 << PB4));        // PB4 = input (da noi thang 3.3V ngoai)
  PORTB &= static_cast<uint8_t>(~(1 << PB4));

  // Tat ADC va Analog Comparator hoan toan - ban thiet ke nay KHONG dung ADC
  // (chi doc muc so PB2/PB3), tat het de toi thieu hoa dong ro ri luc ngu.
  ADCSRA &= static_cast<uint8_t>(~(1 << ADEN));
  ACSR |= (1 << ACD);

  // Bat ngat thay doi muc tren PB0 (bus), PB2 (3.3V), PB3 (9V) - day la CO
  // CHE THUC DUY NHAT cua ca he thong (khong con WDT dinh ky nhu ban cu).
  PCMSK = (1 << PCINT0) | (1 << PCINT2) | (1 << PCINT3);
  GIMSK |= (1 << PCIE);

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sei();

  bool dangCoMe = loadBatchState();          // ghi nho tu MSG_BATCH_START/END
  bool coiKhauCap = false;        // lenh truc tiep MSG_SIREN_ON/OFF - LUON uu tien
  bool coiMatDien = false;        // ATtiny tu phat hien mat 3.3V trong luc co me
  bool coDien33Truoc = esp32PowerOk();
  bool coDien9vTruoc = nineVOk();

  for (;;) {
    sleep_mode();  // ngu Power-down cho toi khi co ngat PCINT0 (bat ky canh
                    // nao tren PB0/PB2/PB3) danh thuc - dong tieu thu luc
                    // ngu chi con vai uA, day la trang thai mac dinh tuyet
                    // doi phan lon thoi gian song cua ATtiny.

    // ---- 1) Co ban tin ESP32 dang gui toi? ----
    if (busIsLow()) {
      const uint8_t code = receiveMessage();
      switch (code) {
        case MSG_BATCH_START:
          if (!dangCoMe) {
            dangCoMe = true;
            saveBatchState(true);
          }
          break;
        case MSG_BATCH_END:
          if (dangCoMe) {
            dangCoMe = false;
            saveBatchState(false);
          }
          coiMatDien = false;  // het me thi khong con ly do giu bao mat dien
          break;
        case MSG_SIREN_ON:
          coiKhauCap = true;
          break;
        case MSG_SIREN_OFF:
          coiKhauCap = false;
          break;
        case MSG_PING:
          // Bao lai neu pin 9V van yeu. Nho vay ESP32 khoi dong lai van
          // khoi phuc duoc E502 ma ATtiny khong can thuc dinh ky.
          if (!coDien9vTruoc) (void)sendMessageBlocking(MSG_9V_LOW);
          break;
        default:
          break;  // Ban tin khong hop le: ACK (neu hop le) da gui trong
                   // receiveMessage(), khong can lam gi them o day
      }
      sirenSet(coiKhauCap || coiMatDien);
    }

    // ---- 2) 3.3V_ESP (con dien ESP32 hay khong) doi trang thai? ----
    bool coDien33 = esp32PowerOk();
    if (coDien33 != coDien33Truoc) {
      delayMs(POWER_DEBOUNCE_MS);  // chong nhieu/gon song thoang qua
      coDien33 = esp32PowerOk();
      if (coDien33 != coDien33Truoc) {
        coDien33Truoc = coDien33;
        if (coDien33) {
          coiMatDien = false;  // yeu cau: tu tat ngay khi co dien lai
        } else if (dangCoMe) {
          coiMatDien = true;   // yeu cau: chi bao khi dang co me ap chay
        }
        sirenSet(coiKhauCap || coiMatDien);
      }
    }

    // ---- 3) Nguon 9V (nuoi coi) doi trang thai? ----
    bool coDien9v = nineVOk();
    if (coDien9v != coDien9vTruoc) {
      delayMs(POWER_DEBOUNCE_MS);
      coDien9v = nineVOk();
      if (coDien9v != coDien9vTruoc) {
        coDien9vTruoc = coDien9v;
        if (!coDien9v) {
          // Bao ve ESP32 - best effort: neu luc nay ESP32 CUNG dang mat
          // dien (vd mat dien luoi lam sut ca 2 nguon) thi se khong ai ACK,
          // sendMessageBlocking() tu bo cuoc sau MAX_RETRY (khoang <1 giay)
          // va tro ve day, khong lam gi them - AN TOAN, khong treo may.
          (void)sendMessageBlocking(MSG_9V_LOW);
        } else {
          // Pin da duoc thay/nguon 9V da phuc hoi: bao ngay de ESP32 go E502
          // tren HMI, web va gui thong bao "Da het".
          (void)sendMessageBlocking(MSG_9V_RECOVERED);
        }
      }
    }
  }
}
