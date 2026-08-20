#pragma once

#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <stdint.h>
#include <ctype.h>

// Wi-Fi duoc cach ly khoi task dieu khien. File nay chi duoc goi boi
// networkTask (tru mayapSetConnectivityMode/mayapGetNetworkStatus/
// mayapRequestWifiPortal/mayapCancelWifiPortal/mayapGetWifiPortalStatus,
// nhung tat ca deu dung atomic/co doc snapshot nen goi tu task nao cung an toan).
//
// Nguon goc cong 1 "Doi Wi-Fi": da gop toan bo logic tu sketch tham khao rieng
// MAYAP_WIFI_WEB_PHU_ONLY (AP MAYAP-XXXX + web cau hinh SSID/mat khau) thang
// vao day va chuyen tu kieu blocking (delay() trong vong doi ket noi) sang
// non-blocking de khong bao gio lam cham task dieu khien/HMI. Sketch tham
// khao rieng khong con ton tai trong repo; toan bo firmware gio chi gom
// 4 file .h (config/network_service/hmi/machine_control) + 1 file .ino.
namespace MayapNetworkInternal {

// Ban rieng cua file nay: machine_control.h co elapsedMs/timeReached trong
// namespace Mayap, hmi.h co ban global rieng, nhung network_service.h duoc
// include TRUOC ca hai trong .ino nen khong the dua vao chung - moi file
// tu chua ham nho nay de doc lap thu tu include.
inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return static_cast<uint32_t>(now - then);
}
inline bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

constexpr uint16_t DNS_PORT = 53;

static volatile uint8_t requestedMode =
    static_cast<uint8_t>(ConnectivityMode::Offline);
static volatile uint8_t publishedState =
    static_cast<uint8_t>(NetworkStateCode::Offline);
static volatile bool publishedConfigured = false;
static volatile bool publishedConnected = false;
static volatile int8_t publishedRssiDbm = -127;

static bool radioActive = false;
static uint32_t connectionStartedAt = 0U;
// Backoff RIENG cua STA Wi-Fi, doc lap voi backoff cua MQTT (realtime_link.h)
// va Telegram (telegram_link.h) - loi/reset o tang nao khong dung cham tang
// khac. Khong con dung 2 bien lastRetryAt/lastStartAttemptAt + hang so co
// dinh nhu truoc: moi that bai lien tiep se tu keo gian khoang cho ra thay vi
// dap WiFi.begin() moi 30s vinh vien khi mat mang keo dai.
static BackoffTimer staBackoff{};

// ------------------------- Thong tin dang nhap Wi-Fi ------------------------
// Doc/ghi tu networkTask. SSID/mat khau nap tu NVS (Preferences); neu chua
// tung luu, dung macro bien dich MAYAP_WIFI_SSID/PASSWORD lam gia tri mac dinh
// mot lan duy nhat de tuong thich firmware cu.
static Preferences wifiPrefs;
static char activeSsid[WIFI_PORTAL_SSID_MAX + 1U] = "";
static char activePassword[WIFI_PORTAL_PASSWORD_MAX + 1U] = "";
static bool credentialsLoaded = false;

inline bool credentialsConfigured() { return activeSsid[0] != '\0'; }

inline void loadCredentialsOnce() {
  if (credentialsLoaded) return;
  credentialsLoaded = true;
  wifiPrefs.begin("mayapwifi", false);
  String ssid = wifiPrefs.getString("ssid", "");
  String pass = wifiPrefs.getString("pass", "");
  if (ssid.isEmpty() && sizeof(NETWORK_WIFI_SSID) > 1U) {
    // Chua tung cau hinh qua HMI/portal: mo phong gia tri build-time mot lan,
    // luu lai de cac lan sau doc thang tu NVS.
    ssid = NETWORK_WIFI_SSID;
    pass = NETWORK_WIFI_PASSWORD;
    wifiPrefs.putString("ssid", ssid);
    wifiPrefs.putString("pass", pass);
  }
  snprintf(activeSsid, sizeof(activeSsid), "%s", ssid.c_str());
  snprintf(activePassword, sizeof(activePassword), "%s", pass.c_str());
}

