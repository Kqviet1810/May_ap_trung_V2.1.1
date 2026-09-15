#!/usr/bin/env python3
"""Kiem tra nhung hop dong de lech giua firmware, web, Worker va tai lieu."""

from __future__ import annotations

import os
import re
import sqlite3
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors: list[str] = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


config_h = read("MAYAP_INDUSTRIAL_v3_4_0/config.h")
app_js = read("app.js")
config_js = read("config.js")
production_js = read("config.production.example.js")
test_js = read("config.test.example.js")
index_html = read("index.html")
sw_js = read("sw.js")
realtime_h = read("MAYAP_INDUSTRIAL_v3_4_0/realtime_link.h")
provisioning_h = read("MAYAP_INDUSTRIAL_v3_4_0/provisioning.h")
release_workflow = read(".github/workflows/release-firmware.yml")

version_match = re.search(r'MAYAP_FIRMWARE_VERSION\[\]\s*=\s*"([^"]+)"', config_h)
require(version_match is not None, "Khong doc duoc MAYAP_FIRMWARE_VERSION")
version = version_match.group(1) if version_match else ""

version_sources = {
    "app.js": re.search(r"appVersion:\s*'([^']+)'", app_js),
    "config.js": re.search(r"appVersion:\s*'([^']+)'", config_js),
    "config.production.example.js": re.search(r"appVersion:\s*'([^']+)'", production_js),
    "config.test.example.js": re.search(r"appVersion:\s*'([^']+)'", test_js),
    "index.html": re.search(r"Điều khiển máy ấp · v([^<]+)", index_html),
    "sw.js": re.search(r"mayap-web-v([^']+)", sw_js),
    "README.md": re.search(r"Máy ấp trứng thông minh v([^\n]+)", read("README.md")),
    "audit/manual/manual.html": re.search(r"MAYAP INDUSTRIAL v([^<]+)</td>", read("audit/manual/manual.html")),
    "cloudflare/package.json": re.search(r'"version":\s*"([^"]+)"', read("cloudflare/package.json")),
}
for source, match in version_sources.items():
    require(match is not None, f"Khong doc duoc version trong {source}")
    if match:
        require(match.group(1) == version, f"Version {source}={match.group(1)} lech firmware={version}")

tag = os.environ.get("GITHUB_REF_NAME", "")
if tag.startswith("v"):
    require(tag[1:] == version, f"Tag {tag} khong khop firmware v{version}")

firmware_protocol = re.search(r"MQTT_PROTOCOL_VERSION\s*=\s*(\d+)U", config_h)
web_protocol = re.search(r"PROTOCOL_VERSION\s*=\s*(\d+)", app_js)
require(firmware_protocol is not None and web_protocol is not None,
        "Khong doc duoc version giao thuc MQTT")
if firmware_protocol and web_protocol:
    require(firmware_protocol.group(1) == web_protocol.group(1),
            "Version giao thuc MQTT firmware/web khong khop")
require('doc["v"] = MQTT_PROTOCOL_VERSION;' in realtime_h,
        "Firmware chua gan version cho payload MQTT")
require("Number(payload.v) !== PROTOCOL_VERSION" in app_js,
        "Web chua tu choi payload sai version")

safe_defaults = {
    "MAYAP_OTA_PASSWORD": "",
    "MAYAP_MQTT_HOST": "",
    "MAYAP_MQTT_USERNAME": "",
    "MAYAP_MQTT_PASSWORD": "",
    "MAYAP_DEVICE_SECRET": "",
    "MAYAP_FACTORY_PIN": "",
}
for macro, expected in safe_defaults.items():
    match = re.search(rf"#define\s+{macro}\s+\"([^\"]*)\"", config_h)
    require(match is not None and match.group(1) == expected,
            f"Mac dinh {macro} phai de trong/fail-closed")
require("broker.emqx.io" not in config_h + config_js + production_js,
        "Broker cong cong xuat hien trong cau hinh mac dinh/production")
require("cdnjs.cloudflare.com/ajax/libs/mqtt" not in index_html,
        "Web khong duoc tai MQTT.js truc tiep tu CDN")
require((ROOT / "vendor/mqtt.min.js").stat().st_size > 100_000,
        "Thieu MQTT.js noi bo hoac file vendor khong hop le")
require("181020" not in config_h, "Con mat khau OTA cu trong config.h")
require("#if MAYAP_MQTT_USE_TLS" in realtime_h,
        "Nhanh TLS MQTT phai dung macro preprocessor MAYAP_MQTT_USE_TLS")
require("mayapMqttHost()" in realtime_h and "mayapMqttPassword()" in realtime_h,
        "MQTT firmware chua doc credential tu provisioning NVS")
require('constexpr char NVS_NAMESPACE[] = "mayap_conn"' in provisioning_h,
        "Thieu namespace NVS provisioning")
require("MAYAP-firmware-${VERSION}.bin" in release_workflow,
        "Workflow release khong tao dung ten asset OTA")
require('ENABLE_PUBLIC_GITHUB_OTA = "1"' in read("cloudflare/wrangler.toml"),
        "OTA secret-free chua duoc bat trong Worker")

enum_match = re.search(r"enum class FaultCode[^\{]*\{(.*?)\};", read("MAYAP_INDUSTRIAL_v3_4_0/machine_control.h"), re.S)
titles_match = re.search(r"const FAULT_TITLES = \{(.*?)\n  \};", app_js, re.S)
require(enum_match is not None and titles_match is not None, "Khong doc duoc bang ma loi")
if enum_match and titles_match:
    firmware_faults = {int(value) for value in re.findall(r"\b\w+\s*=\s*(\d+)", enum_match.group(1)) if value != "0"}
    web_faults = {int(value) for value in re.findall(r"(?:^|[,\n])\s*(\d+)\s*:", titles_match.group(1))}
    require(firmware_faults == web_faults,
            f"Ma loi lech: thieu tren web={sorted(firmware_faults-web_faults)}, du={sorted(web_faults-firmware_faults)}")

shell_match = re.search(r"const APP_SHELL = \[(.*?)\];", sw_js, re.S)
require(shell_match is not None, "Khong doc duoc APP_SHELL")
if shell_match:
    for asset in re.findall(r"'\.\/([^']*)'", shell_match.group(1)):
        if asset:
            require((ROOT / asset).is_file(), f"APP_SHELL tham chieu file khong ton tai: {asset}")

try:
    connection = sqlite3.connect(":memory:")
    connection.executescript(read("cloudflare/schema.sql"))
    connection.close()
except sqlite3.Error as exc:
    errors.append(f"Schema D1/SQLite khong hop le: {exc}")

if errors:
    print("KIEM TRA THAT BAI:", file=sys.stderr)
    for error in errors:
        print(f"- {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"OK: firmware/web v{version}, MQTT protocol v{firmware_protocol.group(1)}, schema va assets dong bo")
