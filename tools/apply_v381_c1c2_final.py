#!/usr/bin/env python3
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8")


def sub1(text, pattern, repl, label, flags=re.MULTILINE | re.DOTALL):
    out, n = re.subn(pattern, repl, text, count=1, flags=flags)
    if n != 1:
        raise SystemExit(f"PATCH FAIL {label}: expected 1, found {n}")
    return out


def require_absent(text, needles, label):
    for needle in needles:
        if needle in text:
            raise SystemExit(f"PATCH FAIL {label}: still contains {needle}")

# -----------------------------------------------------------------------------
# config.h: production defaults, HMI metadata, remove dead 9V/logger fields,
# and publish a cross-task restart lock for all restart-capable OTA paths.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/config.h"
t = read(path)
t = sub1(t, r'constexpr char HMI_FIRMWARE_VERSION\[\]\s*=\s*"3\.7\.0";', 'constexpr char HMI_FIRMWARE_VERSION[] = "3.8.1";', "HMI version")
t = sub1(t, r'#ifndef MAYAP_DIAGNOSTIC_SERIAL\s*\n#define MAYAP_DIAGNOSTIC_SERIAL\s+1\s*\n#endif', '#ifndef MAYAP_DIAGNOSTIC_SERIAL\n#define MAYAP_DIAGNOSTIC_SERIAL 0\n#endif', "diagnostic default")
t = t.replace('mach bao mat dien doc lap dung pin\n// CR2032', 'mach bao mat dien doc lap;\n// xem doc/attiny_power_alarm.md')
t = re.sub(r'^constexpr uint8_t ATTINY_STATUS_FLAG_9V_LOW\s*=.*?\n', '', t, flags=re.MULTILINE)
t = re.sub(r'^constexpr uint32_t ATTINY_9V_CONFIRM_MS\s*=.*?\n', '', t, flags=re.MULTILINE)
t = re.sub(r'^\s*bool attinySirenBatteryLow\s*=.*?\n', '', t, flags=re.MULTILINE)
t = re.sub(r'^\s*bool batchLogAvailable\s*=.*?\n', '', t, flags=re.MULTILINE)
t = re.sub(r'// Trend trong me: 5 phut/mau\..*?constexpr uint8_t BATCH_LOG_MAX_FILES\s*=.*?;\n', '', t, flags=re.DOTALL)
if 'mayapFirmwareRestartLockStorage' not in t:
    gate = '''\n\n// OTA/rollback co the restart MCU. Mac dinh LOCKED tu boot; controlTask chi\n// mo khoa khi khong co batch, resume pending hoac auto-tune dang hoat dong.\ninline volatile bool &mayapFirmwareRestartLockStorage() {\n  static volatile bool locked = true;\n  return locked;\n}\ninline void mayapSetFirmwareRestartLocked(bool locked) {\n  __atomic_store_n(&mayapFirmwareRestartLockStorage(), locked, __ATOMIC_RELEASE);\n}\ninline bool mayapFirmwareRestartLocked() {\n  return __atomic_load_n(&mayapFirmwareRestartLockStorage(), __ATOMIC_ACQUIRE);\n}\n'''
    t = sub1(t, r'(inline bool mayapConsumeIntentionalRestart\(\) \{.*?\n\})', r'\1' + gate, "restart lock storage")
write(path, t)

# -----------------------------------------------------------------------------
# ATtiny13A: remove 9V/battery measurement completely. Keep protocol v2 and
# reserve bit1 implicitly, so installed ESP/Tiny pairs do not need a version bump.
# -----------------------------------------------------------------------------
path = "ATTINY13A_POWER_ALARM/ATTINY13A_POWER_ALARM.ino"
t = read(path)
t = t.replace('// PB3 9V siren-supply sense. AVR-libc only; target ATtiny13A @ 1.2 MHz.', '// PB3 unused; 9V/battery measurement removed. AVR-libc only; target ATtiny13A @ 1.2 MHz.')
for pat in [
    r'^constexpr uint8_t PIN_9V\s*=.*?\n',
    r'^constexpr uint8_t FLAG_9V_LOW\s*=.*?\n',
    r'^static inline bool nineVoltOk\(\).*?\n',
    r'^\s*if \(!nineVoltOk\(\)\) flags \|= FLAG_9V_LOW;\n',
]:
    t = re.sub(pat, '', t, flags=re.MULTILINE)