inline bool saveCredentials(const char *ssid, const char *password) {
  if (!ssid || !ssid[0]) return false;
  if (strlen(ssid) > WIFI_PORTAL_SSID_MAX ||
      strlen(password ? password : "") > WIFI_PORTAL_PASSWORD_MAX) {
    return false;
  }
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", password ? password : "");
  snprintf(activeSsid, sizeof(activeSsid), "%s", ssid);
  snprintf(activePassword, sizeof(activePassword), "%s", password ? password : "");
  return true;
}

// --------------------- Telegram Chat ID (rieng WiFi, doc lap) -----------------
// Chi la CAU HINH (chuoi Chat ID nguoi dung tu lay tu Telegram va nhap qua
// trang web phu). Viec gui tin nhan qua Telegram hoan toan do telegram_link.h
// tu dam nhiem qua HTTPS rieng - file nay chi luu/doc gia tri, khong biet gi
// ve HTTP/Telegram API.
static Preferences telegramPrefs;
static char telegramChatId[TELEGRAM_CHAT_ID_MAX + 1U] = "";
static bool telegramChatIdLoaded = false;

inline void loadTelegramChatIdOnce() {
  if (telegramChatIdLoaded) return;
  telegramChatIdLoaded = true;
  telegramPrefs.begin("mayaptg", false);
  const String id = telegramPrefs.getString("chatid", "");
  snprintf(telegramChatId, sizeof(telegramChatId), "%s", id.c_str());
}

// Chat ID Telegram la so nguyen, co the am (nhom/kenh dang -100xxxxxxxxxxxx).
inline bool isValidTelegramChatId(const char *id) {
  if (!id || !id[0]) return false;
  size_t len = strlen(id);
  if (len > TELEGRAM_CHAT_ID_MAX) return false;
  size_t i = 0;
  if (id[0] == '-') {
    if (len < 2U) return false;
    i = 1U;
  }
  for (; i < len; ++i) {
    if (!isdigit(static_cast<unsigned char>(id[i]))) return false;
  }
  return true;
}

// Rong = tat thong bao Telegram cho may nay (chua cau hinh). Tra ve false chi
// khi chuoi khong rong nhung sai dinh dang.
inline bool saveTelegramChatId(const char *id) {
  loadTelegramChatIdOnce();
  const bool clearing = !id || !id[0];
  if (!clearing && !isValidTelegramChatId(id)) return false;
  telegramPrefs.putString("chatid", clearing ? "" : id);
  snprintf(telegramChatId, sizeof(telegramChatId), "%s", clearing ? "" : id);
  return true;
}

inline void publish(NetworkStateCode state, bool connected,
                    int8_t rssiDbm = -127) {
  __atomic_store_n(&publishedConfigured, credentialsConfigured(),
                   __ATOMIC_RELEASE);
  __atomic_store_n(&publishedConnected, connected, __ATOMIC_RELEASE);
  __atomic_store_n(&publishedRssiDbm, rssiDbm, __ATOMIC_RELEASE);
  __atomic_store_n(&publishedState, static_cast<uint8_t>(state),
                   __ATOMIC_RELEASE);
}

inline void stopRadio() {
  WiFi.setAutoReconnect(false);
  (void)WiFi.disconnect(true, false);
  radioActive = false;
  connectionStartedAt = 0U;
}

inline bool startStation(uint32_t now) {
  // Tai lieu Arduino-ESP32 yeu cau hostname duoc dat truoc khi khoi dong Wi-Fi.
  (void)WiFi.disconnect(true, false);
  if (!WiFi.setHostname(NETWORK_WIFI_HOSTNAME)) {
    stopRadio();
    return false;
  }
  if (!WiFi.mode(WIFI_STA)) {
    stopRadio();
    return false;
  }
  (void)WiFi.setAutoReconnect(true);
  const char *password = activePassword[0] == '\0' ? nullptr : activePassword;
  (void)WiFi.begin(activeSsid, password);
  radioActive = true;
  connectionStartedAt = now;
  publish(NetworkStateCode::Connecting, false);
  return true;
}

// ------------------------------ Cong 1 doi Wi-Fi -----------------------------
// Portal chi duoc yeu cau/huy tu HMI qua cac ham public ben duoi; toan bo xu ly
// thuc te nam trong networkTask (mayapNetworkUpdate) de khong dung chung stack
// TCP/DNS voi bat ky task nao khac.
static volatile uint8_t portalRequestFlag = 0U;   // 1 = HMI vua bam "Doi Wi-Fi"
static volatile uint8_t portalCancelFlag = 0U;    // 1 = HMI bam Thoat/Huy
static volatile uint8_t publishedPortalState =
    static_cast<uint8_t>(WifiPortalState::Idle);
