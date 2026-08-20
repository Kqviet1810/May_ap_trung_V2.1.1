#pragma once

#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>

// ============================================================================
// LOP THONG BAO TELEGRAM (KENH DOC LAP HOAN TOAN VOI WEB/MQTT)
// ----------------------------------------------------------------------------
// KHONG dung chung ket noi/hang doi voi realtime_link.h. File nay tu mo HTTPS
// rieng toi api.telegram.org (sendMessage de gui, getUpdates de nhan lenh
// /status, /help) - mat MQTT/Web KHONG anh huong gi den kenh nay, va nguoc
// lai. Nguon du lieu goc van la MachineController (qua mayapTelegramSetRuntime,
// goi tu controlTask giong het pattern mayapWebSetRuntime cua realtime_link.h).
//
// Vi tri include: sau hmi.h (dung mayapDeviceIdText tu network_service.h va
// cac kieu MachineRuntime/FaultCode tu machine_control.h/config.h da include
// truoc do), truoc machine_control.h (MachineController goi nguoc lai
// mayapTelegramSetRuntime()).
//
// Thu vien can cai: KHONG can cai them - HTTPClient/WiFiClientSecure co san
// trong ESP32 Arduino core; ArduinoJson da la yeu cau cua realtime_link.h.
//
// Tai nguyen: moi lan gui/nhan tao MOI mot WiFiClientSecure NGAN HAN (huy ngay
// sau khi xong), khong giu ket noi thuong truc nhu MQTT - phu hop voi tan
// suat thap (vai phut/lan) va tranh chiem RAM lau dai tren thiet bi khong
// PSRAM. Buffer TLS duoc giam qua setBufferSizes() de gioi han dinh RAM tam
// thoi trong luc goi (~9 KB thay vi mac dinh ~32 KB/phien).
// ============================================================================

namespace MayapTelegramInternal {

inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return static_cast<uint32_t>(now - then);
}
inline bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

enum class NotifyLevel : uint8_t { Info, Warning, Critical, System };

// ------------------------------ Hop thu runtime --------------------------------
// Ghi boi controlTask (mayapTelegramSetRuntime), doc boi networkTask. Copy
// nguyen struct trong critical section ngan, giong het pattern cua
// realtime_link.h::mayapWebSetRuntime - khong I/O trong vung khoa.
static portMUX_TYPE tgMux = portMUX_INITIALIZER_UNLOCKED;
static MachineRuntime knownRuntime{};
static bool knownRuntimeValid = false;

// Ban lam viec RIENG cua networkTask, cap nhat tu knownRuntime moi chu ky
// kiem tra (duoi khoa, xong roi sao chep ra ngoai). Tat ca ham quyet dinh
// (checkFaults/checkTransitions/handleCommand) chi doc bien nay - khong bao
// gio bi controlTask ghi de, tranh rang buoc "doc-roi-ghi-lai" khong an toan.
static MachineRuntime processingRuntime{};

// Backoff RIENG cho Telegram - hoan toan doc lap voi backoff cua MQTT
// (realtime_link.h) va STA Wi-Fi (network_service.h). Ap dung cho CA hai loai
// goi HTTPS (gui tin va hoi lenh): 1 lan that bai (bat ke gui hay hoi lenh)
// deu keo gian lan thu tiep theo cua CA HAI, vi ca hai cung phan anh cung 1
// cau hoi "co goi duoc toi api.telegram.org luc nay khong". Thanh cong o BAT
// KY chieu nao cung reset ve nhanh nhat cho ca hai. TELEGRAM_MIN_SEND_GAP_MS/
// TELEGRAM_POLL_INTERVAL_MS ben duoi la nhip lam viec BINH THUONG khi moi
// thu deu on - khong phai retry, nen giu nguyen co dinh (khong phai dieu
// nguoi dung phan nan); backoff nay chi kich hoat THEM khi co that bai.
static BackoffTimer telegramBackoff{};

