# ATtiny13A power alarm - protocol v3

ATtiny13A la lop **bao mat dien/canh bao doc lap**, khong tham gia PID, dieu khien heater,
dao hay quat. Muc tieu thiet ke V3 la fail-safe, bus open-drain an toan va dong ngu rat thap.

## Chan
- PB0: BUS open-drain 1 day voi ESP32 GPIO41.
- PB1: dieu khien transistor/MOSFET coi.
- PB2: sense 3V3_ESP (HIGH = ESP co nguon).
- PB3: sense nguon 9V coi (HIGH = 9V OK theo nguong phan ap tren PCB).

## Dieu kien arm bao mat dien
ATtiny bat coi khi **PB2 mat 3V3** va mot trong hai co arm duoi day dang ON:

1. **Batch arm**: dang co me hoac resumePending. Trang thai nay luu EEPROM Tiny.
2. **Critical-activity arm ngoai me**: ESP32 lay **output vat ly da qua OutputArbiter** va arm neu
   it nhat mot trong bon nhom sau dang ON:
   - dong co dao trai/phai;
   - quat tuan hoan;
   - quat hut;
   - **contactor nguon nhiet `heatMaster`**.

Khong dung `heaterSsr` lam dieu kien nhiet: SSR bi PID dong/ngat lien tuc, con `heatMaster`
la contactor cap nguon chinh cho cum SSR va phan anh dung y nghia "he thong nhiet dang duoc cap nguon".
**Den, coi va tao am khong arm rieng bao mat dien; tao am chi chay trong me nen batch arm da bao phu.**

Activity ON duoc arm ngay. Activity OFF chi ghi sau khi tat ca tai tren OFF lien tuc 30 s.
Cach nay tranh ghi EEPROM theo cac dao dong relay/ngan han.

## Luu EEPROM va chong mat trang thai
- Batch va critical-activity deu luu bang cap `state` + `~state`.
- Dung `eeprom_update_byte()`: neu gia tri khong doi thi AVR khong ghi lai cell.
- Chi ghi khi trang thai tong ON/OFF thuc su doi; khong con reassert 5 giay.
- Record loi/rach -> fail-safe coi la **ARMED**.
- Activity OFF co debounce 30 s o ESP32 de giam them so chu ky ghi.

Voi heatMaster thay cho xung SSR, so lan ghi activity trong van hanh binh thuong rat thap;
EEPROM khong bi bam theo chu ky PID.

## Protocol v3
ESP32 -> Tiny:
- `1=BATCH_START`
- `2=BATCH_END`
- `3=SIREN_ON`
- `4=SIREN_OFF`
- `5=STATUS_QUERY`
- `6=ACTIVITY_ON`
- `7=ACTIVITY_OFF`

Tiny ACK lenh batch/activity **chi sau khi EEPROM da ghi va doc verify dung**.

### STATUS Tiny -> ESP: mot frame, mot ACK

Trang thai logic van la `8..23`; `status-8` la bitmask 4 bit:
- bit0 = batch
- bit1 = 9V low
- bit2 = emergency siren mirror
- bit3 = critical activity

Nhung **khong truyen 16..23 xung tren day BUS**. Bench thuc te cho thay cac frame dem xung dai tren 15 xung khong du on dinh. Cung khong tach status thanh hai frame vi cach do tao them ACK/timeout/trang thai trung gian va da xuat hien loi mat ACK frame thu hai.

Wire format hien tai:
- **So xung vat ly 8..15** = `8 + (batch | 9V-low | siren)`; chi mang 3 bit thap.
- **Xung LOW dau tien 30 ms** = activity OFF.
- **Xung LOW dau tien 120 ms** = activity ON.
- Cac xung LOW con lai = 30 ms.
- ESP giai ma do rong xung dau, khoi phuc bit activity va tra ve dung status logic `8..23` cho `MachineController`.
- Moi status van chi co **mot frame + mot ACK 30 ms**.

Marker 120 ms duoc chon co chu dich de **fail-closed khi tron firmware cu/moi**: receiver ESP cu coi LOW tu 90 ms tro len la frame loi. Vi vay Tiny moi + ESP cu se bao mat status/E501 thay vi am tham doc activity ON thanh OFF. Chieu nguoc lai, ESP moi cung tu choi frame status cu co hon 15 xung. Hai phien ban khong khop se loi ro, khong gia vo dong bo.

