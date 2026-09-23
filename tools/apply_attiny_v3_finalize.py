from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly 1 match, got {count}")
    return text.replace(old, new, 1)


# -----------------------------------------------------------------------------
# config.h: v3 status gains ACTIVITY flag; remove 5 s reassert; add low-wear
# state-off debounce and low-duty status polling.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/config.h"
text = read(path)
text = replace_once(text,
    "constexpr uint8_t ATTINY_MSG_STATUS_MAX = 15U;\nconstexpr uint8_t ATTINY_MSG_MAX_COMMAND = 7U;\nconstexpr uint8_t ATTINY_MSG_MAX_CODE = 15U;\nconstexpr uint8_t ATTINY_STATUS_FLAG_BATCH = 1U;\nconstexpr uint8_t ATTINY_STATUS_FLAG_9V_LOW = 2U;\nconstexpr uint8_t ATTINY_STATUS_FLAG_SIREN = 4U;\nconstexpr uint32_t ATTINY_STATUS_INTERVAL_MS = 1UL * 3600UL * 1000UL;\nconstexpr uint32_t ATTINY_STATUS_RESPONSE_TIMEOUT_MS = 2500UL;\nconstexpr uint32_t ATTINY_RESYNC_RETRY_MS = 30000UL;\nconstexpr uint32_t ATTINY_SIREN_REASSERT_MS = 15000UL;\nconstexpr uint32_t ATTINY_ACTIVITY_REASSERT_MS = 5000UL;\nconstexpr uint32_t ATTINY_9V_CONFIRM_MS = 3000UL;",
    "constexpr uint8_t ATTINY_MSG_STATUS_MAX = 23U;\nconstexpr uint8_t ATTINY_MSG_MAX_COMMAND = 7U;\nconstexpr uint8_t ATTINY_MSG_MAX_CODE = 23U;\nconstexpr uint8_t ATTINY_STATUS_FLAG_BATCH = 1U;\nconstexpr uint8_t ATTINY_STATUS_FLAG_9V_LOW = 2U;\nconstexpr uint8_t ATTINY_STATUS_FLAG_SIREN = 4U;\nconstexpr uint8_t ATTINY_STATUS_FLAG_ACTIVITY = 8U;\n// Khi dang arm (co me hoac co tai quan trong ngoai me), kiem tra hai chieu moi 1 h.\n// Khi idle van hoi 6 h/lan de E502 (9V LOW) khong bi mat vo thoi han, nhung\n// giu duty-cycle cua Tiny rat thap de CR2032 co the song nhieu nam.\nconstexpr uint32_t ATTINY_STATUS_ARMED_INTERVAL_MS = 1UL * 3600UL * 1000UL;\nconstexpr uint32_t ATTINY_STATUS_IDLE_INTERVAL_MS = 6UL * 3600UL * 1000UL;\nconstexpr uint32_t ATTINY_STATUS_RESPONSE_TIMEOUT_MS = 2500UL;\nconstexpr uint32_t ATTINY_RESYNC_RETRY_MS = 30000UL;\nconstexpr uint32_t ATTINY_SIREN_REASSERT_MS = 15000UL;\n// ON duoc arm ngay. OFF phai on dinh 30 s moi ghi lai Tiny EEPROM, de gop\n// cac dao dong ngan va giam so chu ky ghi EEPROM.\nconstexpr uint32_t ATTINY_ACTIVITY_OFF_CONFIRM_MS = 30000UL;\nconstexpr uint32_t ATTINY_9V_CONFIRM_MS = 3000UL;",
    "config ATtiny constants")
write(path, text)


# -----------------------------------------------------------------------------
# attiny_bus.h: 23-pulse status => 46 edges; 48 gives exact safe capacity.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/attiny_bus.h"
text = read(path)
text = replace_once(text, "constexpr uint8_t EDGE_BUF_SIZE = 32U;",
                    "constexpr uint8_t EDGE_BUF_SIZE = 48U;",
                    "ATtiny edge buffer")
write(path, text)