// -------------------------------- Hang doi gui ----------------------------------
// Chi networkTask dung (ca ghi lan doc) - moi logic quyet dinh gui gi cung
// chay trong mayapTelegramUpdate(), khong co task nao khac cham vao.
struct OutboxItem {
  bool used = false;
  char text[200] = "";
};
static OutboxItem outbox[TELEGRAM_OUTBOX_SIZE];
static uint8_t outboxHead = 0U, outboxTail = 0U, outboxCount = 0U;
static uint32_t lastSendAt = 0U;

inline bool enqueueRaw(const char *text) {
  if (!text || !text[0]) return false;
  if (outboxCount >= TELEGRAM_OUTBOX_SIZE) {
    // Hang doi day (rat hiem - nhieu loi phat sinh dong thoi hon ca toc do
    // gui): bo tin CU nhat de nhuong cho tin moi, tranh ket "spam do" vinh
    // vien khi mang cham. Khong lam crash/treo, chi mat 1 thong bao cu.
    mayapSerialPrintf(false, "[TELEGRAM] outbox day, bo tin cu nhat\n");
    outbox[outboxHead].used = false;
    outboxHead = static_cast<uint8_t>((outboxHead + 1U) % TELEGRAM_OUTBOX_SIZE);
    --outboxCount;
  }
  snprintf(outbox[outboxTail].text, sizeof(outbox[outboxTail].text), "%s", text);
  outbox[outboxTail].used = true;
  outboxTail = static_cast<uint8_t>((outboxTail + 1U) % TELEGRAM_OUTBOX_SIZE);
  ++outboxCount;
  return true;
}

inline const char *levelTag(NotifyLevel level) {
  switch (level) {
    case NotifyLevel::Critical: return "NGHIEM TRONG";
    case NotifyLevel::Warning: return "CANH BAO";
    case NotifyLevel::System: return "HE THONG";
    default: return "THONG TIN";
  }
}

inline void enqueueLevel(NotifyLevel level, const char *body) {
  char text[200];
  snprintf(text, sizeof(text), "[%s] %s\n%s", levelTag(level),
           mayapDeviceIdText().c_str(), body);
  enqueueRaw(text);
}

inline void enqueueResolved(const char *body) {
  char text[200];
  snprintf(text, sizeof(text), "[DA KHAC PHUC] %s\n%s",
           mayapDeviceIdText().c_str(), body);
  enqueueRaw(text);
}

// --------------------------- Noi dung loi (Vietnamese) --------------------------
inline const char *faultSummaryText(uint16_t code) {
  switch (code) {
    case 101: return "Mất cảm biến nhiệt độ/độ ẩm";
    case 102: return "Cảm biến trả về giá trị sai";
    case 103: return "Cảm biến bất thường (nghi ngờ hỏng)";
    case 110: return "Nhiệt độ xuống thấp hơn ngưỡng cảnh báo";
    case 111: return "Nhiệt độ vượt quá ngưỡng cảnh báo cao";
    case 112: return "QUÁ NHIỆT KHẨN CẤP - đã ngắt nguồn nhiệt ngay lập tức";
    case 120: return "Độ ẩm thấp - kiểm tra nguồn cấp nước";
    case 130: return "Công tắc nhiệt bị tắt trong lúc đang ấp";
    case 132: return "Cần bật lại chế độ AUTO để tiếp tục mẻ";
    case 133: return "Chế độ AUTO bị tắt trong lúc đang ấp";
    case 134: return "Đảo trứng tự động bị khóa trong lúc đang ấp";
    case 201: return "Lỗi cả 2 công tắc hành trình đảo trứng";
    case 202: return "Đảo trứng quá thời gian cho phép";
    case 203: return "Cơ cấu đảo trứng bị kẹt";
    case 204: return "Xung đột lệnh điều khiển đảo trứng";
    case 301: return "Mất dữ liệu cấu hình EEPROM";
    case 302: return "Bộ nhớ EEPROM có dấu hiệu suy giảm";
    case 303: return "Máy vừa khởi động lại bất thường";
    case 304: return "Xung đột tín hiệu điều khiển đầu ra";
    case 305: return "Relay đóng cắt quá nhiều lần trong giờ";
    case 306: return "Lỗi đồng hồ thời gian thực (RTC)";
    case 313: return "Đang dọn dẹp dữ liệu mẻ cũ trước đó";
    case 314: return "Mất nhật ký an toàn (safety journal)";
    case 315: return "Mất nhật ký sự kiện của mẻ";
    default: return "Lỗi không xác định";
  }
}