static char publishedPortalApName[20] = "";
static portMUX_TYPE portalNameMux = portMUX_INITIALIZER_UNLOCKED;

enum class PortalPhase : uint8_t { Idle, Starting, ApActive, Testing, Success, Failed };
static PortalPhase portalPhase = PortalPhase::Idle;
static uint32_t portalOpenedAt = 0U;
static uint32_t portalTestStartedAt = 0U;
static uint32_t portalResultUntil_ = 0U;
static char portalApName[20] = "";
static char pendingSsid[WIFI_PORTAL_SSID_MAX + 1U] = "";
static char pendingPassword[WIFI_PORTAL_PASSWORD_MAX + 1U] = "";
static bool pendingCredentialsReady = false;

// Trang thai rieng cho pha "Starting": AP tren ESP32+STA da bat ke ca khi
// STA dang ket noi that (WiFi.softAP() vua bi tu choi vua bi cham) la
// nguyen nhan pho bien nhat khien AP "chap chon"/gan nhu khong phat song.
// Thu lai vai lan thay vi tin softAP() luon thanh cong ngay lan dau.
constexpr uint32_t WIFI_PORTAL_AP_RETRY_MS = 400UL;
constexpr uint32_t WIFI_PORTAL_AP_START_TIMEOUT_MS = 6000UL;
static uint32_t portalApNextAttemptAt_ = 0U;
static uint32_t portalApStartingSince_ = 0U;
static bool portalServersStarted_ = false;

static WebServer portalServer(80);
static DNSServer portalDns;

inline void publishPortalState(WifiPortalState state, const char *apName) {
  __atomic_store_n(&publishedPortalState, static_cast<uint8_t>(state),
                   __ATOMIC_RELEASE);
  portENTER_CRITICAL(&portalNameMux);
  snprintf(publishedPortalApName, sizeof(publishedPortalApName), "%s",
           apName ? apName : "");
  portEXIT_CRITICAL(&portalNameMux);
}

inline String htmlEscape(const String &s) {
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

inline String buildWifiOptions() {
  String options;
  const int count = WiFi.scanComplete() >= 0 ? WiFi.scanComplete()
                                             : WiFi.scanNetworks(false, true);
  if (count <= 0) return F("<option value=''>Khong tim thay Wi-Fi</option>");
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    options += F("<option value=\"");
    options += htmlEscape(ssid);
    options += F("\">");
    options += htmlEscape(ssid);
    options += F("  (");
    options += String(WiFi.RSSI(i));
    options += F(" dBm)</option>");
  }
  return options;
}

// Dinh danh thiet bi theo dung dinh dang "MAP-XXXXXXXXXXXX" ma web dung
// (xem realtime_link.h::ensureIdentity). File nay duoc include TRUOC
// realtime_link.h trong .ino nen tu tinh rieng, khong dung chung bien -
// cung ly do elapsedMs/timeReached o dau file nay phai co ban rieng.
inline String mayapDeviceIdText() {
  const uint64_t mac = ESP.getEfuseMac();
  char buf[20];
  snprintf(buf, sizeof(buf), "MAP-%02X%02X%02X%02X%02X%02X",
           static_cast<uint8_t>(mac >> 0), static_cast<uint8_t>(mac >> 8),
           static_cast<uint8_t>(mac >> 16), static_cast<uint8_t>(mac >> 24),
           static_cast<uint8_t>(mac >> 32), static_cast<uint8_t>(mac >> 40));
  return String(buf);
}

inline String buildConnectionInfo() {
  String info;
  info += F("<div class=info><span>ID thiet bi</span><b>");
  info += mayapDeviceIdText();
  info += F("</b></div>");
  info += F("<div class=info><span>Wi-Fi hien tai</span><b>");
  if (WiFi.status() == WL_CONNECTED) {
    info += htmlEscape(WiFi.SSID());
    info += F(" (");
    info += String(WiFi.RSSI());
    info += F(" dBm)");
  } else if (credentialsConfigured()) {
    info += htmlEscape(String(activeSsid));
    info += F(" (chua ket noi)");
  } else {
    info += F("Chua cau hinh");
  }
  info += F("</b></div>");
  return info;
}

