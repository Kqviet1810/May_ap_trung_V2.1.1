#!/usr/bin/env python3
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def load(path):
    p = ROOT / path
    return p, p.read_text(encoding="utf-8")


def save(p, text):
    p.write_text(text, encoding="utf-8")


def exact(text, old, new, label, count=1):
    found = text.count(old)
    if found != count:
        raise SystemExit(f"PATCH FAIL {label}: expected {count}, found {found}")
    return text.replace(old, new, count)


def rex(text, pattern, repl, label, count=1, flags=re.MULTILINE | re.DOTALL):
    out, n = re.subn(pattern, repl, text, count=count, flags=flags)
    if n != count:
        raise SystemExit(f"PATCH FAIL {label}: expected {count}, found {n}")
    return out


# config.h
p, t = load("MAYAP_INDUSTRIAL_v3_4_0/config.h")
t = exact(t, 'constexpr char HMI_FIRMWARE_VERSION[] = "3.7.0";', 'constexpr char HMI_FIRMWARE_VERSION[] = "3.8.1";', "HMI version")
t = exact(t, '#ifndef MAYAP_DIAGNOSTIC_SERIAL\n#define MAYAP_DIAGNOSTIC_SERIAL 1\n#endif', '#ifndef MAYAP_DIAGNOSTIC_SERIAL\n#define MAYAP_DIAGNOSTIC_SERIAL 0\n#endif', "diagnostic default")
t = exact(t, '// Bus giao tiep 2 chieu voi ATtiny13A (mach bao mat dien doc lap dung pin\n// CR2032, xem doc/attiny_power_alarm.md). La bus "ho tro" (open-drain) dung', '// Bus giao tiep 2 chieu voi ATtiny13A (mach bao mat dien doc lap;\n// xem doc/attiny_power_alarm.md). La bus "ho tro" (open-drain) dung', "ATtiny comment")
for old, label in [
    ('constexpr uint8_t ATTINY_STATUS_FLAG_9V_LOW = 2U;\n', '9V flag'),
    ('constexpr uint32_t ATTINY_9V_CONFIRM_MS = 3000UL;\n', '9V confirm'),
    ('  bool attinySirenBatteryLow = false;\n', 'runtime 9V field'),
    ('  bool batchLogAvailable = false;\n', 'runtime fake logger field'),
]:
    t = exact(t, old, '', label)
t = exact(t, '// Trend trong me: 5 phut/mau. Event quan trong duoc ghi ngay khi xay ra.\nconstexpr uint32_t BATCH_LOG_SAMPLE_MS = 300000UL;\nconstexpr size_t BATCH_LOG_MIN_FREE_BYTES = 256U * 1024U;\nconstexpr uint8_t BATCH_LOG_MAX_FILES = 8U;\n', '', "batch logger constants")
restart_gate = '''\n\n// OTA/rollback co the restart MCU. Mac dinh LOCKED tu boot; controlTask chi\n// mo khoa khi khong co batch, resume pending hoac auto-tune dang hoat dong.\ninline volatile bool &mayapFirmwareRestartLockStorage() {\n  static volatile bool locked = true;\n  return locked;\n}\ninline void mayapSetFirmwareRestartLocked(bool locked) {\n  __atomic_store_n(&mayapFirmwareRestartLockStorage(), locked, __ATOMIC_RELEASE);\n}\ninline bool mayapFirmwareRestartLocked() {\n  return __atomic_load_n(&mayapFirmwareRestartLockStorage(), __ATOMIC_ACQUIRE);\n}\n'''
t = rex(t, r'(inline bool mayapConsumeIntentionalRestart\(\) \{.*?\n\})', r'\1' + restart_gate, "restart gate")
save(p, t)