# -----------------------------------------------------------------------------
# machine_control.h: use HEAT MASTER contactor (not SSR) as heating condition;
# persist/sync activity without periodic 5 s traffic; 30 s OFF debounce.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/machine_control.h"
text = read(path)
text = replace_once(text,
    "// Ngoai me: activity RAM arm neu OUTPUT THUC TE cua it nhat mot tai sau ON:\n  // DAO trai/phai, QUAT TUAN HOAN, QUAT HUT, SSR THANH NHIET.\n  // DEN, contactor tong nhiet, coi va relay spare KHONG duoc tinh.",
    "// Ngoai me: activity arm neu OUTPUT THUC TE cua it nhat mot tai sau ON:\n  // DAO trai/phai, QUAT TUAN HOAN, QUAT HUT, CONTACTOR NGUON NHIET (heatMaster).\n  // Lay contactor tong vi day la dieu kien cap nguon chinh cho cum SSR; KHONG\n  // bam theo xung PID SSR. DEN, coi va relay spare KHONG duoc tinh.",
    "machine ATtiny policy comment")
text = replace_once(text,
    "    const bool expectedActivity = !expectedBatch &&\n        (physicalOut.turnLeft || physicalOut.turnRight ||\n         physicalOut.circulationFan || physicalOut.ventFan || physicalOut.heaterSsr);",
    "    const bool rawCriticalActivity = !expectedBatch &&\n        (physicalOut.turnLeft || physicalOut.turnRight ||\n         physicalOut.circulationFan || physicalOut.ventFan || physicalOut.heatMaster);\n\n    // Arm ON ngay de khong tao cua so mat bao ve. OFF chi chap nhan sau 30 s\n    // khong con bat ky tai quan trong nao, de khong ghi EEPROM Tiny lien tuc\n    // neu relay/nguoi dung dao dong trang thai ngan.\n    if (expectedBatch) {\n      attinyActivityDesired_ = false;\n      attinyActivityOffSince_ = 0U;\n    } else if (rawCriticalActivity) {\n      attinyActivityDesired_ = true;\n      attinyActivityOffSince_ = 0U;\n    } else if (attinyActivityDesired_) {\n      if (attinyActivityOffSince_ == 0U) attinyActivityOffSince_ = now;\n      if (elapsedMs(now, attinyActivityOffSince_) >= ATTINY_ACTIVITY_OFF_CONFIRM_MS) {\n        attinyActivityDesired_ = false;\n        attinyActivityOffSince_ = 0U;\n      }\n    } else {\n      attinyActivityOffSince_ = 0U;\n    }\n    const bool expectedActivity = attinyActivityDesired_;",
    "machine critical activity source")
text = replace_once(text,
    "    const bool keepPowerLossArmed = stopOut.turnLeft || stopOut.turnRight ||\n        stopOut.circulationFan || stopOut.ventFan || stopOut.heaterSsr;",
    "    const bool keepPowerLossArmed = stopOut.turnLeft || stopOut.turnRight ||\n        stopOut.circulationFan || stopOut.ventFan || stopOut.heatMaster;",
    "batch stop heat-master handoff")
text = replace_once(text,
    "        } else if (completedCode == ATTINY_MSG_ACTIVITY_ON || completedCode == ATTINY_MSG_ACTIVITY_OFF) {\n          attinyActivityMirrorOn_ = completedCode == ATTINY_MSG_ACTIVITY_ON;\n          attinyLastActivityAssertAt_ = now;",
    "        } else if (completedCode == ATTINY_MSG_ACTIVITY_ON || completedCode == ATTINY_MSG_ACTIVITY_OFF) {\n          attinyActivityMirrorOn_ = completedCode == ATTINY_MSG_ACTIVITY_ON;\n          attinyActivitySynced_ = (attinyActivityMirrorOn_ == expectedActivity);",
    "activity ACK handling")
text = replace_once(text,
    "        if (completedCode == ATTINY_MSG_ACTIVITY_ON || completedCode == ATTINY_MSG_ACTIVITY_OFF) {\n          // Khong duoc tin mirror neu Tiny khong ACK lenh activity.\n          attinyActivityMirrorOn_ = !expectedActivity;\n        }",
    "        if (completedCode == ATTINY_MSG_ACTIVITY_ON || completedCode == ATTINY_MSG_ACTIVITY_OFF) {\n          // Khong duoc tin mirror neu Tiny khong ACK lenh activity.\n          attinyActivityMirrorOn_ = !expectedActivity;\n          attinyActivitySynced_ = false;\n        }",
    "activity NACK handling")
