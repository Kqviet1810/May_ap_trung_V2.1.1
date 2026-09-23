# ATtiny13A power alarm - protocol v3

ATtiny13A la lop bao mat dien/canh bao doc lap, khong tham gia dieu khien heater/dao.

## Chan
- PB0: BUS open-drain 1 day voi ESP32 GPIO41.
- PB1: dieu khien transistor coi.
- PB2: sense 3V3_ESP (HIGH = ESP co nguon).
- PB3: sense nguon 9V coi (HIGH = 9V OK theo nguong phan ap tren PCB).

## Khi nao Tiny arm bao mat dien
1. **Dang co me/resumePending:** batch state duoc luu EEPROM nhu v2, mat 3V3 -> coi.
2. **Ngoai me:** ESP32 gui `ACTIVITY_ON` neu output THUC TE cua it nhat mot tai sau dang ON:
   - dao trai hoac dao phai;
   - quat tuan hoan;
   - quat hut;
   - SSR thanh nhiet.
   Den, contactor tong nhiet, coi va relay spare KHONG arm bao mat dien.
3. Activity chi luu RAM Tiny, khong ghi EEPROM, de khong mai EEPROM theo PID/relay.
   ESP tai khang dinh `ACTIVITY_ON` dinh ky khi can, de tu phuc hoi neu Tiny reset rieng.

## Protocol v3
ESP32 -> Tiny:
- `1=BATCH_START`
- `2=BATCH_END`
- `3=SIREN_ON`
- `4=SIREN_OFF`
- `5=STATUS_QUERY`
- `6=ACTIVITY_ON`
- `7=ACTIVITY_OFF`

Tiny ACK moi lenh hop le. STATUS_QUERY tra frame 8..15; `status-8` la bitmask:
bit0=batch, bit1=9V low, bit2=emergency siren mirror. ESP ACK frame status.

## Chuyen trang thai khong tao khoang mu
- Bat dau me: BATCH_START duoc gui truoc khi activity ngoai me bi bo.
- Ket thuc me: neu tai quan trong van dang ON, ACTIVITY_ON duoc xep truoc BATCH_END.
- updateAttinyLink chay sau update output, nen dieu kien activity doc trang thai output vat ly moi nhat.

## Hieu chinh nguong 3.3V va 9V
PB2/PB3 hien dung DIGITAL + PCINT. Vi vay nguong dien ap thuc te do bo chia dien ap tren PCB
va nguong VIH/VIL cua ATtiny13A quyet dinh; khong co mot hang so firmware nao co the thay doi
nguong bang cach sua so don thuan. Trong `ATTINY13A_POWER_ALARM.ino` co hai placeholder:
`FIELD_MEASURED_3V3_LOSS_MV` va `FIELD_MEASURED_9V_LOW_MV`. Sau bench-test, ghi/bao lai hai
gia tri thuc te; luc do co the quyet dinh dieu chinh divider hay doi sang ADC threshold co the
hieu chinh bang code.

## Fail-safe
- EEPROM batch record hong/rach -> Tiny coi nhu dang co me.
- Tiny boot khi EEPROM bao dang co me va PB2 LOW -> coi bat ngay.
- Khi ESP mat nguon, Tiny mask PCINT PB0 va keo BUS LOW de tranh floating/wake gia.
- E501: mat giao tiep ATtiny.
- E502: nguon 9V coi low (giu nguyen protocol v3).
- E503: batch ESP/Tiny khong dong bo.

## Tieu thu dien
Tiny ngu phan lon thoi gian o `SLEEP_MODE_PWR_DOWN`; WDT chi bat trong luc xu ly bus va tat
truoc khi ngu. Tieu thu thuc te cua ca mach phu thuoc rat lon vao BOD/fuse, mach chia ap PB2/PB3,
transistor/LED va dong cua coi; can do dong tren PCB that de chot thoi luong pin.

## Build gate
GitHub Actions build rieng ATtiny13A bang avr-g++ va fail neu Flash >1024 B hoac static RAM >64 B.
CI cung so protocol version/message constants voi ESP32 truoc khi compile firmware chinh.
