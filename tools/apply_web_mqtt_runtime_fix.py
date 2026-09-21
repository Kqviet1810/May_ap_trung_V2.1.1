from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
app_path = ROOT / "app.js"
check_path = ROOT / "tools/check_v381_reliability.py"
app = app_path.read_text(encoding="utf-8")
check = check_path.read_text(encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"PATCH FAIL {label}: expected 1, found {count}")
    return text.replace(old, new, 1)


app = replace_once(
    app,
    "    localStorage.setItem(MQTT_OVERRIDE_STORAGE, JSON.stringify({ mqttUrl, mqttUsername, mqttPassword }));\n    return true;",
    "    const runtimeMqtt = { mqttUrl, mqttUsername, mqttPassword };\n"
    "    localStorage.setItem(MQTT_OVERRIDE_STORAGE, JSON.stringify(runtimeMqtt));\n"
    "    // Credential duoc cap sau khi WEB da khoi tao. Cap nhat ngay cau hinh\n"
    "    // runtime trong tab hien tai; neu chi ghi vao RAM-storage thi WEB van\n"
    "    // giu mqttUrl/user/pass rong va connectMqtt() se khong bao gio chay.\n"
    "    WEB = Object.freeze({ ...WEB, ...runtimeMqtt });\n"
    "    return true;",
    "refresh WEB runtime after MQTT session",
)

app = replace_once(
    app,
    "    mqttConnected: false,\n    mqttMessage: 'Chưa kết nối MQTT',",
    "    mqttConnected: false,\n    mqttMessage: 'Chưa kết nối MQTT',\n    mqttSessionState: 'idle',",
    "MQTT session state",
)

app = replace_once(
    app,
    "    if (!state.mqttConnected) return 'connecting';",
    "    if (!state.mqttConnected) {\n"
    "      if (state.mqttSessionState === 'error' || state.mqttSessionState === 'auth-required') return 'offline';\n"
    "      return 'connecting';\n"
    "    }",
    "connection status must not spin forever",
)

app = replace_once(
    app,
    "    if (!WEB.mqttUrl || !/^wss?:\\/\\//i.test(WEB.mqttUrl)) {\n      state.mqttMessage = 'Chưa cấu hình MQTT WebSocket';",
    "    if (!WEB.mqttUrl || !/^wss?:\\/\\//i.test(WEB.mqttUrl)) {\n      state.mqttSessionState = 'error';\n      state.mqttMessage = 'Chưa cấu hình MQTT WebSocket';",
    "invalid MQTT URL state",
)

app = replace_once(
    app,
    "    if (!window.mqtt?.connect) {\n      state.mqttMessage = 'Không tải được thư viện MQTT.js';",
    "    if (!window.mqtt?.connect) {\n      state.mqttSessionState = 'error';\n      state.mqttMessage = 'Không tải được thư viện MQTT.js';",
    "missing MQTT.js state",
)

app = replace_once(
    app,
    "    state.mqtt.on('connect', () => {\n      state.mqttConnected = true;",
    "    state.mqtt.on('connect', () => {\n      state.mqttConnected = true;\n      state.mqttSessionState = 'ready';",
    "connected MQTT session state",
)

old_refresh = """  async function refreshMqttSession() {
    const device = currentDevice() || state.devices[0];
    if (!device?.id || !device.pairingToken) return false;
    const result = await postCloudJson('/api/device/mqtt-session', {
      device_id: device.id,
      pairing_token: device.pairingToken
    });
    return Boolean(result.success && saveProvisionedMqtt(result));
  }
"""
new_refresh = """  async function refreshMqttSession() {
    const device = currentDevice() || state.devices[0];
    if (!device?.id || !device.pairingToken) {
      state.mqttSessionState = 'auth-required';
      state.mqttMessage = 'Cần xác thực lại PIN thiết bị';
      return false;
    }
    state.mqttSessionState = 'loading';
    const result = await postCloudJson('/api/device/mqtt-session', {
      device_id: device.id,
      pairing_token: device.pairingToken
    });
    if (result.success && saveProvisionedMqtt(result)) {
      state.mqttSessionState = 'ready';
      return true;
    }
    state.mqttSessionState = 'error';
    state.mqttMessage = result.error || 'Không lấy được phiên MQTT WebSocket';
    return false;
  }
"""
app = replace_once(app, old_refresh, new_refresh, "refreshMqttSession")

app = replace_once(
    app,
    "    await refreshMqttSession();\n    connectMqtt();",
    "    const mqttReady = await refreshMqttSession();\n    if (mqttReady) connectMqtt();\n    else renderDevice();",
    "init MQTT readiness gate",
)

# Regression tripwires: keep this exact class of bug from returning.
if 'app = read("app.js")' not in check:
    check = replace_once(
        check,
        'config = read("MAYAP_INDUSTRIAL_v3_4_0/config.h")\n',
        'config = read("MAYAP_INDUSTRIAL_v3_4_0/config.h")\napp = read("app.js")\n',
        "load app.js in reliability checker",
    )

anchor = '# Provisioning diagnostics must remain visible on the local HMI path.\n'
web_checks = '''# Web MQTT session returned after page boot must update the live WEB object.\nrequire(app, "WEB = Object.freeze({ ...WEB, ...runtimeMqtt });", "web MQTT runtime credential refresh")\nrequire(app, "state.mqttSessionState = 'ready';", "web MQTT session ready state")\nrequire(app, "if (mqttReady) connectMqtt();", "web MQTT init readiness gate")\nrequire(app, "state.mqttSessionState === 'error' || state.mqttSessionState === 'auth-required'", "web MQTT no infinite connecting state")\n\n'''
if web_checks not in check:
    check = replace_once(check, anchor, web_checks + anchor, "web MQTT regression checks")

# Final assertions before writing.
required = [
    "WEB = Object.freeze({ ...WEB, ...runtimeMqtt });",
    "mqttSessionState: 'idle'",
    "state.mqttSessionState = 'loading';",
    "state.mqttSessionState = 'ready';",
    "if (mqttReady) connectMqtt();",
]
for needle in required:
    if needle not in app:
        raise SystemExit(f"PATCH FAIL final assertion missing: {needle}")

app_path.write_text(app, encoding="utf-8")
check_path.write_text(check, encoding="utf-8")
print("web MQTT runtime fix applied")