inline String buildTelegramCard() {
  loadTelegramChatIdOnce();
  String html;
  html += F("<div class=card><h1>Thong bao Telegram</h1>"
             "<div class=info><span>Trang thai</span><b>");
  if (telegramChatId[0]) {
    html += F("Da cau hinh (Chat ID ");
    html += telegramChatId;
    html += F(")");
  } else {
    html += F("Chua cau hinh");
  }
  html += F("</b></div>"
             "<form method=POST action=/telegram-save>"
             "<label>Telegram Chat ID</label>"
             "<input name=chatid value=\"");
  html += htmlEscape(String(telegramChatId));  // luon la so/'-' sau validate, escape them cho chac
  html += F("\" maxlength=20 placeholder=\"Vi du 123456789, de trong de tat\">"
             "<button type=submit>Luu Chat ID</button></form>"
             "<p>Mo Telegram, nhan tin cho bot @userinfobot de lay Chat ID ca "
             "nhan (hoac lay Chat ID nhom neu them bot vao nhom). May se tu "
             "gui tin nhan xac nhan qua Telegram ngay khi ket noi mang thanh "
             "cong - can May Chinh dang o che do ONLINE.</p></div>");
  return html;
}

inline void handlePortalRoot() {
  const String connectionInfo = buildConnectionInfo();
  const String options = buildWifiOptions();
  String html;
  html.reserve(4096);
  html += F(
    "<!doctype html><html lang=vi><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>MAYAP Wi-Fi</title><style>"
    "body{font-family:sans-serif;background:#0b1220;color:#eef4ff;"
    "margin:0;padding:24px 16px}.card{max-width:420px;margin:auto;"
    "background:#111a2e;border:1px solid #263553;border-radius:16px;"
    "padding:22px}h1{font-size:20px;margin:0 0 14px}"
    "label{display:block;font-size:13px;margin:14px 0 6px;color:#9eabc2}"
    "select,input{width:100%;height:44px;border-radius:10px;border:1px "
    "solid #263553;background:#16213a;color:#eef4ff;padding:0 12px;"
    "font-size:15px;box-sizing:border-box}"
    "button,a.reload{width:100%;height:46px;margin-top:18px;border:0;"
    "border-radius:10px;background:#4f8cff;color:#fff;font-weight:700;"
    "font-size:15px;display:flex;align-items:center;justify-content:center;"
    "text-decoration:none;box-sizing:border-box}"
    "a.reload{background:#263553;margin-top:10px}"
    "p{color:#9eabc2;font-size:12px;line-height:1.5}"
    ".info{display:flex;justify-content:space-between;align-items:center;"
    "font-size:13px;padding:8px 0;border-bottom:1px solid #263553}"
    ".info span{color:#9eabc2}.info b{color:#eef4ff;font-weight:600;"
    "text-align:right;word-break:break-word}"
    ".card + .card{margin-top:16px}"
    "</style></head><body>"
    "<div class=card><h1>MAYAP - Doi Wi-Fi</h1>");
  html += connectionInfo;
  html += F(
    "<form method=POST action=/save>"
    "<label>Mang Wi-Fi</label><select name=ssid required>");
  html += options;
  html += F(
    "</select><label>Mat khau</label>"
    "<input name=password type=password maxlength=64 autocomplete=off>"
    "<button type=submit>Luu &amp; ket noi</button></form>"
    "<a class=reload href=/rescan>&#8635; Tim lai Wi-Fi</a>"
    "<p>Thiet bi se tu thu ket noi mang moi. Kiem tra man hinh may ap de "
    "biet ket qua. Trang nay tu dong dong sau vai phut.</p></div>");
  html += buildTelegramCard();
  html += F("</body></html>");
  portalServer.send(200, "text/html; charset=utf-8", html);
}

inline void handlePortalRescan() {
  // Xoa ket qua quet cu va quet lai dong bo (nguoi dung vua bam "Tim lai Wi-Fi"
  // nen cho doi vai giay la hop ly); buildWifiOptions() cua trang / se dung
  // lai ket qua nay ngay, khong quet lan thu hai.
  WiFi.scanDelete();
  WiFi.scanNetworks(false, true);
  portalServer.sendHeader("Location", "/", true);
  portalServer.send(302, "text/plain", "");
}