# ATtiny13A: delete 9V/battery sensing while retaining protocol-v2 compatibility.
p, t = load("ATTINY13A_POWER_ALARM/ATTINY13A_POWER_ALARM.ino")
t = exact(t, '// PB3 9V siren-supply sense. AVR-libc only; target ATtiny13A @ 1.2 MHz.', '// PB3 unused; 9V/battery measurement removed. AVR-libc only; target ATtiny13A @ 1.2 MHz.', "Tiny header")
t = exact(t, 'constexpr uint8_t PIN_9V = PB3;\n', '', "Tiny PIN_9V")
t = exact(t, 'constexpr uint8_t FLAG_9V_LOW = 2U;\n', '', "Tiny FLAG_9V_LOW")
t = exact(t, 'static inline bool nineVoltOk() { return (PINB & _BV(PIN_9V)) != 0U; }\n', '', "Tiny nineVoltOk")
t = exact(t, '  if (!nineVoltOk()) flags |= FLAG_9V_LOW;\n', '', "Tiny status 9V")
t = exact(t, '    PCMSK = _BV(PIN_BUS) | _BV(PIN_3V3) | _BV(PIN_9V);', '    PCMSK = _BV(PIN_BUS) | _BV(PIN_3V3);', "Tiny wake mask ON")
t = exact(t, '    PCMSK = _BV(PIN_3V3) | _BV(PIN_9V);', '    PCMSK = _BV(PIN_3V3);', "Tiny wake mask OFF")
t = exact(t, '_delay_ms(50);  // debounce PB2/PB3/edge wake.', '_delay_ms(50);  // debounce PB2/BUS edge wake.', "Tiny debounce comment")
save(p, t)

# machine_control.h
p, t = load("MAYAP_INDUSTRIAL_v3_4_0/machine_control.h")
t = rex(t, r'// ============================================================================\n// NHAT KY FLASH TAM VO HIEU HOA.*?// ============================================================================\n// FAULT MANAGER', '// ============================================================================\n// FAULT MANAGER', "remove BatchLogger class")
t = exact(t, '  BatchLogUnavailable = 315,\n', '', "remove E315 enum")
t = exact(t, '  SirenBatteryLow = 502,\n', '', "remove E502 enum")
t = exact(t, 'constexpr uint8_t FAULT_CODE_REAL_COUNT = 41U;', 'constexpr uint8_t FAULT_CODE_REAL_COUNT = 39U;', "fault count")
t = rex(t, r'^\s*\{FaultCode::BatchLogUnavailable,.*?\n', '', "remove E315 descriptor", flags=re.MULTILINE)
t = rex(t, r'^\s*\{FaultCode::SirenBatteryLow,.*?\n', '', "remove E502 descriptor", flags=re.MULTILINE)
t = exact(t, '  // Nhiet cao: cam SSR, nha contactor tong, bat ca hai quat.\n  {FaultCode::HighTemperature, FaultSeverity::Stop, 240U, AlarmTempHigh, false, true, true, true, true, true, "TEMP HIGH"},', '  // Nhiet cao: cam SSR + bat hai quat, NHUNG GIU D14/contactor ON.\n  // Chi EmergencyTemperature/loi master-drop moi nha D14.\n  {FaultCode::HighTemperature, FaultSeverity::Stop, 240U, AlarmTempHigh, false, true, false, true, true, true, "TEMP HIGH"},', "High Temp D14")
t = t.replace('// F-08: canh bao THUAN CHAN DOAN - khong cat nhiet, khong dung dao, khong', '// SensorFrozen co bang chung heater-on: STOP fail-safe, cat SSR + D14; khong')
t = t.replace('// force quat. Chi cho operator biet sensor co the bi dong bang. SensorLost/', '// cho phep tiep tuc dieu khien; chi danh khi heater-on evidence du nguong.')
t = rex(t, r'(bool healthRestartRequested\(\) const \{ return healthRestartRequested_; \})', r'\1\n\n  bool firmwareRestartLocked() const {\n    return batchRunning_ || resumePending_ || autotune_.running();\n  }', "Machine restart state")