// HmiFaultItem.severity la uint8_t "tho" (khong phai enum Mayap::FaultSeverity)
// chinh vi ly do nay: config.h dung truoc machine_control.h trong thu tu
// include, va telegram_link.h cung dung truoc machine_control.h nen KHONG
// the tham chieu toi kieu Mayap::FaultSeverity (chi ton tai sau khi
// machine_control.h duoc doc). Dung thang gia tri so, khop dinh nghia enum
// {Info=0, Warning=1, Stop=2, Emergency=3} trong machine_control.h.
constexpr uint8_t FAULT_SEVERITY_INFO = 0U;
constexpr uint8_t FAULT_SEVERITY_WARNING = 1U;
constexpr uint8_t FAULT_SEVERITY_STOP = 2U;
constexpr uint8_t FAULT_SEVERITY_EMERGENCY = 3U;

inline NotifyLevel levelForSeverity(uint8_t severity) {
  if (severity == FAULT_SEVERITY_EMERGENCY || severity == FAULT_SEVERITY_STOP) {
    return NotifyLevel::Critical;
  }
  if (severity == FAULT_SEVERITY_WARNING) return NotifyLevel::Warning;
  return NotifyLevel::Info;
}

inline uint32_t repeatIntervalForSeverity(uint8_t severity) {
  if (severity == FAULT_SEVERITY_EMERGENCY) return TELEGRAM_REPEAT_CRITICAL_EMERGENCY_MS;
  if (severity == FAULT_SEVERITY_STOP) return TELEGRAM_REPEAT_CRITICAL_STOP_MS;
  if (severity == FAULT_SEVERITY_WARNING) return TELEGRAM_REPEAT_WARNING_MS;
  return TELEGRAM_REPEAT_INFO_MS;
}

// ------------------------- Theo doi loi dang xay ra ------------------------------
// Rieng cho Telegram, doc lap voi eventLog_/HMI: chi quan tam "dieu kien co
// con that su xay ra hay khong" (flags bit0) de quyet dinh gui moi/nhac
// lai/da het, khong quan tam co ACK tren HMI hay chua.
struct FaultTrack {
  bool used = false;
  uint16_t code = 0;
  uint8_t severity = 0;
  uint32_t firstSentAt = 0;
  uint32_t lastSentAt = 0;
};
static FaultTrack faultTrack[TELEGRAM_ACTIVE_TRACK_SIZE];