inline void handlePortalTelegramSave() {
  if (!portalServer.hasArg("chatid")) {
    portalServer.send(400, "text/plain; charset=utf-8", "Thieu Chat ID");
    return;
  }
  String chatId = portalServer.arg("chatid");
  chatId.trim();
  if (!chatId.isEmpty() && !isValidTelegramChatId(chatId.c_str())) {
    portalServer.send(400, "text/plain; charset=utf-8",
        "Chat ID khong hop le. Chi gom chu so, co the co dau - o dau (id nhom/kenh).");
    return;
  }
  if (!saveTelegramChatId(chatId.c_str())) {
    portalServer.send(400, "text/plain; charset=utf-8", "Khong luu duoc Chat ID");
    return;
  }
  portalServer.send(200, "text/html; charset=utf-8",
      chatId.isEmpty()
        ? "<html><meta charset=utf-8><body style='font-family:sans-serif'>"
          "Da tat thong bao Telegram cho may nay.</body></html>"
        : "<html><meta charset=utf-8><body style='font-family:sans-serif'>"
          "Da luu Chat ID. May se tu gui tin nhan xac nhan qua Telegram khi "
          "ket noi mang thanh cong (can May Chinh dang o che do "
          "ONLINE).</body></html>");
}

inline void handlePortalSave() {
  if (!portalServer.hasArg("ssid") || portalServer.arg("ssid").isEmpty()) {
    portalServer.send(400, "text/plain; charset=utf-8", "Thieu SSID");
    return;
  }
  const String ssid = portalServer.arg("ssid");
  const String pass = portalServer.arg("password");
  if (ssid.length() > WIFI_PORTAL_SSID_MAX ||
      pass.length() > WIFI_PORTAL_PASSWORD_MAX) {
    portalServer.send(400, "text/plain; charset=utf-8", "SSID/mat khau qua dai");
    return;
  }
  snprintf(pendingSsid, sizeof(pendingSsid), "%s", ssid.c_str());
  snprintf(pendingPassword, sizeof(pendingPassword), "%s", pass.c_str());
  pendingCredentialsReady = true;
  portalServer.send(200, "text/html; charset=utf-8",
      "<html><meta charset=utf-8><body style='font-family:sans-serif'>"
      "Da nhan Wi-Fi moi. May ap dang thu ket noi, vui long xem man hinh "
      "thiet bi.</body></html>");
}

inline void handlePortalNotFound() {
  // Captive portal: moi URL la khong xac dinh deu quay ve trang cau hinh.
  portalServer.sendHeader("Location", "http://192.168.4.1/", true);
  portalServer.send(302, "text/plain", "");
}

inline void portalStop() {
  portalServer.stop();
  portalDns.stop();
  if (portalPhase != PortalPhase::Idle) {
    // Neu dang o STA co ket noi that thi giu nguyen; chi tat AP.
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
  }
  portalPhase = PortalPhase::Idle;
  pendingCredentialsReady = false;
  publishPortalState(WifiPortalState::Idle, "");
}

// Bat AP that su. Tach rieng khoi portalBeginStarting() de goi lai duoc
// nhieu lan (retry) ma khong lam lai buoc doi mode/dat ten AP.
inline bool bringUpSoftAp() {
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  const bool ok = WiFi.softAP(portalApName);
  mayapSerialPrintf(false, "[PORTAL] softAP(%s) -> %s\n", portalApName,
                    ok ? "OK" : "FAIL");
  return ok;
}

inline void portalBeginStarting(uint32_t now) {
  const uint64_t chip = ESP.getEfuseMac();
  snprintf(portalApName, sizeof(portalApName), "MAYAP-%04X",
           static_cast<unsigned>((chip >> 32U) & 0xFFFFU));

  // RAT QUAN TRONG de AP phat song on dinh: tat auto-reconnect va ngat STA
  // dang co TRUOC khi doi mode. Neu khong, STA (dang tu dong thu ket noi lai
  // mang cu o nen) va AP moi bat se tranh gianh cung mot radio/lich trinh -
  // day chinh la nguyen nhan pho bien khien AP "chap chon", luc phat luc
  // khong, thay vi bao gio cung phat on dinh nhu mong doi.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP_STA);

  portalApStartingSince_ = now;
  portalApNextAttemptAt_ = now;
  portalServersStarted_ = false;
  pendingCredentialsReady = false;
  portalOpenedAt = now;
  portalPhase = PortalPhase::Starting;
  publishPortalState(WifiPortalState::Starting, portalApName);
}

