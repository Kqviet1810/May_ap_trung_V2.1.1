# ATtiny13A power alarm - protocol v3

ATtiny13A la lop **bao mat dien/canh bao doc lap**, khong tham gia PID, dieu khien heater,
dao hay quat. Muc tieu thiet ke V3 la fail-safe va de CR2032 nuoi Tiny trong nhieu nam.

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
**Den, coi va relay spare khong arm bao mat dien.**

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

STATUS_QUERY tra frame 8..23; `status-8` la bitmask 4 bit:
- bit0 = batch
- bit1 = 9V low
- bit2 = emergency siren mirror
- bit3 = critical activity

ESP ACK frame status. Edge buffer ESP la 48 canh, du cho frame toi da 23 xung = 46 canh.

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
- Khi ESP mat nguon, PB0 bi mask khoi PCINT va keo LOW de tranh floating/wake gia/back-power.
- Khong co polling nhanh; status 1 h khi arm, 6 h khi idle.
- Emergency siren reassert 15 s chi xay ra trong tinh huong khan cap, khong anh huong tuoi pin binh thuong.

Muc tieu bench: dong toan mach Tiny khi ngu <= 1-3 uA. Voi CR2032 chinh hang, muc tieu thuc te
5-8 nam la hop ly neu PCB/divider/MOSFET khong tao dong ro lon va BOD fuse duoc cau hinh phu hop.

## Fail-safe
- EEPROM batch/activity record hong -> arm thay vi im lang.
- Tiny boot khi arm va PB2 LOW -> coi bat ngay.
- E501: mat giao tiep ATtiny.
- E502: nguon 9V coi low.
- E503: batch **hoac activity** ESP/Tiny khong dong bo.
- Loi ATtiny chi la diagnostic cho ESP32; Tiny khong duoc quyen cat/ep output dieu khien chinh.

## Build gate
GitHub Actions build ATtiny13A bang avr-g++ va fail neu Flash >1024 B hoac static RAM >64 B.
CI cung kiem protocol/message/status constants giua ESP32 va Tiny truoc khi compile firmware chinh.