text = replace_once(text,
    "      const bool reported9vLow = (flags & ATTINY_STATUS_FLAG_9V_LOW) != 0U;\n      attinyTinyBatch_ = (flags & ATTINY_STATUS_FLAG_BATCH) != 0U;\n      attinyTinySirenOn_ = (flags & ATTINY_STATUS_FLAG_SIREN) != 0U;",
    "      const bool reported9vLow = (flags & ATTINY_STATUS_FLAG_9V_LOW) != 0U;\n      attinyTinyBatch_ = (flags & ATTINY_STATUS_FLAG_BATCH) != 0U;\n      attinyTinySirenOn_ = (flags & ATTINY_STATUS_FLAG_SIREN) != 0U;\n      attinyTinyActivity_ = (flags & ATTINY_STATUS_FLAG_ACTIVITY) != 0U;",
    "status activity decode")
text = replace_once(text,
    "      attinyBatchSynced_ = (attinyTinyBatch_ == expectedBatch);\n      mayapSerialPrintf(false,\n          \"[ATTINY] v=%u link=1 batchSync=%u expectedBatch=%u activity=%u mirror=%u tinyBatch=%u siren=%u tinySiren=%u 9v=%s\\n\",\n          ATTINY_PROTOCOL_VERSION, attinyBatchSynced_ ? 1U : 0U,\n          expectedBatch ? 1U : 0U, expectedActivity ? 1U : 0U,\n          attinyActivityMirrorOn_ ? 1U : 0U, attinyTinyBatch_ ? 1U : 0U,\n          desiredSiren ? 1U : 0U, attinyTinySirenOn_ ? 1U : 0U,\n          reported9vLow ? \"LOW\" : \"OK\");",
    "      attinyBatchSynced_ = (attinyTinyBatch_ == expectedBatch);\n      attinyActivitySynced_ = (attinyTinyActivity_ == expectedActivity);\n      // Status Tiny la nguon su that sau reset rieng le; cap nhat mirror de\n      // block thay-doi-ben-duoi tu dong gui lai neu co mismatch.\n      attinyActivityMirrorOn_ = attinyTinyActivity_;\n      mayapSerialPrintf(false,\n          \"[ATTINY] v=%u link=1 batchSync=%u activitySync=%u expectedBatch=%u activity=%u tinyBatch=%u tinyActivity=%u siren=%u tinySiren=%u 9v=%s\\n\",\n          ATTINY_PROTOCOL_VERSION, attinyBatchSynced_ ? 1U : 0U,\n          attinyActivitySynced_ ? 1U : 0U, expectedBatch ? 1U : 0U,\n          expectedActivity ? 1U : 0U, attinyTinyBatch_ ? 1U : 0U,\n          attinyTinyActivity_ ? 1U : 0U, desiredSiren ? 1U : 0U,\n          attinyTinySirenOn_ ? 1U : 0U, reported9vLow ? \"LOW\" : \"OK\");",
    "status activity sync/log")
text = replace_once(text,
    "    // Activity la RAM-only tren Tiny: ACK xac nhan moi thay doi, va khi ON\n    // tai khang dinh moi 5 s de tu phuc hoi neu Tiny vua reset rieng le.\n    if (expectedActivity != attinyActivityMirrorOn_ ||\n        (expectedActivity && elapsedMs(now, attinyLastActivityAssertAt_) >= ATTINY_ACTIVITY_REASSERT_MS)) {\n      if (mayapAttinyBusRequest(expectedActivity ? ATTINY_MSG_ACTIVITY_ON : ATTINY_MSG_ACTIVITY_OFF) && expectedActivity) {\n        attinyLastActivityAssertAt_ = now;\n      }\n    }",
    "    // Activity Tiny da duoc luu EEPROM + tra lai trong STATUS. Vi vay chi\n    // gui khi trang thai THUC SU doi hoac status cho thay mismatch; KHONG con\n    // reassert 5 s, giup Tiny ngu gan nhu toan bo thoi gian.\n    if (expectedActivity != attinyActivityMirrorOn_) {\n      (void)mayapAttinyBusRequest(\n          expectedActivity ? ATTINY_MSG_ACTIVITY_ON : ATTINY_MSG_ACTIVITY_OFF);\n    }",
    "remove 5s activity reassert")