inline void serviceStarting(uint32_t now) {
  if (!timeReached(now, portalApNextAttemptAt_)) return;
  portalApNextAttemptAt_ = now + WIFI_PORTAL_AP_RETRY_MS;

  if (!bringUpSoftAp()) {
    if (elapsedMs(now, portalApStartingSince_) >= WIFI_PORTAL_AP_START_TIMEOUT_MS) {
      mayapSerialPrintf(true,
          "[PORTAL] khong bat duoc AP sau %lums, huy mo cong\n",
          static_cast<unsigned long>(WIFI_PORTAL_AP_START_TIMEOUT_MS));
      WiFi.mode(WIFI_STA);
      portalPhase = PortalPhase::Idle;
      __atomic_store_n(&portalRequestFlag, 0U, __ATOMIC_RELEASE);
      publishPortalState(WifiPortalState::Idle, "");
    }
    return;
  }

  if (!portalServersStarted_) {
    portalDns.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
    portalServer.on("/", HTTP_GET, handlePortalRoot);
    portalServer.on("/save", HTTP_POST, handlePortalSave);
    portalServer.on("/rescan", HTTP_GET, handlePortalRescan);
    portalServer.on("/telegram-save", HTTP_POST, handlePortalTelegramSave);
    portalServer.onNotFound(handlePortalNotFound);
    portalServer.begin();
    portalServersStarted_ = true;
  }
  portalPhase = PortalPhase::ApActive;
  publishPortalState(WifiPortalState::ApActive, portalApName);
}

inline void servicePortal(uint32_t now) {
  const bool requested = __atomic_load_n(&portalRequestFlag, __ATOMIC_ACQUIRE) != 0U;
  const bool cancel = __atomic_load_n(&portalCancelFlag, __ATOMIC_ACQUIRE) != 0U;
  if (cancel) {
    __atomic_store_n(&portalCancelFlag, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&portalRequestFlag, 0U, __ATOMIC_RELEASE);
    if (portalPhase != PortalPhase::Idle) portalStop();
    return;
  }

  if (portalPhase == PortalPhase::Idle) {
    if (!requested) return;
    portalBeginStarting(now);
    return;
  }

  // Da het han mo cong: dong lai va quay ve ket noi binh thuong.
  if (elapsedMs(now, portalOpenedAt) >= WIFI_PORTAL_MAX_OPEN_MS &&
      portalPhase != PortalPhase::Testing) {
    portalStop();
    return;
  }

  if (portalPhase == PortalPhase::Starting) {
    serviceStarting(now);
    return;
  }

  portalDns.processNextRequest();
  portalServer.handleClient();

  if (portalPhase == PortalPhase::ApActive) {
    if (pendingCredentialsReady) {
      pendingCredentialsReady = false;
      (void)saveCredentials(pendingSsid, pendingPassword);
      WiFi.begin(activeSsid, activePassword[0] ? activePassword : nullptr);
      portalTestStartedAt = now;
      portalPhase = PortalPhase::Testing;
      publishPortalState(WifiPortalState::Testing, portalApName);
    }
    return;
  }

  if (portalPhase == PortalPhase::Testing) {
    if (WiFi.status() == WL_CONNECTED) {
      portalPhase = PortalPhase::Success;
      portalResultUntil_ = now + 8000UL;
      publishPortalState(WifiPortalState::Success, portalApName);
      // Da co mang moi hoat dong: dong AP ngay, khong can nguoi dung thao tac them.
      WiFi.softAPdisconnect(true);
      portalServer.stop();
      portalDns.stop();
      return;
    }
    if (elapsedMs(now, portalTestStartedAt) >= WIFI_PORTAL_TEST_TIMEOUT_MS) {
      portalPhase = PortalPhase::Failed;
      portalResultUntil_ = now + 8000UL;
      publishPortalState(WifiPortalState::Failed, portalApName);
    }
    return;
  }

  if (portalPhase == PortalPhase::Success || portalPhase == PortalPhase::Failed) {
    if (portalPhase == PortalPhase::Failed) {
      // Cho phep nguoi dung mo trang lai thu SSID/mat khau khac trong cung phien AP.
      portalPhase = PortalPhase::ApActive;
      publishPortalState(WifiPortalState::ApActive, portalApName);
      return;
    }
    if (timeReached(now, portalResultUntil_)) {
      portalStop();
      __atomic_store_n(&portalRequestFlag, 0U, __ATOMIC_RELEASE);
    }
  }
}

}  // namespace MayapNetworkInternal