Vi du logical status `22`:
- `22 - 8 = 14 = 0b1110`: 9V-low=1, siren=1, activity=1, batch=0.
- So xung vat ly = `8 + 2 + 4 = 14`.
- Xung thu nhat LOW 120 ms de danh dau activity=ON.
- 13 xung con lai LOW 30 ms.
- ESP giai ma lai thanh logical `22`.

## Dong bo va tu phuc hoi
- Bat dau me: BATCH_START duoc xep truoc khi activity ngoai me bi bo.
- Ket thuc me: neu tai quan trong van ON, ACTIVITY_ON duoc xep **truoc** BATCH_END de khong tao khoang mu.
- ESP hoi STATUS ngay sau boot. Khi dang arm, hoi 1 h/lan; khi idle, 6 h/lan.
- STATUS co activity bit, nen Tiny reset rieng van duoc kiem tra hai chieu va sua mismatch.
- Khong con ACTIVITY_ON moi 5 s.

## Bao 9 V / E502
PB3 va E502 duoc giu nguyen. Khi Tiny bao 9V LOW, ESP32 phat `SIREN BATTERY LOW` (E502).
Ngay ca khi may idle, STATUS 6 h/lan dam bao 9V-low khong bi bo quen vo thoi han.

## Hieu chinh nguong 3.3 V va 9 V
PB2/PB3 hien dung DIGITAL + PCINT. Nguong thuc te do divider + VIH/VIL/hysteresis + VCC Tiny.
Trong `ATTINY13A_POWER_ALARM.ino` co 4 placeholder (mV, do tai nguon truoc divider):
- `FIELD_MEASURED_3V3_LOSS_MV`
- `FIELD_MEASURED_3V3_RESTORE_MV`
- `FIELD_MEASURED_9V_LOW_MV`
- `FIELD_MEASURED_9V_OK_MV`

0 = chua bench-calibrate. Cac so nay hien chi la ghi chu, chua dieu khien threshold.

## Low-power policy
- Power-down sleep la trang thai mac dinh.
- WDT chi bat khi xu ly BUS, tat truoc khi ngu.
- ADC va analog comparator khong dung duoc tat ro rang khi boot.
- Khi ESP mat nguon, **PB0 luon Hi-Z (`busRelease`)** va bi mask khoi PCINT. Tuyet doi khong keo BUS LOW khi rail 3V3 ESP dang mat de tranh back-power/giu net sai muc.
- Vao sleep theo chuoi atomic `cli -> clear PCIF -> recheck -> sleep_enable -> sei -> sleep_cpu`; khong co cua so lost-wakeup.
- **Khong co `_delay_ms(50)` sau wake**. Lenh BUS chi co xung LOW 30 ms; delay 50 ms sau pin-change co the nuot tron xung dau cua lenh.
- Khong co polling nhanh; status 1 h khi arm, 6 h khi idle.
- Emergency siren reassert 15 s chi xay ra trong tinh huong khan cap, khong anh huong tuoi pin binh thuong.

Muc tieu bench dong ngu phai do tren mach that sau khi chot nguon pin, divider va BOD fuse; khong suy dien tuoi pin chi tu dong datasheet cua MCU.

## Fail-safe
- EEPROM batch/activity record hong -> arm thay vi im lang.
- Tiny boot khi arm va PB2 LOW -> coi bat ngay.
- E501: mat giao tiep ATtiny.
- E502: nguon 9V coi low.
- E503: batch **hoac activity** ESP/Tiny khong dong bo.
- Loi ATtiny chi la diagnostic cho ESP32; Tiny khong duoc quyen cat/ep output dieu khien chinh.

## Build gate
GitHub Actions build ATtiny13A bang avr-g++ va fail neu Flash >1024 B hoac static RAM >64 B.
CI kiem protocol/message/status constants giua ESP32 va Tiny, dong thoi khoa cac invariant moi:
- frame STATUS vat ly khong vuot 15 xung;
- activity phai ma hoa bang do rong xung dau 120 ms;
- khong duoc dua split-status state machine tro lai;
- PB0 phai Hi-Z khi ESP mat nguon;
- khong duoc dua delay 50 ms sau wake tro lai.