text = replace_once(text,
    "    } else if ((expectedBatch || expectedActivity) &&\n               elapsedMs(now, attinyLastStatusQueryAt_) >= ATTINY_STATUS_INTERVAL_MS) {\n      if (mayapAttinyBusRequest(ATTINY_MSG_STATUS_QUERY)) attinyLastStatusQueryAt_ = now;",
    "    } else if (elapsedMs(now, attinyLastStatusQueryAt_) >=\n               ((expectedBatch || expectedActivity) ? ATTINY_STATUS_ARMED_INTERVAL_MS\n                                                    : ATTINY_STATUS_IDLE_INTERVAL_MS)) {\n      if (mayapAttinyBusRequest(ATTINY_MSG_STATUS_QUERY)) attinyLastStatusQueryAt_ = now;",
    "low-duty status schedule")
text = replace_once(text,
    "    faults_.set(FaultCode::AttinyStateUnsynced,\n                attinyStatusKnown_ && !attinyBatchSynced_, now);",
    "    faults_.set(FaultCode::AttinyStateUnsynced,\n                attinyStatusKnown_ && (!attinyBatchSynced_ || !attinyActivitySynced_), now);",
    "E503 covers activity sync")
text = replace_once(text,
    "    runtime_.attinyCriticalActivityArmed = expectedActivity && attinyActivityMirrorOn_;",
    "    runtime_.attinyCriticalActivityArmed = expectedActivity &&\n        attinyActivityMirrorOn_ && (!attinyStatusKnown_ || attinyActivitySynced_);",
    "runtime activity armed")
text = replace_once(text,
    "  // Bao mat dien qua ATtiny13A protocol v3. Activity ngoai me chi nam RAM\n  // cua Tiny; 9V sense/E502 van giu nguyen.\n  bool attinySirenMirrorOn_ = false;\n  bool attinyActivityMirrorOn_ = false;",
    "  // Bao mat dien qua ATtiny13A protocol v3. Batch + activity duoc Tiny\n  // luu EEPROM co verify; 9V sense/E502 van giu nguyen.\n  bool attinySirenMirrorOn_ = false;\n  bool attinyActivityMirrorOn_ = false;\n  bool attinyActivityDesired_ = false;\n  bool attinyActivitySynced_ = false;\n  bool attinyTinyActivity_ = false;",
    "activity member flags")
text = replace_once(text,
    "  uint32_t attinyLastSirenAssertAt_ = 0U;\n  uint32_t attinyLastActivityAssertAt_ = 0U;\n  uint32_t attinyStatusDeadline_ = 0U;",
    "  uint32_t attinyLastSirenAssertAt_ = 0U;\n  uint32_t attinyActivityOffSince_ = 0U;\n  uint32_t attinyStatusDeadline_ = 0U;",
    "activity member timers")
write(path, text)


# -----------------------------------------------------------------------------
# ATtiny13A: persist both batch and outside-batch activity; STATUS carries the
# fourth bit. Keep 9V sensing. Disable unused analog comparator/ADC for lower
# current. EEPROM writes use update_byte and happen only on real state changes.
# -----------------------------------------------------------------------------
path = "ATTINY13A_POWER_ALARM/ATTINY13A_POWER_ALARM.ino"
text = read(path)
text = replace_once(text,
    "constexpr uint8_t MSG_STATUS_MAX = 15U;\nconstexpr uint8_t FLAG_BATCH = 1U;\nconstexpr uint8_t FLAG_9V_LOW = 2U;\nconstexpr uint8_t FLAG_SIREN = 4U;\n\nuint8_t EEMEM eeBatchState;\nuint8_t EEMEM eeBatchStateInv;",
    "constexpr uint8_t MSG_STATUS_MAX = 23U;\nconstexpr uint8_t FLAG_BATCH = 1U;\nconstexpr uint8_t FLAG_9V_LOW = 2U;\nconstexpr uint8_t FLAG_SIREN = 4U;\nconstexpr uint8_t FLAG_ACTIVITY = 8U;\n\nuint8_t EEMEM eeBatchState;\nuint8_t EEMEM eeBatchStateInv;\nuint8_t EEMEM eeActivityState;\nuint8_t EEMEM eeActivityStateInv;",
    "Tiny status/activity EEPROM constants")