# Remove fake logger lifecycle and members.
t = re.sub(r'^\s*\(void\)batchLogger_\.begin\(\);\n', '', t, flags=re.MULTILINE)
t = re.sub(r'\n\s*if \(batchStartEpoch_ != 0U\) \{\n\s*batchLogFaultActive_ = !batchLogger_\.start\(batchStartEpoch_, true\);\n\s*\}', '', t)
t = re.sub(r'\n\s*if \(!batchLogger_\.active\(\) && batchStartEpoch_ != 0U\) \{\n\s*batchLogFaultActive_ = !batchLogger_\.start\(batchStartEpoch_, true\);\n\s*\}', '', t)
for pat in [
    r'^\s*batchLogger_\.stop\(\);\n',
    r'^\s*batchLogFaultActive_ = false;\n',
    r'^\s*batchLogFaultActive_ = !batchLogger_\.start\([^\n]+\);\n',
    r'^\s*batchLogSampleSequence_ = 0U;\n',
    r'^\s*batchLogGate_\.reset\([^\n]+\);\n',
    r'^\s*serviceBatchLog\(now\);\n',
    r'^\s*BatchLogger batchLogger_\{\};\n',
    r'^\s*PeriodicGate batchLogGate_\{BATCH_LOG_SAMPLE_MS\};\n',
    r'^\s*uint32_t batchLogSampleSequence_ = 0U;\n',
    r'^\s*bool batchLogFaultActive_ = false;\n',
    r'^\s*runtime_\.batchLogAvailable = false;\n',
]:
    t = re.sub(pat, '', t, flags=re.MULTILINE)
t = rex(t, r'\n\s*faults_\.set\(FaultCode::BatchLogUnavailable,\s*\n\s*\(batchRunning_ \|\| resumePending_\) &&\s*\n\s*\(batchLogFaultActive_ \|\| !batchLogger_\.healthy\(\)\), now\);', '', "remove E315 fault")
t = rex(t, r'\n\s*void serviceBatchLog\(uint32_t now\) \{.*?\n\s*\}\n\n\s*void checkpointBatch\(\)', '\n\n  void checkpointBatch()', "remove serviceBatchLog")
t = rex(t, r'\s*\} else if \(cmd && !strcmp\(cmd, "LOG"\) && arg1 && !strcmp\(arg1, "FILES"\)\) \{\n\s*batchLogger_\.printFiles\(\);\n\s*\} else if \(cmd && !strcmp\(cmd, "LOG"\) && arg1 && !strcmp\(arg1, "CLEAR"\)\) \{', '    } else if (cmd && !strcmp(cmd, "LOG") && arg1 && !strcmp(arg1, "CLEAR")) {', "remove LOG FILES")
t = t.replace('LOG SHOW [N]|FILES|CLEAR(RAM)', 'LOG SHOW [N]|CLEAR(RAM)')

# Remove 9V/E502 path.
t = re.sub(r'^\s*const bool reported9vLow = .*?;\n', '', t, flags=re.MULTILINE)
t = rex(t, r'\n\s*if \(reported9vLow\) \{.*?\n\s*\} else \{\n\s*attiny9vLow_ = false;\n\s*attiny9vConfirmPending_ = false;\n\s*\}', '', "remove 9V debounce")
for pat in [
    r'^\s*faults_\.set\(FaultCode::SirenBatteryLow,.*?\n',
    r'^\s*runtime_\.attinySirenBatteryLow = attiny9vLow_;\n',
    r'^\s*bool attiny9vLow_ = false;\n',
    r'^\s*bool attiny9vConfirmPending_ = false;\n',
    r'^\s*uint32_t attiny9vLowSince_ = 0U;\n',
]:
    t = re.sub(pat, '', t, flags=re.MULTILINE)
