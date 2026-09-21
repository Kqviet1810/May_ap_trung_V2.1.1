from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"RELIABILITY CONTRACT FAIL: {message}")


wrangler = read("cloudflare/wrangler.toml")
wrapper = read("cloudflare/src/reliability-wrapper.js")
security = read("cloudflare/src/security-wrapper.js")
identity = read("MAYAP_INDUSTRIAL_v3_4_0/device_identity.h")
cloud = read("MAYAP_INDUSTRIAL_v3_4_0/cloud_alert_link.h")
config = read("MAYAP_INDUSTRIAL_v3_4_0/config.h")
safety = read("doc/SAFETY_HARDWARE_REQUIREMENTS.md")

require('main = "src/reliability-wrapper.js"' in wrangler,
        "Wrangler must enter through reliability-wrapper.js")
require('REQUIRE_DEVICE_INVENTORY = "0"' in wrangler,
        "small-fleet default must allow controlled auto provisioning")
require('MAX_NEW_DEVICE_REGISTRATIONS_PER_HOUR' in wrangler,
        "new-device rate limit must be configured")

require("import worker from './security-wrapper.js'" in wrapper,
        "reliability wrapper must preserve the existing security wrapper")
require("INSERT OR IGNORE INTO device_inventory" in wrapper,
        "auto provisioning must flow through device_inventory")
require("enabled || 0) !== 1" in wrapper,
        "explicitly disabled inventory records must remain blocked")
require("registerRateKey" in wrapper and "auth_rate_limits" in wrapper,
        "auto provisioning must be rate limited")
require("handleResetPin" in wrapper and "provisioned: true" in wrapper,
        "physical reset PIN must self-heal a missing cloud device record")

require("handleSecureRegister" in security,
        "security wrapper registration validation must remain present")
require("verifyBrowserSession" in security,
        "browser session validation must remain present")

for state in ("CloudOffline", "TlsError", "ServerDenied", "KeyMismatch", "CloudError"):
    require(state in identity, f"HMI provisioning state {state} is missing")
require('return "SERVER 403"' in identity,
        "HMI must expose server provisioning denial")
require('return "KEY ERROR"' in identity,
        "HMI must expose device-key mismatch")

require("MayapProvisioningState::CloudOffline" in cloud,
        "cloud link must publish offline provisioning state")
require("MayapProvisioningState::TlsError" in cloud,
        "cloud link must publish TLS provisioning state")
require("MayapProvisioningState::ServerDenied" in cloud,
        "cloud link must publish HTTP 403 provisioning state")
require("MayapProvisioningState::KeyMismatch" in cloud,
        "cloud link must publish HTTP 401 provisioning state")
require('case 104: return "Cảm biến đứng giá khi heater vẫn cấp nhiệt";' in cloud,
        "SensorFrozen must have a cloud fault message")
require('case 503: return "Trạng thái mẻ giữa ESP32 và ATtiny chưa đồng bộ";' in cloud,
        "ATtiny state mismatch must have a cloud fault message")

require('MAYAP_FIRMWARE_VERSION[] = "3.8.1"' in config,
        "firmware version must identify reliability build")
require("Sensor Frozen" in safety and "cắt cả SSR lẫn contactor" in safety,
        "safety documentation must match SensorFrozen executable behavior")

# The current product profile intentionally requires authenticated TLS; never
# allow a convenience debug change to silently weaken it.
firmware_tree = "\n".join(
    p.read_text(encoding="utf-8", errors="ignore")
    for p in (ROOT / "MAYAP_INDUSTRIAL_v3_4_0").iterdir()
    if p.suffix in {".h", ".ino"}
)
require("setInsecure()" not in firmware_tree,
        "firmware must not disable TLS certificate verification")
require(not re.search(r"BEGIN(?: RSA| EC)? PRIVATE KEY", firmware_tree),
        "private signing keys must not be embedded in firmware")

print("Reliability contract OK")