inline void checkFaults(uint32_t now) {
  bool seen[TELEGRAM_ACTIVE_TRACK_SIZE]{};
  const uint8_t count = processingRuntime.activeFaultDisplayCount;
  for (uint8_t i = 0; i < count; ++i) {
    const HmiFaultItem &item = processingRuntime.activeFaults[i];
    const bool conditionLive = (item.flags & 0x01U) != 0U;
    if (!conditionLive) continue;

    int16_t slot = -1;
    for (uint8_t s = 0; s < TELEGRAM_ACTIVE_TRACK_SIZE; ++s) {
      if (faultTrack[s].used && faultTrack[s].code == item.code) { slot = static_cast<int16_t>(s); break; }
    }
    if (slot < 0) {
      for (uint8_t s = 0; s < TELEGRAM_ACTIVE_TRACK_SIZE; ++s) {
        if (!faultTrack[s].used) { slot = static_cast<int16_t>(s); break; }
      }
    }
    if (slot < 0) continue;  // bang theo doi day (rat hiem) - bo qua ky nay

    seen[slot] = true;
    FaultTrack &track = faultTrack[static_cast<uint8_t>(slot)];
    const NotifyLevel level = levelForSeverity(item.severity);
    const uint32_t interval = repeatIntervalForSeverity(item.severity);
    char body[160];

    if (!track.used) {
      track.used = true;
      track.code = item.code;
      track.severity = item.severity;
      track.firstSentAt = now;
      track.lastSentAt = now;
      snprintf(body, sizeof(body), "%s (mã lỗi %u)", faultSummaryText(item.code), item.code);
      enqueueLevel(level, body);
    } else if (timeReached(now, track.lastSentAt + interval)) {
      track.lastSentAt = now;
      snprintf(body, sizeof(body), "Vẫn còn: %s (mã lỗi %u)", faultSummaryText(item.code), item.code);
      enqueueLevel(level, body);
    }
  }

  for (uint8_t s = 0; s < TELEGRAM_ACTIVE_TRACK_SIZE; ++s) {
    if (faultTrack[s].used && !seen[s]) {
      char body[160];
      snprintf(body, sizeof(body), "%s (mã lỗi %u)", faultSummaryText(faultTrack[s].code), faultTrack[s].code);
      enqueueResolved(body);
      faultTrack[s] = FaultTrack{};
    }
  }
}

// --------------------------- Su kien mot lan (INFO/SYSTEM) -----------------------
static bool lastBatchRunning = false;
static bool haveLastBatchRunning = false;
static bool lastOnline = false;
static bool haveLastOnline = false;

inline void checkTransitions(uint32_t now) {
  (void)now;
  if (!haveLastBatchRunning) {
    lastBatchRunning = processingRuntime.batchRunning;
    haveLastBatchRunning = true;
  } else if (processingRuntime.batchRunning != lastBatchRunning) {
    lastBatchRunning = processingRuntime.batchRunning;
    enqueueLevel(NotifyLevel::Info, processingRuntime.batchRunning
        ? "Đã bắt đầu mẻ ấp mới."
        : "Đã kết thúc mẻ ấp.");
  }
}

inline void checkConnectivity(uint32_t now) {
  (void)now;
  const NetworkStatus status = mayapGetNetworkStatus();
  const bool online = status.requestedMode == ConnectivityMode::Online && status.connected;
  if (!haveLastOnline) {
    lastOnline = online;
    haveLastOnline = true;
    if (online) enqueueLevel(NotifyLevel::System, "Máy đã khởi động và có mạng - sẵn sàng gửi thông báo.");
    return;
  }
  if (online != lastOnline) {
    lastOnline = online;
    enqueueLevel(NotifyLevel::System, online
        ? "Đã khôi phục kết nối mạng."
        : "Mất kết nối mạng - tin nhắn sẽ được gửi lại khi có mạng trở lại.");
  }
}