text = replace_once(text,
    "static bool loadBatch() {\n  const uint8_t v = eeprom_read_byte(&eeBatchState);\n  const uint8_t n = eeprom_read_byte(&eeBatchStateInv);\n  if ((v ^ n) == 0xFFU && v <= 1U) return v != 0U;\n  return true;  // EEPROM rach/chua hop le: fail-safe coi nhu dang co me.\n}\n\nstatic bool saveBatch(bool on) {\n  const uint8_t v = on ? 1U : 0U;\n  eeprom_update_byte(&eeBatchState, v);\n  eeprom_update_byte(&eeBatchStateInv, static_cast<uint8_t>(~v));\n  return eeprom_read_byte(&eeBatchState) == v &&\n         eeprom_read_byte(&eeBatchStateInv) == static_cast<uint8_t>(~v);\n}",
    "static bool loadState(const uint8_t *stateAddr, const uint8_t *invAddr) {\n  const uint8_t v = eeprom_read_byte(stateAddr);\n  const uint8_t n = eeprom_read_byte(invAddr);\n  if ((v ^ n) == 0xFFU && v <= 1U) return v != 0U;\n  return true;  // Record rach/chua hop le: fail-safe = ARMED.\n}\n\nstatic bool saveState(uint8_t *stateAddr, uint8_t *invAddr, bool on) {\n  const uint8_t v = on ? 1U : 0U;\n  eeprom_update_byte(stateAddr, v);\n  eeprom_update_byte(invAddr, static_cast<uint8_t>(~v));\n  return eeprom_read_byte(stateAddr) == v &&\n         eeprom_read_byte(invAddr) == static_cast<uint8_t>(~v);\n}",
    "Tiny generic persistent state")
text = replace_once(text,
    "  if (emergencySiren) flags |= FLAG_SIREN;\n  return static_cast<uint8_t>(MSG_STATUS_BASE + flags);",
    "  if (emergencySiren) flags |= FLAG_SIREN;\n  if (criticalActivity) flags |= FLAG_ACTIVITY;\n  return static_cast<uint8_t>(MSG_STATUS_BASE + flags);",
    "Tiny status activity flag")
text = replace_once(text,
    "  DDRB = _BV(PIN_SIREN);\n  PORTB = 0U;\n  batchActive = loadBatch();\n  criticalActivity = false;  // RAM-only; ESP resync khi link song.\n  emergencySiren = false;",
    "  DDRB = _BV(PIN_SIREN);\n  PORTB = 0U;\n  // Khong dung ADC/comparator: tat ro rang de giam dong nen. PB2/PB3 van la\n  // digital input + PCINT, khong bi anh huong. BOD la fuse va phai bench-test.\n#ifdef ACD\n  ACSR |= _BV(ACD);\n#endif\n#ifdef PRADC\n  PRR |= _BV(PRADC);\n#endif\n  batchActive = loadState(&eeBatchState, &eeBatchStateInv);\n  criticalActivity = loadState(&eeActivityState, &eeActivityStateInv);\n  emergencySiren = false;",
    "Tiny low-power boot/persist load")
text = replace_once(text,
    "      if (code == MSG_BATCH_START) {\n        ok = saveBatch(true); if (ok) batchActive = true;\n      } else if (code == MSG_BATCH_END) {\n        ok = saveBatch(false); if (ok) batchActive = false;",
    "      if (code == MSG_BATCH_START) {\n        ok = saveState(&eeBatchState, &eeBatchStateInv, true); if (ok) batchActive = true;\n      } else if (code == MSG_BATCH_END) {\n        ok = saveState(&eeBatchState, &eeBatchStateInv, false); if (ok) batchActive = false;",
    "Tiny batch persistent calls")