t = t.replace('  } else if (attiny9vConfirmPending_ && elapsedMs(now, attiny9vLowSince_) < ATTINY_9V_CONFIRM_MS) {\n    nextAttinyStatusAt_ = now + 250UL;\n', '')
t = t.replace('  mayapSerialPrintf(false, "[ATtiny] status batch=%u 9v=%u siren=%u sync=%u\\n",\n                    reportedBatch ? 1U : 0U, reported9vLow ? 1U : 0U,\n                    reportedSiren ? 1U : 0U, attinyBatchSynced_ ? 1U : 0U);', '  mayapSerialPrintf(false, "[ATtiny] status batch=%u siren=%u sync=%u\\n",\n                    reportedBatch ? 1U : 0U, reportedSiren ? 1U : 0U,\n                    attinyBatchSynced_ ? 1U : 0U);')

# Command-side OTA/rollback guard.
t = exact(t, '      case HmiCommandType::FirmwareWebApply:\n        mayapRequestFirmwareWebApply();\n        ok = true;\n        resultText = "DANG TAI FIRMWARE...";\n        break;', '      case HmiCommandType::FirmwareWebApply:\n        if (firmwareRestartLocked()) {\n          ok = false;\n          resultText = "DUNG ME/RESUME/AUTOTUNE";\n        } else {\n          mayapRequestFirmwareWebApply();\n          ok = true;\n          resultText = "DANG TAI FIRMWARE...";\n        }\n        break;', "block web OTA")
t = exact(t, '      case HmiCommandType::FirmwareRollback:\n        if (!mayapFirmwareRollbackAvailable()) {\n          ok = false;\n          resultText = "KHONG CO BAN CU";\n        } else {\n          mayapRequestFirmwareRollback();\n          ok = true;\n          resultText = "DANG QUAY LAI...";\n        }\n        break;', '      case HmiCommandType::FirmwareRollback:\n        if (firmwareRestartLocked()) {\n          ok = false;\n          resultText = "DUNG ME/RESUME/AUTOTUNE";\n        } else if (!mayapFirmwareRollbackAvailable()) {\n          ok = false;\n          resultText = "KHONG CO BAN CU";\n        } else {\n          mayapRequestFirmwareRollback();\n          ok = true;\n          resultText = "DANG QUAY LAI...";\n        }\n        break;', "block rollback")
t = t.replace('CHI GIU LAI 1 ngoai le duy nhat cho viec NHA contactor ngay: qua nhiet KHAN CAP.', 'D14 bi nha boi EmergencyTemperature, sensor/fault master-drop hoac system trip; HighTemperature thuong chi cam SSR.')
t = exact(t, '[SERIAL] ALARM mute 5 phut.\n', '[SERIAL] ALARM mute 1 phut.\n', "mute text")
t = t.replace('file log', 'moc thoi gian me').replace('kiem tra pin CR2032', 'kiem tra nguon ATtiny va day BUS')
for banned in ['BatchLogger', 'batchLogger_', 'batchLogFaultActive_', 'batchLogSampleSequence_', 'batchLogGate_', 'FaultCode::SirenBatteryLow', 'attiny9vLow_', 'attiny9vConfirmPending_', 'attiny9vLowSince_', 'ATTINY_9V_CONFIRM_MS', 'ATTINY_STATUS_FLAG_9V_LOW']:
    if banned in t:
        raise SystemExit(f"PATCH FAIL machine cleanup: {banned} remains")
save(p, t)

# controlTask publishes the final restart lock.
p, t = load("MAYAP_INDUSTRIAL_v3_4_0/MAYAP_INDUSTRIAL_v3_4_0.ino")
t = exact(t, '    Machine.update(now);\n', '    Machine.update(now);\n    mayapSetFirmwareRestartLocked(Machine.firmwareRestartLocked());\n', "control OTA lock")
t = exact(t, '  Machine.begin();\n', '  Machine.begin();\n  mayapSetFirmwareRestartLocked(Machine.firmwareRestartLocked());\n', "initial OTA lock")
save(p, t)