// ------------------------------- Goi HTTPS ---------------------------------------
inline String urlEncode(const char *text) {
  String out;
  out.reserve(strlen(text) + 8U);
  for (const char *p = text; *p; ++p) {
    const uint8_t c = static_cast<uint8_t>(*p);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else if (c == '\n') {
      out += "%0A";
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

inline bool sendTelegramMessage(const char *text) {
  const char *chatId = mayapGetTelegramChatId();
  if (!chatId[0] || !TELEGRAM_BOT_TOKEN[0]) return false;

  WiFiClientSecure client;
  client.setInsecure();  // khong xac thuc CA (giong lop MQTT TLS) - xem ghi chu dau file
  client.setBufferSizes(8192, 1024);  // giam RAM tam thoi so voi mac dinh 16K/16K

  HTTPClient http;
  http.setConnectTimeout(TELEGRAM_HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(TELEGRAM_HTTP_TIMEOUT_MS);

  char url[128];
  snprintf(url, sizeof(url), "https://%s/bot%s/sendMessage", TELEGRAM_API_HOST, TELEGRAM_BOT_TOKEN);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body;
  body.reserve(strlen(text) * 3U + 16U);
  body += "chat_id=";
  body += chatId;
  body += "&text=";
  body += urlEncode(text);

  const int code = http.POST(body);
  const bool ok = code == 200;
  mayapSerialPrintf(false, "[TELEGRAM] sendMessage -> HTTP %d %s\n", code, ok ? "OK" : "FAIL");
  http.end();
  return ok;
}

inline void drainOutbox(uint32_t now) {
  if (outboxCount == 0U) return;
  if (!timeReached(now, lastSendAt + TELEGRAM_MIN_SEND_GAP_MS)) return;
  if (!telegramBackoff.ready(now)) return;  // lan goi truoc (gui hoac hoi lenh) vua that bai
  const NetworkStatus status = mayapGetNetworkStatus();
  if (!(status.requestedMode == ConnectivityMode::Online && status.connected)) return;

  lastSendAt = now;
  OutboxItem &item = outbox[outboxHead];
  const bool ok = sendTelegramMessage(item.text);
  if (ok) {
    telegramBackoff.onSuccess();
    item.used = false;
    outboxHead = static_cast<uint8_t>((outboxHead + 1U) % TELEGRAM_OUTBOX_SIZE);
    --outboxCount;
  } else {
    // That bai (mang chap chon, Telegram loi tam thoi...): giu nguyen dau
    // hang doi (khong mat tin), nhung lui backoff truoc khi cho phep thu lai
    // - khong dap HTTPS lien tuc moi TELEGRAM_MIN_SEND_GAP_MS trong khi mang
    // dang that su mat trong nhieu gio.
    telegramBackoff.onFailure(now);
  }
}

// -------------------------- Lenh tu Telegram (getUpdates) ------------------------
static uint32_t lastPollAt = 0U;
static int64_t updateOffset = 0;
// updateOffset chi o RAM, mat khi may reset. Neu khong danh dau, lan poll dau
// tien sau reset se tai lai TOAN BO backlog lenh cu con dong tren server
// Telegram (mac dinh luu ~24h) va tra loi lai chung - gay nham lan/spam. Bo
// qua (chi nuot offset, khong xu ly) o lan poll DAU TIEN sau moi lan khoi
// dong, giong quy uoc pho bien cua cac bot Telegram khac.
static bool updateOffsetInitialized = false;

inline void handleCommand(const char *fromChatId, const char *text) {
  const char *myChatId = mayapGetTelegramChatId();
  // Chi tra loi dung Chat ID da cau hinh - tranh bat ky ai biet ten bot deu
  // co the "hoi chuyen" may (bao mat toi thieu can co).
  if (!myChatId[0] || strcmp(fromChatId, myChatId) != 0) return;

  if (!strncmp(text, "/status", 7)) {
    char body[220];
    snprintf(body, sizeof(body),
        "Nhiệt độ: %.1f°C\nĐộ ẩm: %.0f%%\nTrạng thái: %s\nMẻ ấp: %s",
        processingRuntime.temperature, processingRuntime.humidity, processingRuntime.machineState,
        processingRuntime.batchRunning ? "Đang chạy" : "Không chạy");
    enqueueLevel(NotifyLevel::Info, body);
  } else if (!strncmp(text, "/help", 5) || !strncmp(text, "/start", 6)) {
    enqueueLevel(NotifyLevel::Info,
        "Các lệnh hỗ trợ:\n/status - Xem tình trạng hiện tại\n/help - Xem hướng dẫn này");
  }
}

inline void pollUpdates(uint32_t now) {
  if (!timeReached(now, lastPollAt + TELEGRAM_POLL_INTERVAL_MS)) return;
  const char *chatId = mayapGetTelegramChatId();
  if (!chatId[0] || !TELEGRAM_BOT_TOKEN[0]) return;
  if (!telegramBackoff.ready(now)) return;  // lan goi truoc (gui hoac hoi lenh) vua that bai
  const NetworkStatus status = mayapGetNetworkStatus();
  if (!(status.requestedMode == ConnectivityMode::Online && status.connected)) return;
  lastPollAt = now;

  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(8192, 1024);
  HTTPClient http;
  http.setConnectTimeout(TELEGRAM_HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(TELEGRAM_HTTP_TIMEOUT_MS);

  char url[176];
  snprintf(url, sizeof(url), "https://%s/bot%s/getUpdates?offset=%lld&timeout=0",
           TELEGRAM_API_HOST, TELEGRAM_BOT_TOKEN, static_cast<long long>(updateOffset));
  if (!http.begin(client, url)) {
    telegramBackoff.onFailure(now);
    return;
  }
  const int code = http.GET();
  if (code != 200) {
    telegramBackoff.onFailure(now);
    mayapSerialPrintf(false, "[TELEGRAM] getUpdates -> HTTP %d, thu lai sau %lums\n", code,
                      static_cast<unsigned long>(telegramBackoff.nextAttemptAt - now));
    http.end();
    return;
  }
  telegramBackoff.onSuccess();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return;

  JsonArrayConst results = doc["result"].as<JsonArrayConst>();
  const bool discardBacklog = !updateOffsetInitialized;
  updateOffsetInitialized = true;
  for (size_t i = 0; i < results.size(); ++i) {
    JsonObjectConst update = results[i].as<JsonObjectConst>();
    const int64_t updateId = update["update_id"] | static_cast<int64_t>(0);
    if (updateId >= updateOffset) updateOffset = updateId + 1;
    if (discardBacklog) continue;  // nuot backlog cu sau reset, khong tra loi lai
    JsonObjectConst message = update["message"].as<JsonObjectConst>();
    if (message.isNull()) continue;
    const int64_t chatIdNum = message["chat"]["id"] | static_cast<int64_t>(0);
    const char *text = message["text"] | "";
    char chatIdBuf[24];
    snprintf(chatIdBuf, sizeof(chatIdBuf), "%lld", static_cast<long long>(chatIdNum));
    handleCommand(chatIdBuf, text);
  }
}

}  // namespace MayapTelegramInternal

// ================================ API cong khai ================================

inline void mayapTelegramBegin() {
  // Khong can khoi tao gi truoc: moi client HTTPS la ngan han, tao khi can gui.
}

// Chi duoc goi tu networkTask (khong blocking task khac; ban than no CO the
// block chinh networkTask vai giay khi thuc su goi HTTPS, xem ghi chu dau file).
inline void mayapTelegramUpdate(uint32_t now) {
  using namespace MayapTelegramInternal;
  const char *chatId = mayapGetTelegramChatId();
  if (!chatId[0]) return;  // chua cau hinh Chat ID - khong lam gi ca, tiet kiem tai nguyen

  static uint32_t lastCheckAt = 0U;
  if (timeReached(now, lastCheckAt + TELEGRAM_CHECK_INTERVAL_MS)) {
    lastCheckAt = now;
    portENTER_CRITICAL(&tgMux);
    const bool valid = knownRuntimeValid;
    if (valid) processingRuntime = knownRuntime;
    portEXIT_CRITICAL(&tgMux);
    if (valid) {
      checkFaults(now);
      checkTransitions(now);
    }
    checkConnectivity(now);
  }

  drainOutbox(now);
  pollUpdates(now);
}

// MachineController goi ham nay tu controlTask, cung noi/cung nhip voi
// mayapWebSetRuntime() cua realtime_link.h (xem may_ap_industrial.ino/
// machine_control.h::copyRuntimeToHmi khu vuc goi hmiSetRuntime()).
inline void mayapTelegramSetRuntime(const MachineRuntime &runtime) {
  using namespace MayapTelegramInternal;
  portENTER_CRITICAL(&tgMux);
  knownRuntime = runtime;
  knownRuntimeValid = true;
  portEXIT_CRITICAL(&tgMux);
}
