from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly 1 match, got {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def regex_once(path: Path, pattern: str, replacement: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    new_text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly 1 regex match, got {count}")
    path.write_text(new_text, encoding="utf-8")


config = ROOT / "MAYAP_INDUSTRIAL_v3_4_0" / "config.h"
cloud = ROOT / "MAYAP_INDUSTRIAL_v3_4_0" / "cloud_alert_link.h"
machine = ROOT / "MAYAP_INDUSTRIAL_v3_4_0" / "machine_control.h"

# Development branch now contains firmware-visible provisioning diagnostics, so
# give it its own version instead of reporting 3.8.0 after flashing.
replace_once(
    config,
    'constexpr char MAYAP_FIRMWARE_VERSION[] = "3.8.0";',
    'constexpr char MAYAP_FIRMWARE_VERSION[] = "3.8.1";',
    "firmware version",
)

# Cloud fault text: SensorFrozen (104) and ATtiny state mismatch (503) existed in
# the controller but were missing from phone notification text.
replace_once(
    cloud,
    '    case 103: return "Cảm biến bất thường (nghi ngờ hỏng)";\n',
    '    case 103: return "Cảm biến bất thường (nghi ngờ hỏng)";\n'
    '    case 104: return "Cảm biến đứng giá khi heater vẫn cấp nhiệt";\n',
    "cloud fault 104",
)
replace_once(
    cloud,
    '    case 502: return "Pin còi 9V sắp hết - hãy thay pin sớm để bảo đảm còi báo khi mất điện";\n',
    '    case 502: return "Pin còi 9V sắp hết - hãy thay pin sớm để bảo đảm còi báo khi mất điện";\n'
    '    case 503: return "Trạng thái mẻ giữa ESP32 và ATtiny chưa đồng bộ";\n',
    "cloud fault 503",
)

# Feed provisioning state to HMI without adding any network work to hmiTask.
replace_once(
    cloud,
    '  if (!TLS_ROOT_CA[0]) {\n'
    '    mayapSerialPrintf(true, "[CLOUD] TLS bi khoa: thieu CA goc tin cay\\n");\n'
    '    return false;\n'
    '  }\n',
    '  if (!TLS_ROOT_CA[0]) {\n'
    '    mayapSetProvisioningState(MayapProvisioningState::TlsError);\n'
    '    mayapSerialPrintf(true, "[CLOUD] TLS bi khoa: thieu CA goc tin cay\\n");\n'
    '    return false;\n'
    '  }\n',
    "TLS provisioning status",
)

replace_once(
    cloud,
    '  snprintf(url, sizeof(url), "https://%s%s", CLOUD_API_HOST, path);\n'
    '  return http.begin(client, url);\n',
    '  snprintf(url, sizeof(url), "https://%s%s", CLOUD_API_HOST, path);\n'
    '  const bool started = http.begin(client, url);\n'
    '  if (!started) mayapSetProvisioningState(MayapProvisioningState::CloudError);\n'
    '  return started;\n',
    "http begin provisioning status",
)

replace_once(
    cloud,
    'inline bool postJson(const char *path, const JsonDocument &doc, const char *logTag,\n'
    '                     String *responseBody = nullptr) {\n',
    'inline bool postJson(const char *path, const JsonDocument &doc, const char *logTag,\n'
    '                     String *responseBody = nullptr, int *responseCode = nullptr) {\n',
    "postJson signature",
)
replace_once(
    cloud,
    '  if (!beginCloudRequest(http, client, path)) {\n'
    '    mayapSerialPrintf(false, "[CLOUD] %s -> http.begin() THAT BAI (URL/TLS)\\n", logTag);\n'
    '    return false;\n'
    '  }\n',
    '  if (!beginCloudRequest(http, client, path)) {\n'
    '    if (responseCode) *responseCode = 0;\n'
    '    mayapSerialPrintf(false, "[CLOUD] %s -> http.begin() THAT BAI (URL/TLS)\\n", logTag);\n'
    '    return false;\n'
    '  }\n',
    "postJson begin failure code",
)
replace_once(
    cloud,
    '  const int code = http.POST(body);\n'
    '  const bool ok = code == 200;\n',
    '  const int code = http.POST(body);\n'
    '  if (responseCode) *responseCode = code;\n'
    '  const bool ok = code == 200;\n',
    "postJson response code",
)

old_send_register = '''inline bool sendRegister() {
  JsonDocument doc;
  doc["device_id"] = mayapDeviceIdText();
  doc["device_key"] = mayapDeviceSecret();
  doc["device_name"] = mayapDeviceIdText();
  String response;
  if (!postJson("/api/device/register", doc, "register", &response)) return false;
  storeProvisioningFromResponse(response);
  return rotateLegacyDeviceKey();
}
'''
new_send_register = '''inline bool sendRegister() {
  mayapSetProvisioningState(MayapProvisioningState::Syncing);
  JsonDocument doc;
  doc["device_id"] = mayapDeviceIdText();
  doc["device_key"] = mayapDeviceSecret();
  doc["device_name"] = mayapDeviceIdText();
  String response;
  int code = 0;
  if (!postJson("/api/device/register", doc, "register", &response, &code)) {
    if (code == 403) {
      mayapSetProvisioningState(MayapProvisioningState::ServerDenied);
    } else if (code == 401) {
      mayapSetProvisioningState(MayapProvisioningState::KeyMismatch);
    } else if (code != 0) {
      mayapSetProvisioningState(MayapProvisioningState::CloudError);
    }
    return false;
  }
  storeProvisioningFromResponse(response);
  return rotateLegacyDeviceKey();
}
'''
replace_once(cloud, old_send_register, new_send_register, "sendRegister diagnostics")

old_reset = '''inline bool sendResetPin() {
  JsonDocument doc;
  doc["device_id"] = mayapDeviceIdText();
  doc["device_key"] = mayapDeviceSecret();
  String response;
  if (!postJson("/api/device/reset-pin", doc, "reset-pin", &response)) return false;
  storeProvisioningFromResponse(response);
  return true;
}
'''
new_reset = '''inline bool sendResetPin() {
  JsonDocument doc;
  doc["device_id"] = mayapDeviceIdText();
  doc["device_key"] = mayapDeviceSecret();
  String response;
  int code = 0;
  if (!postJson("/api/device/reset-pin", doc, "reset-pin", &response, &code)) {
    if (code == 403) {
      mayapSetProvisioningState(MayapProvisioningState::ServerDenied);
    } else if (code == 401) {
      mayapSetProvisioningState(MayapProvisioningState::KeyMismatch);
    } else if (code != 0) {
      mayapSetProvisioningState(MayapProvisioningState::CloudError);
    }
    return false;
  }
  storeProvisioningFromResponse(response);
  return true;
}
'''
replace_once(cloud, old_reset, new_reset, "sendResetPin diagnostics")

old_service_register = '''inline void serviceRegister(uint32_t now) {
  if (registered) return;
  if (!cloudBackoff.ready(now)) return;
  const NetworkStatus status = mayapGetNetworkStatus();
  if (!(status.requestedMode == ConnectivityMode::Online && status.connected)) return;
  if (sendRegister()) {
    registered = true;
    cloudBackoff.onSuccess();
  } else {
    cloudBackoff.onFailure(now);
  }
}
'''
new_service_register = '''inline void serviceRegister(uint32_t now) {
  if (registered) return;
  if (!cloudBackoff.ready(now)) return;
  const NetworkStatus status = mayapGetNetworkStatus();
  if (status.requestedMode != ConnectivityMode::Online) return;
  if (!status.connected) {
    mayapSetProvisioningState(MayapProvisioningState::CloudOffline);
    return;
  }
  if (sendRegister()) {
    registered = true;
    cloudBackoff.onSuccess();
  } else {
    cloudBackoff.onFailure(now);
  }
}
'''
replace_once(cloud, old_service_register, new_service_register, "serviceRegister diagnostics")

# Remove stale safety comments that contradicted the executable descriptor table.
regex_once(
    machine,
    r'    // F-03: dropHeatMaster=false o ca 3 dong duoi day.*?    \{FaultCode::SensorLost',
    '    // v3.8.x: mat/sai/nghi ngo cam bien la STOP fault. Ca SSR va contactor\\n'
    '    // tong nhiet deu bi cat; cong tac HEATER vat ly chi la dieu kien cho\\n'
    '    // phep, khong duoc vuot qua fault. Day la lop software bo sung cho\\n'
    '    // thermostat co khi + cau chi nhiet doc lap ngoai firmware.\\n'
    '    {FaultCode::SensorLost',
    "sensor safety stale comment",
)

regex_once(
    machine,
    r'    // doi trong thoi gian dai du frame van hop le, CRC dung, khong mat tin.*?    \{FaultCode::SensorFrozen',
    '    // SensorFrozen chi active khi PV dung gia du lau VA heater da tich luy\\n'
    '    // ON-time dang ke trong cung cua so. Khi da co bang chung nay, fault\\n'
    '    // STOP cat ca SSR va contactor; o diem dat on dinh voi heater gan nhu\\n'
    '    // khong cap se khong bi false-trip.\\n'
    '    {FaultCode::SensorFrozen',
    "sensor frozen stale comment",
)

replace_once(
    machine,
    '    // Nhiet cao: chi cam SSR, giu contactor tong, bat ca hai quat.\n',
    '    // Nhiet cao: cam SSR, nha contactor tong, bat ca hai quat.\n',
    "high temp stale comment",
)

print("v3.8.1 reliability source patch applied successfully")