# Final gates in all restart-capable OTA paths.
p, t = load("MAYAP_INDUSTRIAL_v3_4_0/ota_web_update.h")
t = exact(t, '  if (__atomic_exchange_n(&applyRequestFlag, false, __ATOMIC_ACQ_REL)) {\n    if (pendingAvailable && pendingVersion[0] && pendingSha256[0] && pendingSignature[0] && pendingSize > 0U) {', '  if (__atomic_exchange_n(&applyRequestFlag, false, __ATOMIC_ACQ_REL)) {\n    if (mayapFirmwareRestartLocked()) {\n      setError("Dang co me/resume/auto-tune");\n      mayapSerialPrintf(true, "[OTA-WEB] APPLY bi khoa: dang co me/resume/auto-tune\\n");\n      return;\n    }\n    if (pendingAvailable && pendingVersion[0] && pendingSha256[0] && pendingSignature[0] && pendingSize > 0U) {', "remote OTA final gate")
save(p, t)
p, t = load("MAYAP_INDUSTRIAL_v3_4_0/ota_rollback.h")
t = exact(t, 'inline void mayapFirmwareRollbackUpdate() {\n  using namespace MayapRollbackInternal;\n  if (!__atomic_exchange_n(&rollbackRequested, false, __ATOMIC_ACQ_REL)) return;\n', 'inline void mayapFirmwareRollbackUpdate() {\n  using namespace MayapRollbackInternal;\n  if (!__atomic_exchange_n(&rollbackRequested, false, __ATOMIC_ACQ_REL)) return;\n  if (mayapFirmwareRestartLocked()) {\n    mayapSerialPrintf(true, "[ROLLBACK] Bi khoa: dang co me/resume/auto-tune\\n");\n    return;\n  }\n', "rollback final gate")
save(p, t)
p, t = load("MAYAP_INDUSTRIAL_v3_4_0/ota_update.h")
t = exact(t, 'inline void mayapOtaUpdate() {\n  MayapOtaInternal::applyNetworkPolicy();\n  if (MayapOtaInternal::active) ArduinoOTA.handle();\n}', 'inline void mayapOtaUpdate() {\n  MayapOtaInternal::applyNetworkPolicy();\n  // Không service ArduinoOTA khi batch/resume/auto-tune đang hoạt động.\n  if (MayapOtaInternal::active && !mayapFirmwareRestartLocked()) ArduinoOTA.handle();\n}', "LAN OTA gate")
save(p, t)

# Cloud names.
p, t = load("MAYAP_INDUSTRIAL_v3_4_0/cloud_alert_link.h")
t = exact(t, '    case 134: return "Nhiệt độ có xu hướng chạm ngưỡng cảnh báo";\n', '    case 134: return "Nhiệt độ có xu hướng chạm ngưỡng cảnh báo";\n    case 135: return "Đang chờ xác nhận tiếp tục mẻ";\n    case 136: return "Mẻ đã quá ngày ấp dự kiến";\n    case 137: return "RTC chưa sẵn sàng để phục hồi mẻ";\n    case 138: return "Đang chờ xác nhận mẻ quá hạn";\n', "cloud 135-138")
t = exact(t, '    case 204: return "Lịch đảo trứng bị trễ";\n', '    case 204: return "Lịch đảo trứng bị trễ";\n    case 205: return "Cần kiểm tra cơ khí đảo trứng";\n', "cloud 205")
t = re.sub(r'^\s*case 315:.*?\n', '', t, flags=re.MULTILINE)
t = re.sub(r'^\s*case 502:.*?\n', '', t, flags=re.MULTILINE)
save(p, t)

# Web Auto Resume lock.
p, t = load("app.js")
anchor = """    const totalDaysInput = $('totalDays');\n    if (totalDaysInput) {\n      totalDaysInput.disabled = Boolean(runtime.batchRunning);\n      totalDaysInput.title = runtime.batchRunning\n        ? 'Đang có mẻ chạy - khoá số ngày ấp (giống trên máy), dừng mẻ để đổi'\n        : '';\n    }\n"""
addition = anchor + """    const autoResumeInput = $('resumeAfterPowerLoss');\n    if (autoResumeInput) {\n      autoResumeInput.disabled = Boolean(runtime.batchRunning);\n      autoResumeInput.title = runtime.batchRunning\n        ? 'Đang có mẻ chạy/đang chờ phục hồi - dừng hoặc xử lý mẻ trước khi đổi Auto Resume'\n        : '';\n    }\n"""
t = exact(t, anchor, addition, "web Auto Resume")
save(p, t)