text = replace_once(text,
    "      } else if (code == MSG_ACTIVITY_ON) {\n        criticalActivity = true; ok = true;\n      } else if (code == MSG_ACTIVITY_OFF) {\n        criticalActivity = false; ok = true;",
    "      } else if (code == MSG_ACTIVITY_ON) {\n        ok = saveState(&eeActivityState, &eeActivityStateInv, true);\n        if (ok) criticalActivity = true;\n      } else if (code == MSG_ACTIVITY_OFF) {\n        ok = saveState(&eeActivityState, &eeActivityStateInv, false);\n        if (ok) criticalActivity = false;",
    "Tiny activity persistent calls")
write(path, text)


# -----------------------------------------------------------------------------
# Documentation: final V3 contract + long-life operating policy.
# -----------------------------------------------------------------------------
path = "doc/attiny_power_alarm.md"
write(path, """# ATtiny13A power alarm - protocol v3\n\nATtiny13A la lop **bao mat dien/canh bao doc lap**, khong tham gia PID, dieu khien heater,\ndao hay quat. Muc tieu thiet ke V3 la fail-safe va de CR2032 nuoi Tiny trong nhieu nam.\n\n## Chan\n- PB0: BUS open-drain 1 day voi ESP32 GPIO41.\n- PB1: dieu khien transistor/MOSFET coi.\n- PB2: sense 3V3_ESP (HIGH = ESP co nguon).\n- PB3: sense nguon 9V coi (HIGH = 9V OK theo nguong phan ap tren PCB).\n\n## Dieu kien arm bao mat dien\nATtiny bat coi khi **PB2 mat 3V3** va mot trong hai co arm duoi day dang ON:\n\n1. **Batch arm**: dang co me hoac resumePending. Trang thai nay luu EEPROM Tiny.\n2. **Critical-activity arm ngoai me**: ESP32 lay **output vat ly da qua OutputArbiter** va arm neu\n   it nhat mot trong bon nhom sau dang ON:\n   - dong co dao trai/phai;\n   - quat tuan hoan;\n   - quat hut;\n   - **contactor nguon nhiet `heatMaster`**.\n\nKhong dung `heaterSsr` lam dieu kien nhiet: SSR bi PID dong/ngat lien tuc, con `heatMaster`\nla contactor cap nguon chinh cho cum SSR va phan anh dung y nghia "he thong nhiet dang duoc cap nguon".\n**Den, coi va relay spare khong arm bao mat dien.**\n\nActivity ON duoc arm ngay. Activity OFF chi ghi sau khi tat ca tai tren OFF lien tuc 30 s.\nCach nay tranh ghi EEPROM theo cac dao dong relay/ngan han.\n\n## Luu EEPROM va chong mat trang thai\n- Batch va critical-activity deu luu bang cap `state` + `~state`.\n- Dung `eeprom_update_byte()`: neu gia tri khong doi thi AVR khong ghi lai cell.\n- Chi ghi khi trang thai tong ON/OFF thuc su doi; khong con reassert 5 giay.\n- Record loi/rach -> fail-safe coi la **ARMED**.\n- Activity OFF co debounce 30 s o ESP32 de giam them so chu ky ghi.\n\nVoi heatMaster thay cho xung SSR, so lan ghi activity trong van hanh binh thuong rat thap;\nEEPROM khong bi bam theo chu ky PID.\n\n## Protocol v3\nESP32 -> Tiny:\n- `1=BATCH_START`\n- `2=BATCH_END`\n- `3=SIREN_ON`\n- `4=SIREN_OFF`\n- `5=STATUS_QUERY`\n- `6=ACTIVITY_ON`\n- `7=ACTIVITY_OFF`\n\nTiny ACK lenh batch/activity **chi sau khi EEPROM da ghi va doc verify dung**.\n\nSTATUS_QUERY tra frame 8..23; `status-8` la bitmask 4 bit:\n- bit0 = batch\n- bit1 = 9V low\n- bit2 = emergency siren mirror\n- bit3 = critical activity\n\nESP ACK frame status. Edge buffer ESP la 48 canh, du cho frame toi da 23 xung = 46 canh.\n\n## Dong bo va tu phuc hoi\n- Bat dau me: BATCH_START duoc xep truoc khi activity ngoai me bi bo.\n- Ket thuc me: neu tai quan trong van ON, ACTIVITY_ON duoc xep **truoc** BATCH_END de khong tao khoang mu.\n- ESP hoi STATUS ngay sau boot. Khi dang arm, hoi 1 h/lan; khi idle, 6 h/lan.\n- STATUS co activity bit, nen Tiny reset rieng van duoc kiem tra hai chieu va sua mismatch.\n- Khong con ACTIVITY_ON moi 5 s.\n\n## Bao 9 V / E502\nPB3 va E502 duoc giu nguyen. Khi Tiny bao 9V LOW, ESP32 phat `SIREN BATTERY LOW` (E502).\nNgay ca khi may idle, STATUS 6 h/lan dam bao 9V-low khong bi bo quen vo thoi han.\n\n## Hieu chinh nguong 3.3 V va 9 V\nPB2/PB3 hien dung DIGITAL + PCINT. Nguong thuc te do divider + VIH/VIL/hysteresis + VCC Tiny.\nTrong `ATTINY13A_POWER_ALARM.ino` co 4 placeholder (mV, do tai nguon truoc divider):\n- `FIELD_MEASURED_3V3_LOSS_MV`\n- `FIELD_MEASURED_3V3_RESTORE_MV`\n- `FIELD_MEASURED_9V_LOW_MV`\n- `FIELD_MEASURED_9V_OK_MV`\n\n0 = chua bench-calibrate. Cac so nay hien chi la ghi chu, chua dieu khien threshold.\n\n## Low-power policy\n- Power-down sleep la trang thai mac dinh.\n- WDT chi bat khi xu ly BUS, tat truoc khi ngu.\n- ADC va analog comparator khong dung duoc tat ro rang khi boot.\n- Khi ESP mat nguon, PB0 bi mask khoi PCINT va keo LOW de tranh floating/wake gia/back-power.\n- Khong co polling nhanh; status 1 h khi arm, 6 h khi idle.\n- Emergency siren reassert 15 s chi xay ra trong tinh huong khan cap, khong anh huong tuoi pin binh thuong.\n\nMuc tieu bench: dong toan mach Tiny khi ngu <= 1-3 uA. Voi CR2032 chinh hang, muc tieu thuc te\n5-8 nam la hop ly neu PCB/divider/MOSFET khong tao dong ro lon va BOD fuse duoc cau hinh phu hop.\n\n## Fail-safe\n- EEPROM batch/activity record hong -> arm thay vi im lang.\n- Tiny boot khi arm va PB2 LOW -> coi bat ngay.\n- E501: mat giao tiep ATtiny.\n- E502: nguon 9V coi low.\n- E503: batch **hoac activity** ESP/Tiny khong dong bo.\n- Loi ATtiny chi la diagnostic cho ESP32; Tiny khong duoc quyen cat/ep output dieu khien chinh.\n\n## Build gate\nGitHub Actions build ATtiny13A bang avr-g++ va fail neu Flash >1024 B hoac static RAM >64 B.\nCI cung kiem protocol/message/status constants giua ESP32 va Tiny truoc khi compile firmware chinh.\n""")