t = t.replace('PCMSK = _BV(PIN_BUS) | _BV(PIN_3V3) | _BV(PIN_9V);', 'PCMSK = _BV(PIN_BUS) | _BV(PIN_3V3);')
t = t.replace('PCMSK = _BV(PIN_3V3) | _BV(PIN_9V);', 'PCMSK = _BV(PIN_3V3);')
t = t.replace('_delay_ms(50);  // debounce PB2/PB3/edge wake.', '_delay_ms(50);  // debounce PB2/BUS edge wake.')
require_absent(t, ['PIN_9V', 'FLAG_9V_LOW', 'nineVoltOk'], 'ATtiny 9V removal')
write(path, t)

# -----------------------------------------------------------------------------
# machine_control.h: High Temp keeps D14, restart state API, remove fake
# persistent BatchLogger/E315, remove E502 and all 9V state/debounce paths.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/machine_control.h"
t = read(path)

# BatchLogger class is intentionally no-op today; remove the entire dead block.
t = sub1(t, r'// ============================================================================\n// NHAT KY FLASH TAM VO HIEU HOA.*?// ============================================================================\n// FAULT MANAGER', '// ============================================================================\n// FAULT MANAGER', "BatchLogger class removal")

# Fault enums/descriptors.
t = re.sub(r'^\s*BatchLogUnavailable\s*=\s*315,.*?\n', '', t, flags=re.MULTILINE)
t = re.sub(r'^\s*SirenBatteryLow\s*=\s*502,.*?\n', '', t, flags=re.MULTILINE)
t = sub1(t, r'constexpr uint8_t FAULT_CODE_REAL_COUNT\s*=\s*41U;', 'constexpr uint8_t FAULT_CODE_REAL_COUNT = 39U;', "fault count")
t = re.sub(r'^\s*\{FaultCode::BatchLogUnavailable,.*?\n', '', t, flags=re.MULTILINE)
t = re.sub(r'^\s*\{FaultCode::SirenBatteryLow,.*?\n', '', t, flags=re.MULTILINE)

# HighTemperature: inhibit SSR but keep heat master/contact D14 ON.
old_desc = '{FaultCode::HighTemperature, FaultSeverity::Stop, 240U, AlarmTempHigh, false, true, true, true, true, true, "TEMP HIGH"}'
new_desc = '{FaultCode::HighTemperature, FaultSeverity::Stop, 240U, AlarmTempHigh, false, true, false, true, true, true, "TEMP HIGH"}'
if t.count(old_desc) != 1:
    raise SystemExit(f"PATCH FAIL High Temp descriptor: expected 1, found {t.count(old_desc)}")
t = t.replace(old_desc, new_desc, 1)
t = t.replace('// Nhiet cao: cam SSR, nha contactor tong, bat ca hai quat.', '// Nhiet cao: cam SSR + bat hai quat, GIU D14/contactor ON; Emergency/master-drop moi nha D14.')

# Keep SensorFrozen comments aligned with executable fail-safe behavior.
t = t.replace('// F-08: canh bao THUAN CHAN DOAN - khong cat nhiet, khong dung dao, khong', '// SensorFrozen co bang chung heater-on: STOP fail-safe, cat SSR + D14; khong')
t = t.replace('// force quat. Chi cho operator biet sensor co the bi dong bang. SensorLost/', '// cho phep tiep tuc dieu khien; chi danh khi heater-on evidence du nguong.')

