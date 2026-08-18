/*
  MAYAP - CHI PHAN KET NOI WIFI BANG WEB PHU
  ESP32 / Arduino core

  Chuc nang:
  - Doc SSID/password da luu trong Preferences.
  - Thu ket noi Wi-Fi da luu.
  - Neu chua ket noi: mo AP MAYAP-XXXX.
  - Captive portal/WebServer tai http://192.168.4.1
  - Quet Wi-Fi, nhap mat khau, luu flash, thu ket noi lai.
  - Giu nut BOOT (GPIO0) 3 giay -> xoa Wi-Fi; tha nut -> mo che do cau hinh.

  Khong co MQTT, HMI, machine control hay WebAdapter.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

namespace MayapWifiPortal {

static constexpr uint16_t DNS_PORT = 53;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000UL;
static constexpr uint8_t BOOT_BUTTON_PIN = 0;
static constexpr uint32_t BOOT_HOLD_MS = 3000UL;

static WebServer server(80);
static DNSServer dnsServer;
static Preferences prefs;

static bool portalRunning = false;
static uint32_t lastRetryAt = 0;
static uint32_t bootPressedAt = 0;
static bool bootResetHandled = false;

static String savedSsid;
static String savedPassword;
static String apName;

static void startPortal();

static void clearSavedWifi() {
  prefs.clear();
  savedSsid = "";
  savedPassword = "";
}

static void clearNetworkAndOpenPortal() {
  clearSavedWifi();
  WiFi.disconnect(true, true);
  delay(50);
  startPortal();
}

static void serviceBootButton() {
  const bool pressed = digitalRead(BOOT_BUTTON_PIN) == LOW;

  if (pressed) {
    if (bootPressedAt == 0) bootPressedAt = millis();

    if (!bootResetHandled &&
        static_cast<uint32_t>(millis() - bootPressedAt) >= BOOT_HOLD_MS) {
      bootResetHandled = true;
      clearSavedWifi();
      Serial.println(F("[BOOT] Da xoa Wi-Fi; tha nut de mo portal"));
    }
    return;
  }

  if (bootResetHandled) {
    bootResetHandled = false;
    bootPressedAt = 0;
    clearNetworkAndOpenPortal();
    return;
  }

  bootPressedAt = 0;
}

static String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); ++i) {
    switch (s[i]) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += s[i]; break;
    }
  }
  return out;
}

static String makeApName() {
  const uint64_t chip = ESP.getEfuseMac();
  char name[20];
  snprintf(name, sizeof(name), "MAYAP-%04X",
           static_cast<unsigned>((chip >> 32U) & 0xFFFFU));
  return String(name);
}

static bool connectSavedWifi(uint32_t timeoutMs) {
  if (savedSsid.isEmpty()) return false;

  Serial.printf("[WiFi] Dang ket noi: %s\n", savedSsid.c_str());
  WiFi.mode(portalRunning ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         static_cast<uint32_t>(millis() - startedAt) < timeoutMs) {
    serviceBootButton();
    if (portalRunning || savedSsid.isEmpty()) return false;
    delay(20);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Da ket noi, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  WiFi.disconnect(false, false);
  Serial.println(F("[WiFi] Ket noi that bai."));
  return false;
}

static String buildWifiOptions() {
  String options;
  const int count = WiFi.scanNetworks(false, true);

  if (count <= 0) {
    return F("<option value=''>Khong tim thay Wi-Fi</option>");
  }

  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;

    options += F("<option value=\"");
    options += htmlEscape(ssid);
    options += F("\">");
    options += htmlEscape(ssid);
    options += F("  (");
    options += String(WiFi.RSSI(i));
    options += F(" dBm)");
    if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) options += F(" - Mo");
    options += F("</option>");
  }

  WiFi.scanDelete();
  return options;
}

static void sendPortalPage(const String &message = String()) {
  const String options = buildWifiOptions();

  String html;
  html.reserve(12000);
  html += F(R"rawliteral(
<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b1220">
<title>MAYAP • Wi-Fi Setup</title>
<style>
:root{
  --bg:#0b1220;--panel:#111a2e;--panel2:#16213a;--line:#263553;
  --text:#eef4ff;--muted:#9eabc2;--accent:#4f8cff;--accent2:#78a9ff;
  --ok:#39d98a;--danger:#ff6b6b;--shadow:0 24px 70px rgba(0,0,0,.38);
}
*{box-sizing:border-box}
body{
  margin:0;min-height:100vh;font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif;
  color:var(--text);background:
  radial-gradient(circle at 15% 10%,rgba(79,140,255,.18),transparent 30%),
  radial-gradient(circle at 90% 90%,rgba(57,217,138,.08),transparent 28%),var(--bg);
}
.wrap{width:min(760px,100%);margin:auto;padding:28px 16px 42px}
.header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}
.brand{display:flex;align-items:center;gap:12px}
.logo{
  width:46px;height:46px;border-radius:14px;display:grid;place-items:center;
  background:linear-gradient(135deg,var(--accent),#315bc2);box-shadow:0 10px 28px rgba(79,140,255,.28);
  font-weight:800;font-size:19px;letter-spacing:-1px
}
.brand b{font-size:18px;letter-spacing:.2px}.brand span{display:block;color:var(--muted);font-size:12px;margin-top:2px}
.badge{display:flex;align-items:center;gap:7px;padding:8px 11px;border:1px solid var(--line);border-radius:999px;background:rgba(17,26,46,.7);font-size:12px;color:#cbd6ea}
.dot{width:7px;height:7px;border-radius:50%;background:var(--ok);box-shadow:0 0 0 4px rgba(57,217,138,.10)}
.card{background:rgba(17,26,46,.92);border:1px solid var(--line);border-radius:22px;box-shadow:var(--shadow);overflow:hidden}
.hero{padding:28px 28px 22px;background:linear-gradient(180deg,rgba(255,255,255,.025),transparent)}
h1{margin:0 0 9px;font-size:28px;line-height:1.15;letter-spacing:-.7px}
.lead{margin:0;color:var(--muted);line-height:1.55;font-size:14px;max-width:610px}
.form{padding:24px 28px 28px}
.field{margin-bottom:18px}
label{display:flex;align-items:center;justify-content:space-between;margin:0 0 8px;font-size:13px;font-weight:700;color:#dbe5f7}
.hint{font-size:11px;color:var(--muted);font-weight:500}
.selectwrap{position:relative}
select,input{
  width:100%;height:52px;border:1px solid var(--line);border-radius:13px;outline:none;
  background:var(--panel2);color:var(--text);padding:0 15px;font-size:15px;transition:.18s;
}
select{appearance:none;padding-right:44px}
.selectwrap:after{content:"⌄";position:absolute;right:16px;top:13px;color:var(--muted);font-size:19px;pointer-events:none}
input::placeholder{color:#66748d}
select:focus,input:focus{border-color:var(--accent);box-shadow:0 0 0 4px rgba(79,140,255,.12)}
.pass{position:relative}.pass input{padding-right:52px}.eye{position:absolute;right:8px;top:7px;width:38px;height:38px;border:0;background:transparent;color:var(--muted);cursor:pointer;font-size:17px}
.actions{display:flex;gap:10px;align-items:center;margin-top:8px}
button.primary{
  flex:1;height:52px;border:0;border-radius:13px;cursor:pointer;color:white;font-weight:800;font-size:14px;
  background:linear-gradient(135deg,var(--accent),#376bd5);box-shadow:0 12px 28px rgba(79,140,255,.22);transition:.18s
}
button.primary:hover{transform:translateY(-1px);filter:brightness(1.06)}
button.refresh{height:52px;padding:0 17px;border:1px solid var(--line);border-radius:13px;background:var(--panel2);color:#d8e2f4;font-weight:700;cursor:pointer}
.info{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:20px}
.info div{padding:13px 14px;border:1px solid var(--line);border-radius:13px;background:rgba(255,255,255,.018)}
.info small{display:block;color:var(--muted);font-size:10px;text-transform:uppercase;letter-spacing:.8px;margin-bottom:4px}
.info strong{font-size:12px;word-break:break-all}
.message{margin:0 0 18px;padding:12px 14px;border-radius:12px;background:rgba(57,217,138,.08);border:1px solid rgba(57,217,138,.25);color:#bdf3d8;font-size:13px}
.steps{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:14px}
.step{padding:15px;border:1px solid var(--line);border-radius:14px;background:rgba(17,26,46,.65)}
.num{width:25px;height:25px;border-radius:8px;display:grid;place-items:center;background:rgba(79,140,255,.15);color:var(--accent2);font-size:11px;font-weight:800;margin-bottom:9px}
.step b{font-size:12px}.step p{margin:5px 0 0;color:var(--muted);font-size:11px;line-height:1.45}
.footer{text-align:center;color:#68768f;font-size:10px;margin-top:16px}
@media(max-width:560px){
  .wrap{padding:18px 12px 30px}.hero,.form{padding-left:20px;padding-right:20px}
  h1{font-size:24px}.badge{display:none}.info,.steps{grid-template-columns:1fr}
  .actions{flex-direction:column}.actions button{width:100%}
}
</style>
</head>
<body>
<div class="wrap">
  <div class="header">
    <div class="brand">
      <div class="logo">M</div>
      <div><b>MAYAP</b><span>Industrial Wi-Fi Setup</span></div>
    </div>
    <div class="badge"><i class="dot"></i> Configuration mode</div>
  </div>

  <main class="card">
    <section class="hero">
      <h1>Kết nối thiết bị với Wi-Fi</h1>
      <p class="lead">Chọn mạng Wi-Fi mà thiết bị sẽ sử dụng, nhập mật khẩu và nhấn <b>Lưu &amp; kết nối</b>. Cấu hình được lưu trên bộ nhớ thiết bị.</p>
    </section>

    <section class="form">
)rawliteral");

  if (!message.isEmpty()) {
    html += F("<div class='message'>✓ ");
    html += htmlEscape(message);
    html += F("</div>");
  }

  html += F(R"rawliteral(
      <form method="POST" action="/save">
        <div class="field">
          <label><span>Mạng Wi-Fi</span><span class="hint">SSID</span></label>
          <div class="selectwrap">
            <select name="ssid" required>
)rawliteral");
  html += options;
  html += F(R"rawliteral(
            </select>
          </div>
        </div>

        <div class="field">
          <label><span>Mật khẩu</span><span class="hint">Tối đa 64 ký tự</span></label>
          <div class="pass">
            <input id="password" name="password" type="password" maxlength="64" autocomplete="off" placeholder="Nhập mật khẩu Wi-Fi">
            <button class="eye" type="button" onclick="togglePass()" aria-label="Hiện mật khẩu">◉</button>
          </div>
        </div>

        <div class="actions">
          <button class="refresh" type="button" onclick="location.reload()">↻ Quét lại</button>
          <button class="primary" type="submit">Lưu &amp; kết nối →</button>
        </div>
      </form>

      <div class="info">
        <div><small>Thiết bị</small><strong>)rawliteral");
  html += htmlEscape(apName);
  html += F(R"rawliteral(</strong></div>
        <div><small>Địa chỉ cấu hình</small><strong>192.168.4.1</strong></div>
      </div>
    </section>
  </main>

  <div class="steps">
    <div class="step"><div class="num">01</div><b>Chọn mạng</b><p>Quét và chọn SSID Wi-Fi gần thiết bị.</p></div>
    <div class="step"><div class="num">02</div><b>Nhập mật khẩu</b><p>Thông tin sẽ được lưu cục bộ trên ESP32.</p></div>
    <div class="step"><div class="num">03</div><b>Kết nối</b><p>Thiết bị tự động thử kết nối với mạng mới.</p></div>
  </div>
  <div class="footer">MAYAP • Local configuration portal • Không cần Internet</div>
</div>
<script>
function togglePass(){
  const x=document.getElementById('password');
  x.type=x.type==='password'?'text':'password';
}
</script>
</body>
</html>
)rawliteral");

  server.send(200, "text/html; charset=utf-8", html);
}

static void handleRoot() {
  sendPortalPage();
}

static void handleSave() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain; charset=utf-8", "Thieu SSID");
    return;
  }

  const String newSsid = server.arg("ssid");
  const String newPassword = server.arg("password");

  if (newSsid.isEmpty() || newSsid.length() > 32 || newPassword.length() > 64) {
    server.send(400, "text/plain; charset=utf-8", "SSID/password khong hop le");
    return;
  }

  savedSsid = newSsid;
  savedPassword = newPassword;

  prefs.putString("ssid", savedSsid);
  prefs.putString("pass", savedPassword);

  server.send(200, "text/html; charset=utf-8",
              "<html><meta charset='utf-8'><body>"
              "Da luu Wi-Fi. ESP32 dang thu ket noi..."
              "<script>setTimeout(()=>location.href='/',5000)</script>"
              "</body></html>");

  delay(150);
  WiFi.disconnect(false, false);
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
  lastRetryAt = millis();
}

static void handleStatus() {
  String json = F("{\"connected\":");
  json += (WiFi.status() == WL_CONNECTED) ? F("true") : F("false");
  json += F(",\"ssid\":\"");
  json += htmlEscape(WiFi.SSID());
  json += F("\",\"ip\":\"");
  json += WiFi.localIP().toString();
  json += F("\"}");
  server.send(200, "application/json", json);
}

static void handleNotFound() {
  // Captive portal: moi URL deu quay ve trang cau hinh.
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

static void startPortal() {
  if (portalRunning) return;

  portalRunning = true;
  apName = makeApName();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                    IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));

  // Giong luong goc: AP MAYAP-XXXX. De trong password => AP mo.
  WiFi.softAP(apName.c_str());

  dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("[WiFi] Mo web phu: %s\n", apName.c_str());
  Serial.println(F("[WiFi] Mo trinh duyet: http://192.168.4.1"));
}

static void begin() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  prefs.begin("mayapwifi", false);
  savedSsid = prefs.getString("ssid", "");
  savedPassword = prefs.getString("pass", "");

  if (!connectSavedWifi(WIFI_CONNECT_TIMEOUT_MS)) {
    startPortal();
  }
}

static void loop() {
  serviceBootButton();

  if (portalRunning) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  // Neu dang co portal va da ket noi Wi-Fi thi van giu portal de co the xem/truy cap.
  // Neu mat Wi-Fi, thu ket noi lai theo chu ky, khong block loop chinh.
  if (!savedSsid.isEmpty() && WiFi.status() != WL_CONNECTED &&
      static_cast<uint32_t>(millis() - lastRetryAt) >= WIFI_RETRY_INTERVAL_MS) {
    lastRetryAt = millis();
    WiFi.disconnect(false, false);
    WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
  }
}

}  // namespace MayapWifiPortal

void setup() {
  Serial.begin(115200);
  MayapWifiPortal::begin();
}

void loop() {
  MayapWifiPortal::loop();

  // Dat phan chuong trinh chinh cua may tai day.
  // Khong dung delay dai de web phu phan hoi tot.
  delay(2);
}