# -----------------------------------------------------------------------------
# Reliability checker: protect final V3 architecture.
# -----------------------------------------------------------------------------
path = "tools/check_v381_reliability.py"
text = read(path)
old = '''require(config, "ATTINY_STATUS_FLAG_9V_LOW = 2U", "ATtiny 9V status retained")
require(machine, "physicalOut.turnLeft || physicalOut.turnRight", "outside-batch turning power-loss arm")
require(machine, "physicalOut.circulationFan || physicalOut.ventFan || physicalOut.heaterSsr", "outside-batch fan/heater power-loss arm")
require(machine, "const bool expectedActivity = !expectedBatch", "activity only outside batch")
require(machine, "ATTINY_ACTIVITY_REASSERT_MS", "ATtiny RAM activity reassert")
require(machine, "keepPowerLossArmed", "batch-stop no-gap handoff")
'''
new = '''require(config, "ATTINY_STATUS_FLAG_9V_LOW = 2U", "ATtiny 9V status retained")
require(config, "ATTINY_STATUS_FLAG_ACTIVITY = 8U", "ATtiny activity status flag")
require(config, "ATTINY_ACTIVITY_OFF_CONFIRM_MS = 30000UL", "ATtiny activity off debounce")
require(config, "ATTINY_STATUS_IDLE_INTERVAL_MS = 6UL * 3600UL * 1000UL", "ATtiny low-duty idle status")
require(machine, "physicalOut.turnLeft || physicalOut.turnRight", "outside-batch turning power-loss arm")
require(machine, "physicalOut.circulationFan || physicalOut.ventFan || physicalOut.heatMaster", "outside-batch fan/heat-master power-loss arm")
if "physicalOut.heaterSsr" in machine[machine.find("void updateAttinyLink"):machine.find("void updateBatchTime")]:
    raise SystemExit("FAIL: ATtiny activity must use heatMaster, not heaterSsr")
if "ATTINY_ACTIVITY_REASSERT_MS" in config or "ATTINY_ACTIVITY_REASSERT_MS" in machine:
    raise SystemExit("FAIL: 5-second ATtiny activity reassert reintroduced")
require(machine, "attinyActivitySynced_", "ATtiny two-way activity sync")
require(machine, "keepPowerLossArmed", "batch-stop no-gap handoff")
'''
text = replace_once(text, old, new, "reliability ATtiny checks")
text = replace_once(text,
    'require(attiny, "batchActive || criticalActivity", "ATtiny power-loss alarm OR policy")\n',
    'require(attiny, "batchActive || criticalActivity", "ATtiny power-loss alarm OR policy")\nrequire(attiny, "eeActivityState", "ATtiny activity EEPROM persistence")\nrequire(attiny, "FLAG_ACTIVITY", "ATtiny activity status feedback")\nrequire(attiny, "PRR |= _BV(PRADC)", "ATtiny ADC power reduction")\nrequire(attiny_bus, "EDGE_BUF_SIZE = 48U", "ATtiny v3 4-bit status edge capacity")\n',
    "reliability Tiny persistence/low-power checks")