# Expose restart-sensitive state from the controller.
if 'bool firmwareRestartLocked() const' not in t:
    t = sub1(t, r'(\n private:\n  enum class BatchPhase)', '\n  bool firmwareRestartLocked() const {\n    return batchRunning_ || resumePending_ || autotune_.running();\n  }\n\1', "Machine restart state")

# Remove logger lifecycle/counters/calls.
for pat in [
    r'^\s*\(void\)batchLogger_\.begin\(\);\n',
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
t = re.sub(r'\n\s*if \(batchStartEpoch_ != 0U\) \{\s*batchLogFaultActive_ = !batchLogger_\.start\(batchStartEpoch_, true\);\s*\}', '', t, flags=re.DOTALL)
t = re.sub(r'\n\s*if \(!batchLogger_\.active\(\) && batchStartEpoch_ != 0U\) \{\s*batchLogFaultActive_ = !batchLogger_\.start\(batchStartEpoch_, true\);\s*\}', '', t, flags=re.DOTALL)
t = re.sub(r'\n\s*faults_\.set\(FaultCode::BatchLogUnavailable,.*?\);', '', t, count=1, flags=re.DOTALL)
t = sub1(t, r'\n\s*void serviceBatchLog\(uint32_t now\) \{.*?\n\s*\}\s*\n\s*void checkpointBatch\(\)', '\n\n  void checkpointBatch()', "serviceBatchLog removal")
t = re.sub(r'\s*\} else if \(cmd && !strcmp\(cmd, "LOG"\) && arg1 && !strcmp\(arg1, "FILES"\)\) \{\s*batchLogger_\.printFiles\(\);\s*\} else if \(cmd && !strcmp\(cmd, "LOG"\) && arg1 && !strcmp\(arg1, "CLEAR"\)\) \{', '    } else if (cmd && !strcmp(cmd, "LOG") && arg1 && !strcmp(arg1, "CLEAR")) {', t, count=1, flags=re.DOTALL)
t = t.replace('LOG SHOW [N]|FILES|CLEAR(RAM)', 'LOG SHOW [N]|CLEAR(RAM)')

# Remove ATtiny 9V parsing/debounce/state.
t = re.sub(r'^\s*const bool reported9vLow = .*?;\n', '', t, flags=re.MULTILINE)
t = re.sub(r'\n\s*if \(reported9vLow == attiny9vLow_\) \{.*?\n\s*\}\s*\n\s*attinyBatchSynced_', '\n\n      attinyBatchSynced_', t, count=1, flags=re.DOTALL)
t = re.sub(r'\s*\} else if \(attiny9vConfirmPending_ && timeReached\(now, attiny9vConfirmAt_\)\) \{.*?\n\s*\} else if \(expectedBatch &&', '    } else if (expectedBatch &&', t, count=1, flags=re.DOTALL)
for pat in [
    r'^\s*faults_\.set\(FaultCode::SirenBatteryLow,.*?\n',
    r'^\s*runtime_\.attinySirenBatteryLow = .*?\n',
    r'^\s*bool attiny9vLow_ = false;\n',
    r'^\s*bool attiny9vCandidate_ = false;\n',
    r'^\s*bool attiny9vConfirmPending_ = false;\n',
    r'^\s*uint32_t attiny9vConfirmAt_ = 0U;\n',
]:
    t = re.sub(pat, '', t, flags=re.MULTILINE)
t = re.sub(r'mayapSerialPrintf\(false,\s*"\[ATTINY\] v=%u link=1 sync=%u expected=%u tinyBatch=%u siren=%u tinySiren=%u 9v=%s\\n",\s*ATTINY_PROTOCOL_VERSION, attinyBatchSynced_ \? 1U : 0U,\s*expectedBatch \? 1U : 0U, attinyTinyBatch_ \? 1U : 0U,\s*desiredSiren \? 1U : 0U, attinyTinySirenOn_ \? 1U : 0U,\s*reported9vLow \? "LOW" : "OK"\);', 'mayapSerialPrintf(false, "[ATTINY] v=%u link=1 sync=%u expected=%u tinyBatch=%u siren=%u tinySiren=%u\\n",\n          ATTINY_PROTOCOL_VERSION, attinyBatchSynced_ ? 1U : 0U,\n          expectedBatch ? 1U : 0U, attinyTinyBatch_ ? 1U : 0U,\n          desiredSiren ? 1U : 0U, attinyTinySirenOn_ ? 1U : 0U);', t, count=1, flags=re.DOTALL)

# Block command-side Web APPLY and rollback while restart-sensitive state is active.
t = sub1(t, r'\s*case HmiCommandType::FirmwareWebApply:.*?\n\s*break;\n\s*case HmiCommandType::FirmwareWebCheckNow:', '''\n        case HmiCommandType::FirmwareWebApply:\n          if (firmwareRestartLocked()) {\n            ok = false; message = "DUNG ME/RESUME/AUTOTUNE";\n          } else {\n            mayapRequestFirmwareWebApply();\n            ok = true; message = "DANG TAI FIRMWARE...";\n          }\n          break;\n        case HmiCommandType::FirmwareWebCheckNow:''', "Web OTA command lock")
t = sub1(t, r'\s*case HmiCommandType::FirmwareRollback:.*?\n\s*break;\n', '''\n        case HmiCommandType::FirmwareRollback:\n          if (firmwareRestartLocked()) {\n            ok = false; message = "DUNG ME/RESUME/AUTOTUNE";\n          } else if (mayapRollbackAvailable()) {\n            mayapRequestFirmwareRollback();\n            ok = true; message = "DANG QUAY LAI FIRMWARE CU...";\n          } else {\n            ok = false; message = "KHONG CO BAN CU DE QUAY LAI";\n          }\n          break;\n''', "rollback command lock")

t = t.replace('CHI GIU LAI 1 ngoai le duy nhat cho viec NHA contactor ngay: qua nhiet KHAN CAP.', 'D14 bi nha boi EmergencyTemperature, sensor/fault master-drop hoac system trip; HighTemperature thuong chi cam SSR.')
t = t.replace('[SERIAL] ALARM mute 5 phut.', '[SERIAL] ALARM mute 1 phut.')
t = t.replace('kiem tra pin CR2032', 'kiem tra nguon ATtiny va day BUS')

require_absent(t, ['BatchLogger', 'batchLogger_', 'batchLogFaultActive_', 'batchLogSampleSequence_', 'batchLogGate_', 'BatchLogUnavailable', 'SirenBatteryLow', 'attiny9vLow_', 'attiny9vCandidate_', 'attiny9vConfirmPending_', 'attiny9vConfirmAt_', 'ATTINY_9V_CONFIRM_MS', 'ATTINY_STATUS_FLAG_9V_LOW', 'reported9vLow'], 'machine cleanup')
write(path, t)

# -----------------------------------------------------------------------------
# Publish controller restart lock from controlTask, and initialize after begin.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/MAYAP_INDUSTRIAL_v3_4_0.ino"
t = read(path)
if 'mayapSetFirmwareRestartLocked(Machine.firmwareRestartLocked());' not in t:
    t = sub1(t, r'(\s*Machine\.update\(now\);)', r'\1\n    mayapSetFirmwareRestartLocked(Machine.firmwareRestartLocked());', "control restart lock")
    t = sub1(t, r'(\s*Machine\.begin\(\);)', r'\1\n  mayapSetFirmwareRestartLocked(Machine.firmwareRestartLocked());', "initial restart lock")
write(path, t)

# -----------------------------------------------------------------------------
# Final gates in every restart-capable OTA path.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/ota_web_update.h"
t = read(path)
if 'APPLY bi khoa: dang co me/resume/auto-tune' not in t:
    t = sub1(t, r'(if \(__atomic_exchange_n\(&applyRequestFlag, false, __ATOMIC_ACQ_REL\)\) \{)', r'\1\n    if (mayapFirmwareRestartLocked()) {\n      setError("Dang co me/resume/auto-tune");\n      mayapSerialPrintf(true, "[OTA-WEB] APPLY bi khoa: dang co me/resume/auto-tune\\n");\n      return;\n    }', "remote OTA final gate")
write(path, t)

path = "MAYAP_INDUSTRIAL_v3_4_0/ota_rollback.h"
t = read(path)
if '[ROLLBACK] Bi khoa' not in t:
    t = sub1(t, r'(if \(!__atomic_exchange_n\(&rollbackRequested, false, __ATOMIC_ACQ_REL\)\) return;)', r'\1\n  if (mayapFirmwareRestartLocked()) {\n    mayapSerialPrintf(true, "[ROLLBACK] Bi khoa: dang co me/resume/auto-tune\\n");\n    return;\n  }', "rollback final gate")
write(path, t)

path = "MAYAP_INDUSTRIAL_v3_4_0/ota_update.h"
t = read(path)
t = sub1(t, r'if \(MayapOtaInternal::active\) ArduinoOTA\.handle\(\);', 'if (MayapOtaInternal::active && !mayapFirmwareRestartLocked()) ArduinoOTA.handle();', "LAN OTA final gate")
write(path, t)

# -----------------------------------------------------------------------------
# Cloud fault names: keep existing E104/E503, add missing active codes and
# remove retired E315/E502 names.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/cloud_alert_link.h"
t = read(path)
if 'case 135:' not in t:
    t = sub1(t, r'(\s*case 134:.*?\n)', r'\1    case 135: return "Đang chờ xác nhận tiếp tục mẻ";\n    case 136: return "Mẻ đã quá ngày ấp dự kiến";\n    case 138: return "Đang chờ xác nhận mẻ quá hạn";\n', "cloud 135/136/138")
# E137 already exists in current source.
if 'case 205:' not in t:
    t = sub1(t, r'(\s*case 204:.*?\n)', r'\1    case 205: return "Cần kiểm tra cơ khí đảo trứng";\n', "cloud 205")
t = re.sub(r'^\s*case 315:.*?\n', '', t, flags=re.MULTILINE)
t = re.sub(r'^\s*case 502:.*?\n', '', t, flags=re.MULTILINE)
write(path, t)

# -----------------------------------------------------------------------------
# Web: lock Auto Resume whenever a batch is active OR recovery confirmation
# is pending, so UI does not offer a setting firmware must reject.
# -----------------------------------------------------------------------------
path = "app.js"
t = read(path)
if 'const autoResumeInput' not in t:
    anchor = r'''(    const totalDaysInput = \$\('totalDays'\);\n    if \(totalDaysInput\) \{.*?\n    \}\n)'''
    addition = r'''\1\n    const autoResumeInput = $('resumeAfterPowerLoss');\n    if (autoResumeInput) {\n      const autoResumeLocked = Boolean(runtime.batchRunning || runtime.resumeConfirmationRequired);\n      autoResumeInput.disabled = autoResumeLocked;\n      autoResumeInput.title = autoResumeLocked\n        ? 'Đang có mẻ hoặc đang chờ phục hồi - xử lý/dừng mẻ trước khi đổi Auto Resume'\n        : '';\n    }\n'''
    t = sub1(t, anchor, addition, "web Auto Resume lock")
write(path, t)

# -----------------------------------------------------------------------------
# HMI metadata and pin comments.
# -----------------------------------------------------------------------------
path = "MAYAP_INDUSTRIAL_v3_4_0/hmi.h"
t = read(path)
t = t.replace('MAYAP HMI ST7567S 128x64 + rotary + buzzer - v3.7.0', 'MAYAP HMI ST7567S 128x64 + rotary + buzzer - v3.8.1')
t = t.replace('Phan cung: LCD 0x3F SDA8/SCL9, rotary 38/39/40, buzzer GPIO41.', 'Phan cung: LCD 0x3F SDA8/SCL9, rotary 38/39/40, buzzer GPIO2; GPIO41 la ATtiny BUS.')
write(path, t)

# -----------------------------------------------------------------------------
# Documentation/audit synchronization. These replacements are intentionally
# tolerant: executable code remains the source of truth and CI verifies it.
# -----------------------------------------------------------------------------
path = "doc/SAFETY_HARDWARE_REQUIREMENTS.md"
t = read(path)
t = t.replace('- High Temperature: SSR OFF + contactor OFF + ép vent/circulation fan ON.', '- High Temperature: SSR OFF + GIỮ contactor D14 ON + ép vent/circulation fan ON. Chỉ Emergency (hoặc fault master-drop khác) mới nhả D14.')
t = t.replace('- Ép nhiệt vượt ngưỡng High: contactor phải nhả và cả hai fan phải chạy.', '- Ép nhiệt vượt ngưỡng High: SSR phải OFF, D14 phải GIỮ ON và cả hai fan phải chạy; vượt Emergency thì D14 phải nhả.')
write(path, t)

path = "doc/COMMISSIONING_V3_8_1.md"
t = read(path)
t = t.replace('- [ ] Ép nhiệt cao: SSR OFF + contactor OFF + quạt an toàn theo thiết kế.', '- [ ] Ép High Temp: SSR OFF + D14/contactor GIỮ ON + quạt an toàn ON; Ép Emergency: D14 phải OFF.')
if 'Web APPLY, rollback và ArduinoOTA LAN' not in t:
    t = t.replace('- [ ] boot firmware mới thành công và rollback contract còn hoạt động.', '- [ ] boot firmware mới thành công và rollback contract còn hoạt động.\n- [ ] Khi batch/resume/autotune đang hoạt động: Web APPLY, rollback và ArduinoOTA LAN đều KHÔNG được phép bắt đầu/restart.')
write(path, t)

path = "doc/attiny_power_alarm.md"
t = read(path)
t = re.sub(r'- PB3:.*', '- PB3: không sử dụng; chức năng đo 9V/pin đã loại bỏ.', t, count=1)
t = t.replace('bit0=batch, bit1=9V low,\nbit2=emergency siren mirror', 'bit0=batch, bit2=emergency siren mirror. Bit1 để trống để tương thích protocol v2')
t = t.replace('E501: mat giao tiep; E502: 9V coi low; E503: state batch ESP/Tiny khong dong bo.', 'E501: mất giao tiếp; E503: state batch ESP/Tiny không đồng bộ. E502 đã loại bỏ cùng chức năng đo 9V/pin.')
write(path, t)

# Metadata manifest.
path = "release-manifest.json"
data = json.loads(read(path)); data['hmi'] = '3.8.1'; write(path, json.dumps(data, ensure_ascii=False, indent=2) + '\n')

path = "README.md"
t = read(path).replace('HMI 3.7.0', 'HMI 3.8.1').replace('HMI **3.7.0**', 'HMI **3.8.1**')
write(path, t)

path = "audit/00_AUDIT_STATUS.md"
t = read(path)
if 'v3.8.1 RELIABILITY / C1-C2 REMEDIATION' not in t:
    t = sub1(t, r'```text\nCurrent phase:.*?```', '```text\nCurrent phase:  v3.8.1 RELIABILITY / C1-C2 REMEDIATION\nTarget:         branch hardening/v3.8.1-reliability\nBaseline:       v3.7 audit below is historical; current delta is 06_V381_DELTA_AUDIT.md\nRuntime:        CI compile/regression + hardware commissioning required\n```', "audit status")
write(path, t)

path = "audit/06_V381_DELTA_AUDIT.md"
t = read(path)
if '## C1/C2 remediation addendum' not in t:
    t += '''\n\n## C1/C2 remediation addendum\n- High Temp: SSR OFF, D14 remains ON; Emergency/master-drop releases D14.\n- Web OTA APPLY, rollback and ArduinoOTA LAN locked during batch/resume/autotune.\n- ATtiny 9V/battery measurement and E502 removed; PB3 unused.\n- Cloud names synchronized for E135/E136/E137/E138/E205.\n- Web Auto Resume locked while batch/recovery is active.\n- Fake persistent BatchLogger/E315 removed; RAM EventLog remains supported.\n- Production diagnostic serial default OFF; DEV/PILOT explicitly enable diagnostics.\n- HMI metadata/pin and alarm-mute text synchronized.\n- Safety/commissioning comments synchronized with executable logic.\n'''
write(path, t)

# -----------------------------------------------------------------------------
# Permanent regression gates for all findings fixed in this pass.
# -----------------------------------------------------------------------------
path = "tools/check_v381_reliability.py"
t = read(path)
if '# C1/C2 remediation invariants.' not in t:
    insert = r'''
# C1/C2 remediation invariants.
require_re(machine, r'\{FaultCode::HighTemperature,\s*FaultSeverity::Stop,.*?false,\s*true,\s*false,\s*true,\s*true,\s*true,\s*"TEMP HIGH"\}', "HighTemperature keeps D14")
require(machine, "firmwareRestartLocked() const", "Machine restart-sensitive state")
for path in ["MAYAP_INDUSTRIAL_v3_4_0/ota_web_update.h", "MAYAP_INDUSTRIAL_v3_4_0/ota_rollback.h", "MAYAP_INDUSTRIAL_v3_4_0/ota_update.h"]:
    require(read(path), "mayapFirmwareRestartLocked()", f"OTA gate {path}")
for forbidden in ["SirenBatteryLow", "BatchLogUnavailable", "BatchLogger", "ATTINY_STATUS_FLAG_9V_LOW", "ATTINY_9V_CONFIRM_MS", "attinySirenBatteryLow"]:
    if forbidden in config or forbidden in machine:
        raise SystemExit(f"FAIL: removed feature returned: {forbidden}")
tiny = read("ATTINY13A_POWER_ALARM/ATTINY13A_POWER_ALARM.ino")
for forbidden in ["PIN_9V", "FLAG_9V_LOW", "nineVoltOk"]:
    if forbidden in tiny:
        raise SystemExit(f"FAIL: ATtiny 9V measurement returned: {forbidden}")
app = read("app.js")
require(app, "autoResumeInput.disabled = autoResumeLocked", "web Auto Resume lock")
for code in [135, 136, 137, 138, 205]:
    require(cloud, f"case {code}:", f"cloud fault E{code}")
hmi = read("MAYAP_INDUSTRIAL_v3_4_0/hmi.h")
require_re(config, r'HMI_FIRMWARE_VERSION\[\]\s*=\s*"3\.8\.1"', "HMI metadata")
require(hmi, "buzzer GPIO2; GPIO41 la ATtiny BUS", "HMI pin metadata")
require(config, "#define MAYAP_DIAGNOSTIC_SERIAL 0", "production diagnostic default")
require(machine, "ALARM mute 1 phut", "alarm mute text")
'''
    t = t.replace('# Security regression tripwires.\n', insert + '\n# Security regression tripwires.\n')
write(path, t)

# -----------------------------------------------------------------------------
# Test artifact: every push to this reliability branch must publish the exact
# ESP32 + ATtiny binaries that passed CI, so hardware testing uses the same SHA.
# -----------------------------------------------------------------------------
path = ".github/workflows/build-firmware.yml"
t = read(path)
t = t.replace("if: github.event_name == 'workflow_dispatch'\n        uses: actions/upload-artifact@v6", "if: github.event_name == 'workflow_dispatch' || github.ref == 'refs/heads/hardening/v3.8.1-reliability'\n        uses: actions/upload-artifact@v6")
write(path, t)

print('FINAL C1/C2 remediation applied')