# HMI metadata.
p, t = load("MAYAP_INDUSTRIAL_v3_4_0/hmi.h")
t = exact(t, 'MAYAP HMI ST7567S 128x64 + rotary + buzzer - v3.7.0', 'MAYAP HMI ST7567S 128x64 + rotary + buzzer - v3.8.1', "HMI header")
t = exact(t, 'Phan cung: LCD 0x3F SDA8/SCL9, rotary 38/39/40, buzzer GPIO41.', 'Phan cung: LCD 0x3F SDA8/SCL9, rotary 38/39/40, buzzer GPIO2; GPIO41 la ATtiny BUS.', "HMI pins")
save(p, t)

# Safety/current docs.
p, t = load("doc/SAFETY_HARDWARE_REQUIREMENTS.md")
t = exact(t, '- High Temperature: SSR OFF + contactor OFF + ép vent/circulation fan ON.', '- High Temperature: SSR OFF + GIỮ contactor D14 ON + ép vent/circulation fan ON. Chỉ Emergency (hoặc fault master-drop khác) mới nhả D14.', "safety High")
t = exact(t, '- Ép nhiệt vượt ngưỡng High: contactor phải nhả và cả hai fan phải chạy.', '- Ép nhiệt vượt ngưỡng High: SSR phải OFF, D14 phải GIỮ ON và cả hai fan phải chạy; vượt Emergency thì D14 phải nhả.', "safety High test")
save(p, t)
p, t = load("doc/COMMISSIONING_V3_8_1.md")
t = exact(t, '- [ ] Ép nhiệt cao: SSR OFF + contactor OFF + quạt an toàn theo thiết kế.', '- [ ] Ép High Temp: SSR OFF + D14/contactor GIỮ ON + quạt an toàn ON; Ép Emergency: D14 phải OFF.', "commissioning High")
t = exact(t, '- [ ] boot firmware mới thành công và rollback contract còn hoạt động.', '- [ ] boot firmware mới thành công và rollback contract còn hoạt động.\n- [ ] Khi batch/resume/autotune đang hoạt động: Web APPLY, rollback và ArduinoOTA LAN đều KHÔNG được phép bắt đầu/restart.', "commissioning OTA")
save(p, t)
p, t = load("doc/attiny_power_alarm.md")
t = exact(t, '- PB3: sense nguon 9V coi (HIGH = 9V OK theo nguong phan ap tren PCB).', '- PB3: không sử dụng; chức năng đo 9V/pin đã loại bỏ.', "ATtiny doc PB3")
t = exact(t, 'tra frame status 6..13; `status-6` la bitmask: bit0=batch, bit1=9V low,\nbit2=emergency siren mirror. ESP ACK frame status.', 'tra frame status theo protocol v2; `status-6` là bitmask: bit0=batch,\nbit2=emergency siren mirror. Bit1 để trống nhằm tương thích protocol cũ. ESP ACK frame status.', "ATtiny doc status")
t = exact(t, '- E501: mat giao tiep; E502: 9V coi low; E503: state batch ESP/Tiny khong dong bo.', '- E501: mất giao tiếp; E503: state batch ESP/Tiny không đồng bộ. E502 đã loại bỏ cùng chức năng đo 9V/pin.', "ATtiny doc faults")
t = t.replace('dong ngu/CR2032', 'dong ngu/nguon backup')
save(p, t)