write(path, text)


# -----------------------------------------------------------------------------
# Build protocol contract: check v3 activity commands/flag and expanded status.
# -----------------------------------------------------------------------------
path = ".github/workflows/build-firmware.yml"
text = read(path)
text = replace_once(text,
    "              ('STATUS_QUERY', r'ATTINY_MSG_STATUS_QUERY\\s*=\\s*(\\d+)U', r'MSG_STATUS_QUERY\\s*=\\s*(\\d+)U'),\n              ('STATUS_BASE', r'ATTINY_MSG_STATUS_BASE\\s*=\\s*(\\d+)U', r'MSG_STATUS_BASE\\s*=\\s*(\\d+)U'),",
    "              ('STATUS_QUERY', r'ATTINY_MSG_STATUS_QUERY\\s*=\\s*(\\d+)U', r'MSG_STATUS_QUERY\\s*=\\s*(\\d+)U'),\n              ('ACTIVITY_ON', r'ATTINY_MSG_ACTIVITY_ON\\s*=\\s*(\\d+)U', r'MSG_ACTIVITY_ON\\s*=\\s*(\\d+)U'),\n              ('ACTIVITY_OFF', r'ATTINY_MSG_ACTIVITY_OFF\\s*=\\s*(\\d+)U', r'MSG_ACTIVITY_OFF\\s*=\\s*(\\d+)U'),\n              ('STATUS_BASE', r'ATTINY_MSG_STATUS_BASE\\s*=\\s*(\\d+)U', r'MSG_STATUS_BASE\\s*=\\s*(\\d+)U'),",
    "workflow activity command contract")
write(path, text)

print("ATtiny v3 finalization patch applied")
