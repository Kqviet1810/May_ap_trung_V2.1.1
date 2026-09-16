// ATTINY13A_POWER_ALARM.ino
// Bao mat dien doc lap cho MAYAP - chay tren ATtiny13A-PU (U6 trong so do
// Schematic_ESP32_MAT_HMI_V1), nguon rieng qua pin CR2032 (B2), hoan toan
// doc lap voi ESP32 va nguon 9V chinh.
//
// NGUYEN LY (xem doc/attiny_power_alarm.md de biet day du so do, cach tinh
// cau phan ap va huong dan nap code):
//  - ATtiny13A duoc cap nguon boi net "3.3V" rieng, backup bang pin CR2032
//    (B2) - KHONG chung nguon voi ESP32 (net "3.3V_ESP") hay nguon 9V chinh
//    (cap cho coi), nen van song binh thuong ke ca khi ca 2 nguon do mat.
//  - PB2 (ADC1) do dien ap 3.3V_ESP qua cau phan ap (R1 2.2M + R2 3.3M) -
//    dung de biet ESP32/mach chinh con dien hay khong.
//  - PB3 (ADC3) do dien ap nguon 9V qua cau phan ap (R4 2.2M + R5 3.3M +
//    R6 1M) - da co san tren mach nhung CHUA dung trong ban nay, danh cho
//    mo rong sau (vd ATtiny bao muc pin 9V ve ESP32 qua bus khi con dien).
//  - PB0 la "bus 1 day" nhan tin hieu tu ESP32 (GPIO41, hang so
//    PIN_ATTINY_BATCH_FLAG trong config.h cua firmware chinh): muc HIGH =
//    ESP32 dang bao "co me ap dang chay", muc LOW = khong co me nao dang
//    chay. ATtiny CHI DOC bus nay (khong bao gio tu drive chan PB0 trong ban
//    nay, tranh dung do neu ESP32 cung dang drive). Gia tri doc duoc gan
//    nhat duoc GHI NHO vao bien RAM (lastKnownBatchRunning) - vi RAM cua
//    ATtiny khong mat khi ESP32 mat dien (ATtiny chay rieng, khong bi anh
//    huong), day chinh la "trang thai truoc luc mat dien" ma ta can.
//  - PB1 dieu khien coi (qua R7 100k -> Q2 -> coi dung nguon 9V, U21).
//
// THUAT TOAN CHINH (xem loop() trong main()):
//  1) Thuc day ~1 giay/lan bang Watchdog Timer, ngu Power-down giua cac lan
//     do de tiet kiem toi da pin CR2032 (dong tieu thu trung biny chi con
//     vai chuc uA, du chay lien tuc nhieu nam).
//  2) Doc ADC1 (3.3V_ESP):
//     - Neu dien ap >= nguong "con dien" (POWER_OK_ADC_THRESHOLD): coi ESP32
//       con song -> doc them PB0 de cap nhat lastKnownBatchRunning; neu coi
//       dang keu thi TAT NGAY (dung yeu cau nguoi dung: "tu tat khi co dien
//       lai"); reset bo dem debounce mat dien ve 0.
//     - Neu duoi nguong (nghi ngo mat dien): tang bo dem debounce. Khi bo
//       dem dat POWER_LOSS_DEBOUNCE_TICKS (mac dinh ~5 giay) moi THUC SU ket
//       luan la mat dien that (tranh bao gia do nhieu dien ap thoang qua).
//       Luc do, CHI bat coi neu lastKnownBatchRunning == true (dung yeu cau
//       nguoi dung: "chi bao khi dang co me ap chay") - khong co me nao
//       dang ap thi im lang, khong lam phien.
//
// NAP CODE (xem chi tiet trong doc/attiny_power_alarm.md):
//  - Dung mach nap ISP ngoai (vd USBasp) noi vao J1: MOSI=PB0, MISO=PB1,
//    SCK=PB2, RESET=PB5, GND=GND. J1 KHONG cap nguon qua ISP (chan VCC da
//    bo trong thiet ke) - PHAI de mach da gan san pin CR2032 luc nap.
//  - KHONG DUNG toi fuse RSTDISBL (giai phong PB5 lam GPIO thuong) - mach
//    chi dung PB5/RESET voi pull-up thuong (R10), doi fuse nay se can mach
//    nap dien ap cao (HVSP) moi khoi phuc duoc neu lam sai/muon nap lai.
//  - Dung FUSE MAC DINH cua ATtiny13A moi xuat xuong (dao dong noi 9.6MHz,
//    chia 8 -> 1.2MHz he thong) la du, KHONG can chinh fuse gi them.
//  - Bien dich: Arduino IDE + board package "ATTinyCore" (Spence Konde),
//    chon board "ATtiny13", clock "1.2 MHz (Internal, 9.6MHz Osc, CKDIV8)".
//    Code o day chi dung avr-libc thuan (avr/io.h...), KHONG goi ham nao
//    cua Arduino (digitalWrite/analogRead...) de giu chac chan vua 1KB flash
//    va hanh vi de xac. Cai board package van bat buoc de IDE biet cau hinh
//    biên dich + nap dung cho ATtiny13A.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