# Metadata/audit.
p, t = load("release-manifest.json")
data = json.loads(t); data["hmi"] = "3.8.1"; save(p, json.dumps(data, ensure_ascii=False, indent=2) + "\n")
p, t = load("README.md"); t = t.replace('HMI 3.7.0', 'HMI 3.8.1').replace('HMI **3.7.0**', 'HMI **3.8.1**'); save(p, t)
p, t = load("audit/00_AUDIT_STATUS.md")
t = rex(t, r'```text\nCurrent phase:.*?```', '```text\nCurrent phase:  v3.8.1 RELIABILITY / C1-C2 REMEDIATION\nTarget:         branch hardening/v3.8.1-reliability\nBaseline:       v3.7 audit below is historical; current delta is 06_V381_DELTA_AUDIT.md\nRuntime:        CI compile/regression available; hardware commissioning remains required\n```', "audit header")
save(p, t)
p, t = load("audit/06_V381_DELTA_AUDIT.md")
if '## C1/C2 remediation addendum' not in t:
    t += '''\n\n## C1/C2 remediation addendum\n- High Temp: SSR OFF, D14 remains ON; Emergency/master-drop releases D14.\n- Web OTA APPLY, rollback and ArduinoOTA LAN locked during batch/resume/autotune.\n- ATtiny 9V/battery measurement and E502 removed; PB3 unused.\n- Cloud names added for E135/E136/E137/E138/E205.\n- Web Auto Resume locked while batch/resume is active.\n- Fake persistent BatchLogger/E315 removed; RAM EventLog remains supported.\n- Production diagnostic serial default OFF; DEV/PILOT explicitly enable diagnostics.\n- HMI metadata/pin and alarm-mute text synchronized.\n- Safety/commissioning comments synchronized with executable logic.\n'''
save(p, t)

# Permanent regression gates.
p, t = load("tools/check_v381_reliability.py")
insert = '''\n# C1/C2 remediation invariants.\nrequire_re(machine, r'\\{FaultCode::HighTemperature,\\s*FaultSeverity::Stop,.*?false,\\s*true,\\s*false,\\s*true,\\s*true,\\s*true,\\s*"TEMP HIGH"\\}', "HighTemperature keeps D14")\nrequire(machine, "firmwareRestartLocked() const", "Machine restart-sensitive state")\nfor path in ["MAYAP_INDUSTRIAL_v3_4_0/ota_web_update.h", "MAYAP_INDUSTRIAL_v3_4_0/ota_rollback.h", "MAYAP_INDUSTRIAL_v3_4_0/ota_update.h"]:\n    require(read(path), "mayapFirmwareRestartLocked()", f"OTA gate {path}")\nfor forbidden in ["SirenBatteryLow", "BatchLogUnavailable", "BatchLogger", "ATTINY_STATUS_FLAG_9V_LOW", "ATTINY_9V_CONFIRM_MS", "attinySirenBatteryLow"]:\n    if forbidden in config or forbidden in machine:\n        raise SystemExit(f"FAIL: removed feature returned: {forbidden}")\ntiny = read("ATTINY13A_POWER_ALARM/ATTINY13A_POWER_ALARM.ino")\nfor forbidden in ["PIN_9V", "FLAG_9V_LOW", "nineVoltOk"]:\n    if forbidden in tiny:\n        raise SystemExit(f"FAIL: ATtiny 9V measurement returned: {forbidden}")\napp = read("app.js")\nrequire(app, "autoResumeInput.disabled = Boolean(runtime.batchRunning)", "web Auto Resume batch lock")\nfor code in [135, 136, 137, 138, 205]: require(cloud, f"case {code}:", f"cloud fault E{code}")\nhmi = read("MAYAP_INDUSTRIAL_v3_4_0/hmi.h")\nrequire_re(config, r'HMI_FIRMWARE_VERSION\\[\\]\\s*=\\s*"3\\.8\\.1"', "HMI metadata")\nrequire(hmi, "buzzer GPIO2; GPIO41 la ATtiny BUS", "HMI pin metadata")\nrequire(config, "#define MAYAP_DIAGNOSTIC_SERIAL 0", "production diagnostic default")\nrequire(machine, "ALARM mute 1 phut", "alarm mute text")\n'''
t = exact(t, '# Security regression tripwires.\n', insert + '\n# Security regression tripwires.\n', "regression checker")
save(p, t)

print("C1/C2 patch applied successfully")
