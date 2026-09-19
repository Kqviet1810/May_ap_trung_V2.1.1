# ATtiny13A power alarm - protocol v2

ATtiny13A la lop bao mat dien/canh bao doc lap, khong tham gia dieu khien heater/dao.

## Chan
- PB0: BUS open-drain 1 day voi ESP32 GPIO41.
- PB1: dieu khien transistor coi.
- PB2: sense 3V3_ESP (HIGH = ESP co nguon).
- PB3: sense nguon 9V coi (HIGH = 9V OK theo nguong phan ap tren PCB).

## Protocol v2
ESP32 la master. Lenh ESP -> Tiny: `1=BATCH_START`, `2=BATCH_END`,
`3=SIREN_ON`, `4=SIREN_OFF`, `5=STATUS_QUERY`. Tiny ACK lenh 1/2 CHI SAU
khi EEPROM state+inverse da ghi va doc lai dung. STATUS_QUERY duoc ACK roi Tiny
tra frame status 6..13; `status-6` la bitmask: bit0=batch, bit1=9V low,
bit2=emergency siren mirror. ESP ACK frame status.

## Fail-safe
- EEPROM batch record hong/rach -> Tiny coi nhu dang co me.
- Tiny boot khi EEPROM bao dang co me va PB2 LOW -> coi bat ngay, khong cho canh moi.
- Khi ESP mat nguon, Tiny mask PCINT PB0 va keo BUS LOW de tranh floating/wake gia.
- Khi ESP co nguon lai, Tiny tha BUS, cho phep giao tiep lai.
- ESP tach `link healthy` va `state synchronized`; STATUS thanh cong khong duoc che
  loi batch mismatch.
- ESP tai khang dinh SIREN_ON dinh ky khi emergency con ton tai.
- E501: mat giao tiep; E502: 9V coi low; E503: state batch ESP/Tiny khong dong bo.

## EEPROM va watchdog
Batch state luu 2 byte `state` + `~state`; chi ghi START/END, co verify read-back.
WDT chi bat khi Tiny dang xu ly bus, tat truoc Power-down de khong danh thuc dinh ky.
Khong tu dong thay fuse BOD trong CI; fuse chi duoc chot sau bench-test dong ngu/CR2032.

## Build gate
GitHub Actions build rieng ATtiny13A bang avr-g++ va fail neu Flash >1024 B hoac
static RAM >64 B. CI cung so protocol version/message constants voi ESP32 truoc khi
compile firmware chinh.