// ---- Chan (theo so do U6 ATTINY13A-PU) ----
// PB0 = bus 1 day, doc trang thai "dang ap" tu ESP32 (GPIO41 qua R9 470k,
//       duong day duoc ESP32 keo len 3.3V_ESP qua R8 10k).
// PB1 = dieu khien coi (qua R7 100k -> Q2 -> coi nguon 9V).
// PB2 = ADC1, do dien ap 3.3V_ESP qua cau phan ap R1/R2/R3.
// PB3 = ADC3, do dien ap 9V qua cau phan ap R4/R5/R6 (chua dung trong ban nay).
// PB4 = noi thang len 3.3V tren mach (khong dung trong firmware, tranh tha noi).
// PB5 = RESET, keo len 10k qua R10 (khong dung lam GPIO - xem ghi chu tren).
constexpr uint8_t PIN_BUS   = PB0;
constexpr uint8_t PIN_SIREN = PB1;
constexpr uint8_t ADC_CH_3V3_ESP = 1;  // ADC1 = PB2

// So dem ADC (thang 0-1023, tham chieu VCC cua chinh ATtiny - la pin CR2032)
// duoi muc nay coi la "ESP32 da mat dien". Cach tinh: cau phan ap chia
// 3.3V_ESP con lai ~60% (R2/(R1+R2) = 3.3M/5.5M) tai diem do, tuc ~1.98V
// luc du dien - ADC (tham chieu VCC pin, ~2.0-3.3V tuy do moi/cu) doc duoc
// khoang 600-1023 tuy VCC luc do. Luc mat dien, diem do gan 0V bat ke VCC
// bao nhieu -> so dem gan 0. Nguong 100 co bien do an toan rat lon so voi ca
// 2 truong hop, khong bi anh huong boi pin CR2032 gia dan xuong ap. Neu doi
// gia tri R1/R2 tren mach, phai tinh lai nguong nay cho khop (xem
// doc/attiny_power_alarm.md).
constexpr uint16_t POWER_OK_ADC_THRESHOLD = 100;

// So lan doc lien tiep duoi nguong (moi lan cach ~1 giay qua ngat Watchdog)
// truoc khi XAC NHAN mat dien that - tranh bao gia do nhieu dien ap thoang
// qua (vd nhap nhay luoi dien vai chuc ms ma ESP32 tu chiu duoc nho tu bu).
constexpr uint8_t POWER_LOSS_DEBOUNCE_TICKS = 5;

volatile uint8_t wdtTick = 0;

ISR(WDT_vect) {
  wdtTick = 1;
}

// Cau hinh Watchdog ngat (KHONG reset) sau ~1 giay, dung de danh thuc chip
// tu che do ngu Power-down - day la co che ngu/thuc tiet kiem dien chuan
// cho AVR chay pin lau dai (dong tieu thu luc ngu chi con vai uA).
static void wdtSetup1s() {
  cli();
  wdt_reset();
  MCUSR &= static_cast<uint8_t>(~(1 << WDRF));
  WDTCR |= (1 << WDCE) | (1 << WDE);
  // WDP2:WDP1:WDP0 = 1:1:0 -> khoang 1.0 giay (xem bang Watchdog Timer
  // Prescale Select trong datasheet ATtiny13A). WDTIE bat che do ngat.
  WDTCR = (1 << WDTIE) | (1 << WDP2) | (1 << WDP1);
  sei();
}