inline void mayapNetworkBegin() {
  using namespace MayapNetworkInternal;
  loadCredentialsOnce();
  loadTelegramChatIdOnce();
  __atomic_store_n(&requestedMode,
                   static_cast<uint8_t>(ConnectivityMode::Offline),
                   __ATOMIC_RELEASE);
  stopRadio();
  publish(NetworkStateCode::Offline, false);
}

// Chi doc/ghi tu networkTask: telegram_link.h::mayapTelegramUpdate() chay
// trong chinh task nay (giong cach mqtt.loop() cua realtime_link.h dung
// chung task), va trang cau hinh Telegram cua cong doi Wi-Fi cung xu ly tai
// day. Khong can atomic/mutex vi khong co task nao khac dung toi 2 bien nay.
inline const char *mayapGetTelegramChatId() {
  using namespace MayapNetworkInternal;
  loadTelegramChatIdOnce();
  return telegramChatId;
}

inline bool mayapSetTelegramChatId(const char *id) {
  return MayapNetworkInternal::saveTelegramChatId(id);
}

inline void mayapSetConnectivityMode(ConnectivityMode mode) {
  if (static_cast<uint8_t>(mode) >
      static_cast<uint8_t>(ConnectivityMode::Online)) {
    mode = ConnectivityMode::Offline;
  }
  __atomic_store_n(&MayapNetworkInternal::requestedMode,
                   static_cast<uint8_t>(mode), __ATOMIC_RELEASE);
  // Chuyen ve OFFLINE phai dong luon cong doi Wi-Fi neu dang mo, tranh AP
  // "mo cua" sau khi nguoi dung da chon rut mang.
  if (mode != ConnectivityMode::Online) {
    __atomic_store_n(&MayapNetworkInternal::portalCancelFlag, 1U,
                     __ATOMIC_RELEASE);
  }
}

inline NetworkStatus mayapGetNetworkStatus() {
  using namespace MayapNetworkInternal;
  NetworkStatus status{};
  uint8_t mode = __atomic_load_n(&requestedMode, __ATOMIC_ACQUIRE);
  if (mode > static_cast<uint8_t>(ConnectivityMode::Online)) {
    mode = static_cast<uint8_t>(ConnectivityMode::Offline);
  }
  uint8_t state = __atomic_load_n(&publishedState, __ATOMIC_ACQUIRE);
  if (state > static_cast<uint8_t>(NetworkStateCode::Connected)) {
    state = static_cast<uint8_t>(NetworkStateCode::Offline);
  }
  status.requestedMode = static_cast<ConnectivityMode>(mode);
  status.state = static_cast<NetworkStateCode>(state);
  status.credentialsConfigured = __atomic_load_n(
      &publishedConfigured, __ATOMIC_ACQUIRE);
  status.connected = __atomic_load_n(&publishedConnected, __ATOMIC_ACQUIRE);
  status.rssiDbm = __atomic_load_n(&publishedRssiDbm, __ATOMIC_ACQUIRE);
  return status;
}

inline bool mayapRequestWifiPortal() {
  using namespace MayapNetworkInternal;
  const uint8_t mode = __atomic_load_n(&requestedMode, __ATOMIC_ACQUIRE);
  if (mode != static_cast<uint8_t>(ConnectivityMode::Online)) return false;
  __atomic_store_n(&portalCancelFlag, 0U, __ATOMIC_RELEASE);
  __atomic_store_n(&portalRequestFlag, 1U, __ATOMIC_RELEASE);
  return true;
}

inline void mayapCancelWifiPortal() {
  __atomic_store_n(&MayapNetworkInternal::portalCancelFlag, 1U, __ATOMIC_RELEASE);
}

