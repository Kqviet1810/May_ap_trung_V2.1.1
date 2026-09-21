from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patch(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"FAIL: {path}: expected exactly 1 match, got {count}: {old!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


config = "MAYAP_INDUSTRIAL_v3_4_0/config.h"
hmi = "MAYAP_INDUSTRIAL_v3_4_0/hmi.h"
network = "MAYAP_INDUSTRIAL_v3_4_0/network_service.h"
checker = "tools/check_v381_reliability.py"

patch(
    config,
    "constexpr uint32_t MENU_IDLE_TIMEOUT_MS = 60000UL;",
    "constexpr uint32_t MENU_IDLE_TIMEOUT_MS = 60000UL;\n"
    "// Rieng man DOI WIFI can du 2 phut de nguoi dung ket noi AP va nhap mat khau.\n"
    "constexpr uint32_t WIFI_PORTAL_UI_IDLE_TIMEOUT_MS = 120000UL;",
)

patch(
    config,
    "// Cong 1 doi Wi-Fi: mo AP toi da 5 phut cho nguoi dung nhap SSID/mat khau moi,\n"
    "// sau do tu dong dong portal va quay lai ket noi binh thuong.\n"
    "constexpr uint32_t WIFI_PORTAL_MAX_OPEN_MS = 300000UL;",
    "// Cong 1 doi Wi-Fi: mo AP toi da 2 phut cho nguoi dung nhap SSID/mat khau moi,\n"
    "// sau do tu dong dong portal va quay lai ket noi binh thuong.\n"
    "constexpr uint32_t WIFI_PORTAL_MAX_OPEN_MS = 120000UL;",
)

patch(
    hmi,
    "      now - lastInteractionAt >= MENU_IDLE_TIMEOUT_MS) {",
    "      now - lastInteractionAt >=\n"
    "          ((view == View::WifiChange) ? WIFI_PORTAL_UI_IDLE_TIMEOUT_MS\n"
    "                                      : MENU_IDLE_TIMEOUT_MS)) {",
)

patch(
    network,
    '    "box-shadow:0 0 0 3px rgba(66,170,156,.15)}"\n'
    '    "button,a.reload{width:100%;height:48px;margin-top:18px;border:0;"',
    '    "box-shadow:0 0 0 3px rgba(66,170,156,.15)}"\n'
    '    ".showPass{display:flex;align-items:center;gap:8px;margin:10px 2px 0;"\n'
    '    "text-transform:none;letter-spacing:0;font-size:13px;color:#496963;cursor:pointer}"\n'
    '    ".showPass input{width:18px;height:18px;margin:0;padding:0;flex:none;box-shadow:none}"\n'
    '    "button,a.reload{width:100%;height:48px;margin-top:18px;border:0;"',
)

patch(
    network,
    '    "</select><label>Mat khau</label>"\n'
    '    "<input name=password type=password maxlength=64 autocomplete=off>"\n'
    '    "<button type=submit>Luu &amp; ket noi</button></form>"',
    '    "</select><label>Mat khau</label>"\n'
    '    "<input id=wifiPassword name=password type=password maxlength=64 autocomplete=off>"\n'
    '    "<label class=showPass><input id=showPassword type=checkbox "\n'
    '    "onchange=\\\"document.getElementById(\\\'wifiPassword\\\').type=this.checked?\\\'text\\\':\\\'password\\\'\\\">"\n'
    '    "<span>Hien mat khau</span></label>"\n'
    '    "<button type=submit>Luu &amp; ket noi</button></form>"',
)

patch(
    checker,
    'network = read("MAYAP_INDUSTRIAL_v3_4_0/network_service.h")\n',
    'network = read("MAYAP_INDUSTRIAL_v3_4_0/network_service.h")\n'
    'hmi = read("MAYAP_INDUSTRIAL_v3_4_0/hmi.h")\n',
)

patch(
    checker,
    'require(ino, "mayapSetWifiPortalOtaQuiesced(quiesced)", "otaTask quiesce acknowledgement")\n',
    'require(ino, "mayapSetWifiPortalOtaQuiesced(quiesced)", "otaTask quiesce acknowledgement")\n'
    'require(config, "WIFI_PORTAL_MAX_OPEN_MS = 120000UL", "Wi-Fi portal 2 minute network timeout")\n'
    'require(config, "WIFI_PORTAL_UI_IDLE_TIMEOUT_MS = 120000UL", "Wi-Fi portal 2 minute HMI timeout")\n'
    'require(hmi, "(view == View::WifiChange) ? WIFI_PORTAL_UI_IDLE_TIMEOUT_MS", "Wi-Fi screen uses dedicated timeout")\n'
    'require(network, "id=showPassword", "Wi-Fi portal show-password control")\n'
    'require(network, "this.checked?\\\'text\\\':\\\'password\\\'", "Wi-Fi portal password visibility toggle")\n',
)

print("Wi-Fi portal 2-minute + show-password patch applied")