// Doc 1 kenh ADC (tham chieu VCC, khong dung tham chieu noi 1.1V vi dien ap
// can do (~2V) da vuot muc do). Bat/tat ADC quanh moi lan doc de tiet kiem
// dien tuc thoi giua cac lan (module ADC tieu thu dang ke neu de bat lien tuc).
static uint16_t adcRead(uint8_t channel) {
  ADMUX = static_cast<uint8_t>(channel & 0x03);  // REFS0=0 -> tham chieu VCC
  ADCSRA |= (1 << ADEN);
  ADCSRA |= (1 << ADSC);
  while ((ADCSRA & (1 << ADSC)) != 0) {}
  const uint16_t value = ADC;
  ADCSRA &= static_cast<uint8_t>(~(1 << ADEN));
  return value;
}

static inline void sirenSet(bool on) {
  if (on) {
    PORTB |= (1 << PIN_SIREN);
  } else {
    PORTB &= static_cast<uint8_t>(~(1 << PIN_SIREN));
  }
}

static inline bool busReadBatchRunning() {
  return (PINB & (1 << PIN_BUS)) != 0;
}

int main(void) {
  // ---- Cau hinh chan luc khoi dong ----
  DDRB &= static_cast<uint8_t>(~(1 << PIN_BUS));   // PB0 = input
  PORTB &= static_cast<uint8_t>(~(1 << PIN_BUS));  // khong bat pull-up noi bo
                                                    // (duong bus da co R8 10k
                                                    // ben phia ESP32 keo len)
  DDRB |= (1 << PIN_SIREN);                        // PB1 = output
  sirenSet(false);                                 // an toan: coi TAT khi khoi dong

  // Tat bo dem tin hieu so (digital input buffer) tren 2 chan ADC dang dung
  // (PB2=ADC1, PB3=ADC3) - khuyen nghi chuan cua nha san xuat de giam dong ro
  // ri khi dien ap analog nam giua muc logic cao/thap.
  DIDR0 |= (1 << ADC1D) | (1 << ADC3D);

  // Chon truoc prescaler ADC (/16 -> xung dong ho ADC ~75kHz tu he thong
  // 1.2MHz mac dinh, nam trong khoang khuyen nghi 50-200kHz cho do chinh xac
  // 10-bit du) - ADEN de o 0, chi bat tam thoi trong adcRead().
  ADCSRA = (1 << ADPS2);

  // Tat Analog Comparator (khong dung toi) de tiet kiem them dong ro ri.
  ACSR |= (1 << ACD);

  wdtSetup1s();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sei();

  bool lastKnownBatchRunning = false;
  bool alarmActive = false;
  uint8_t powerLossTicks = 0;

  for (;;) {
    sleep_mode();  // ngu Power-down cho toi khi Watchdog danh thuc (~1 giay)
    if (wdtTick == 0) continue;
    wdtTick = 0;

    const uint16_t esp32Rail = adcRead(ADC_CH_3V3_ESP);

    if (esp32Rail >= POWER_OK_ADC_THRESHOLD) {
      // ESP32 con dien - cap nhat "dang co me ap hay khong" tu bus, va tat
      // coi ngay neu dang keu (dung yeu cau: tu tat khi co dien lai).
      lastKnownBatchRunning = busReadBatchRunning();
      powerLossTicks = 0;
      if (alarmActive) {
        alarmActive = false;
        sirenSet(false);
      }
    } else {
      // Nghi ngo mat dien - dem debounce truoc khi ket luan la that.
      if (powerLossTicks < POWER_LOSS_DEBOUNCE_TICKS) {
        ++powerLossTicks;
      }
      if (powerLossTicks >= POWER_LOSS_DEBOUNCE_TICKS && !alarmActive) {
        if (lastKnownBatchRunning) {
          alarmActive = true;
          sirenSet(true);
        }
        // lastKnownBatchRunning == false: khong co me nao dang ap luc mat
        // dien -> im lang, dung yeu cau "chi bao khi dang co me ap chay".
      }
    }
  }
}