inline WifiPortalStatus mayapGetWifiPortalStatus() {
  using namespace MayapNetworkInternal;
  WifiPortalStatus status{};
  uint8_t state = __atomic_load_n(&publishedPortalState, __ATOMIC_ACQUIRE);
  if (state > static_cast<uint8_t>(WifiPortalState::Failed)) {
    state = static_cast<uint8_t>(WifiPortalState::Idle);
  }
  status.state = static_cast<WifiPortalState>(state);
  portENTER_CRITICAL(&portalNameMux);
  snprintf(status.apName, sizeof(status.apName), "%s", publishedPortalApName);
  portEXIT_CRITICAL(&portalNameMux);
  return status;
}

inline void mayapNetworkUpdate(uint32_t now) {
  using namespace MayapNetworkInternal;
  servicePortal(now);

  const uint8_t requested = __atomic_load_n(&requestedMode, __ATOMIC_ACQUIRE);
  const bool onlineRequested =
      requested == static_cast<uint8_t>(ConnectivityMode::Online);
  const bool portalOwnsRadio = portalPhase != PortalPhase::Idle;

  if (!onlineRequested) {
    if (radioActive) stopRadio();
    // Nguoi dung chu dong rut OFFLINE - khi ho bat lai ONLINE (co the sau
    // vai gio/vai ngay), cho phep thu ket noi ngay lap tuc thay vi ke thua
    // buoc backoff cua lan mat mang KHONG lien quan truoc do.
    staBackoff.reset(now);
    if (!portalOwnsRadio) publish(NetworkStateCode::Offline, false);
    return;
  }

  if (!credentialsConfigured()) {
    if (radioActive) stopRadio();
    staBackoff.reset(now);
    if (!portalOwnsRadio) publish(NetworkStateCode::NotConfigured, false);
    return;
  }

  // Trong luc cong dang mo (AP/Testing), STA thuong thuong duoc portal tu
  // dieu khien (WiFi.begin khi test SSID moi); khong de vong lap STA binh
  // thuong danh nhau voi no.
  if (portalOwnsRadio) {
    if (WiFi.isConnected()) {
      int32_t rssi = WiFi.RSSI();
      if (rssi < -127) rssi = -127;
      if (rssi > 0) rssi = 0;
      publish(NetworkStateCode::Connected, true, static_cast<int8_t>(rssi));
    } else {
      publish(NetworkStateCode::Connecting, false);
    }
    radioActive = false;  // ep STA-only loop ben duoi khoi dong lai sau khi portal dong
    // Giu backoff o step 0 suot thoi gian cong dang mo (goi lai moi tick o
    // day khong sao - chi la 2 phep gan so). Khi cong dong va nhuong lai
    // quyen dieu khien STA, vong lap ben duoi luon bat dau tu do tre ngan
    // nhat, khong "an theo" so lan that bai cua mang CU truoc khi mo cong.
    staBackoff.reset(now);
    return;
  }

  if (!radioActive) {
    if (!staBackoff.ready(now)) {
      publish(NetworkStateCode::Connecting, false);
      return;
    }
    const bool started = startStation(now);
    if (!started) {
      // setHostname()/mode() that bai (rat hiem - loi driver): lui backoff
      // truoc khi thu lai, khong dap lien tuc gay xoay vong CPU vo ich.
      staBackoff.onFailure(now);
      publish(NetworkStateCode::Connecting, false);
      return;
    }
    return;  // vua goi WiFi.begin(): danh cho no NETWORK_CONNECT_TIMEOUT_MS de ket noi
  }

  if (WiFi.isConnected()) {
    staBackoff.onSuccess();  // dat lai retry counter dung yeu cau
    int32_t rssi = WiFi.RSSI();
    if (rssi < -127) rssi = -127;
    if (rssi > 0) rssi = 0;
    publish(NetworkStateCode::Connected, true,
            static_cast<int8_t>(rssi));
    return;
  }

  publish(NetworkStateCode::Connecting, false);
  if (elapsedMs(now, connectionStartedAt) < NETWORK_CONNECT_TIMEOUT_MS) {
    return;  // van con trong thoi gian cho hop ly cho lan thu hien tai
  }
  if (!staBackoff.ready(now)) return;  // dang trong thoi gian lui backoff

  staBackoff.onFailure(now);
  connectionStartedAt = now;
  if (!WiFi.reconnect()) {
    const char *password = activePassword[0] == '\0' ? nullptr : activePassword;
    (void)WiFi.begin(activeSsid, password);
  }
}
