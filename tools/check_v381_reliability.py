from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL: {label}: missing {needle!r}")


def require_re(text: str, pattern: str, label: str) -> None:
    if not re.search(pattern, text, re.MULTILINE | re.DOTALL):
        raise SystemExit(f"FAIL: {label}: pattern not found: {pattern}")


config = read("MAYAP_INDUSTRIAL_v3_4_0/config.h")
app = read("app.js")
identity = read("MAYAP_INDUSTRIAL_v3_4_0/device_identity.h")
cloud = read("MAYAP_INDUSTRIAL_v3_4_0/cloud_alert_link.h")
machine = read("MAYAP_INDUSTRIAL_v3_4_0/machine_control.h")
network = read("MAYAP_INDUSTRIAL_v3_4_0/network_service.h")
ota = read("MAYAP_INDUSTRIAL_v3_4_0/ota_update.h")
ino = read("MAYAP_INDUSTRIAL_v3_4_0/MAYAP_INDUSTRIAL_v3_4_0.ino")
wrangler = read("cloudflare/wrangler.toml")
wrapper = read("cloudflare/src/reliability-wrapper.js")
security = read("cloudflare/src/security-wrapper.js")
safety = read("doc/SAFETY_HARDWARE_REQUIREMENTS.md")
build_workflow = read(".github/workflows/build-firmware.yml")

require_re(config, r'MAYAP_FIRMWARE_VERSION\[\]\s*=\s*"3\.8\.1"', "firmware version")

# MQTT deploy image must fail at compile time if broker credentials are absent.
require(config, "static_assert(sizeof(MQTT_BROKER_HOST) > 1U", "MQTT host compile guard")
require(config, "static_assert(sizeof(MQTT_USERNAME) > 1U", "MQTT username compile guard")
require(config, "static_assert(sizeof(MQTT_PASSWORD) > 1U", "MQTT password compile guard")
require(build_workflow, 'username = "__ci_pr_mqtt_user__"', "PR MQTT compile placeholder")
require(build_workflow, 'password = "__ci_pr_mqtt_password__"', "PR MQTT password placeholder")
require(build_workflow, 'Thieu GitHub Secrets: MAYAP_MQTT_USERNAME/MAYAP_MQTT_PASSWORD', "deploy MQTT secrets gate")
require(build_workflow, "startsWith(github.ref, 'refs/heads/hardening/')", "hardening test artifact")
require(build_workflow, "firmware-test-${{ github.sha }}", "test artifact tied to commit SHA")

# Web MQTT session returned after page boot must update the live WEB object.
require(app, "WEB = Object.freeze({ ...WEB, ...runtimeMqtt });", "web MQTT runtime credential refresh")
require(app, "state.mqttSessionState = 'ready';", "web MQTT session ready state")
require(app, "if (mqttReady) connectMqtt();", "web MQTT init readiness gate")
require(app, "state.mqttSessionState === 'error' || state.mqttSessionState === 'auth-required'", "web MQTT no infinite connecting state")


# Wi-Fi portal must quiesce cross-task network I/O before changing radio mode.
require(network, "PortalPhase::Quiescing", "Wi-Fi portal quiescing phase")
require(network, "WiFi.disconnect(false, false)", "portal disconnect keeps radio alive")
require(network, "portalOtaQuiescedFlag", "portal/OTA quiesce handshake")
require(network, "portalPhase != PortalPhase::Quiescing", "cancel during quiesce must not touch radio")
require(network, "[PORTAL-PANIC] stage=", "portal panic RTC breadcrumb")
require(ota, "mayapOtaQuiesceForWifiPortal", "ArduinoOTA portal quiesce helper")
require(ota, "if (MayapOtaInternal::inProgress)", "do not abort active ArduinoOTA")
require(ino, "mayapWifiPortalExclusiveRequested()", "otaTask portal exclusion")
require(ino, "mayapSetWifiPortalOtaQuiesced(quiesced)", "otaTask quiesce acknowledgement")

# Provisioning diagnostics must remain visible on the local HMI path.
for state in ["CloudOffline", "TlsError", "ServerDenied", "KeyMismatch", "CloudError"]:
    require(identity, f"MayapProvisioningState::{state}", f"provisioning state {state}")
require(identity, "mayapProvisioningStateText()", "provisioning state text")
require(identity, "mayapProvisioningStateText();", "HMI PIN fallback uses provisioning state")

# Cloud must classify provisioning failures rather than looping forever as 'syncing'.
require(cloud, "int *responseCode = nullptr", "HTTP response code propagation")
require(cloud, "MayapProvisioningState::ServerDenied", "HTTP 403 classification")
require(cloud, "MayapProvisioningState::KeyMismatch", "HTTP 401 classification")
require(cloud, "MayapProvisioningState::CloudOffline", "offline classification")

# Current-scale policy: auto provisioning is enabled, but inventory remains available.
require(wrangler, 'main = "src/reliability-wrapper.js"', "reliability worker entrypoint")
require(wrangler, 'REQUIRE_DEVICE_INVENTORY = "0"', "auto provisioning default")
require(wrangler, "MAX_NEW_DEVICE_REGISTRATIONS_PER_HOUR", "registration rate limit setting")
require(wrangler, "NEW_DEVICE_REGISTRATION_WINDOW_MINUTES", "registration window setting")
require(wrapper, "strictInventoryRequired", "optional strict inventory mode")
require(wrapper, "registrationWindowMs", "configurable registration window")
require(wrapper, "recordNewDeviceAdmission", "new-device rate accounting")
require(wrapper, "INSERT OR IGNORE INTO device_inventory", "auto-admit inventory record")
require(wrapper, "handleResetPin", "reset-pin recovery path")
require(wrapper, "provisioned: true", "reset-pin provisioning result")

# Do not weaken the original device-key gate: reliability-wrapper must feed through it.
require(security, "handleSecureRegister", "security register gate")
require(wrapper, "return worker.fetch(request, env, ctx);", "delegate to security wrapper")

# Safety consistency: SensorFrozen requires actual heater-on evidence and remains fail-safe.
require(machine, "heaterStuckAccumOnMs_ >= frozenEvidenceOnMs", "SensorFrozen heater evidence")
require_re(
    machine,
    r'\{FaultCode::SensorFrozen,\s*FaultSeverity::Stop,.*?true,\s*true,\s*true,\s*false,\s*true,\s*true,\s*"SENSOR FROZEN"\}',
    "SensorFrozen descriptor",
)
require(safety, "fault ở mức STOP và cắt cả SSR lẫn contactor nhiệt", "SensorFrozen safety documentation")

# Security regression tripwires.
firmware_text = "\n".join(
    p.read_text(encoding="utf-8", errors="ignore")
    for p in (ROOT / "MAYAP_INDUSTRIAL_v3_4_0").glob("*")
    if p.suffix in {".h", ".ino"}
)
if "setInsecure()" in firmware_text:
    raise SystemExit("FAIL: setInsecure() reintroduced")
if "BEGIN PRIVATE KEY" in firmware_text or "BEGIN EC PRIVATE KEY" in firmware_text:
    raise SystemExit("FAIL: private signing key found in firmware tree")

print("v3.8.1 reliability regression checks: OK")
