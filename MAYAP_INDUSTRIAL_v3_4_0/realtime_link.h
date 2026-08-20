#pragma once

#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#if MQTT_USE_TLS
#include <WiFiClientSecure.h>
#endif
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

// ============================================================================
// LOP GIAO TIEP THOI GIAN THUC WEB <-> ESP32 (MQTT qua broker)
// ----------------------------------------------------------------------------
// Thay the cho "MQTT chua on dinh": ban web (app.js) da san co giao thuc MQTT
// day du (presence/snapshot/config/command/ack/log/session) nhung firmware
// truoc day CHUA co client MQTT nao ca. File nay them client MQTT non-blocking,
// dung LAI toan bo hang doi lenh/luu cau hinh da co san cua HMI (queueCommand/
// startConfigSave) de web va HMI chia se cung mot co che an toan, khong tao
// duong dieu khien thiet bi rieng biet thu hai.
//
// QUAN TRONG VE LUONG (khac hmi.h o mot diem): moi thao tac MQTT that su
// (mqtt.loop/publish/connect/subscribe, tuc co I/O mang) CHI duoc goi tu
// networkTask ben trong mayapWebLinkUpdate(). Cac ham controlTask goi
// (mayapWebSet.../mayapWebConfirm...) khong bao gio goi thang vao PubSubClient
// - chung chi ghi vao hop thu webMux-protected roi networkTask tu doc va phat
// o vong lap ke tiep. Day la diem khac voi hmiSetConfig/hmiSetRuntime (hmi.h
// khong co I/O mang nen duoc phep ghi truc tiep); voi web, publish la I/O nen
// PHAI o lai trong networkTask de khong lam controlTask (vong dieu khien thuc)
// bi cham/block boi socket.
//
// Vi tri include: PHAI sau hmi.h (can queueCommand/startConfigSave/sanitizeConfig
// dang la ham global trong hmi.h) va TRUOC machine_control.h (MachineController
// se goi nguoc lai mayapWebSetConfig/mayapWebSetRuntime/mayapWebConfirmCommand/
// mayapWebConfirmConfigSave/mayapWebPushEventLog o vai diem no da cap nhat HMI).
//
// Thu vien can cai qua Library Manager (Arduino IDE) hoac platformio.ini:
//   - "PubSubClient" cua Nick O'Leary
//   - "ArduinoJson" cua Benoit Blanchon, BAT BUOC ban 7.x (dung lop
//     JsonDocument thong nhat cua v7; ban 6.x khong co API nay)
// ============================================================================

namespace MayapRealtimeInternal {

inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return static_cast<uint32_t>(now - then);
}
inline bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

constexpr uint8_t WEB_REQUEST_ID_CAPACITY = 40U;  // khop firmware/web (xem app.js)

// Mutex duy nhat bao ve toan bo hop thu trao doi giua controlTask (ghi
// mayapWebSet.../mayapWebConfirm...) va networkTask (doc trong
// mayapWebLinkUpdate). Cac vung critical section o day deu ngan (copy struct/
// vai truong), khong bao gio giu mutex qua mot loi goi I/O.
static portMUX_TYPE webMux = portMUX_INITIALIZER_UNLOCKED;

// --------------------------- Dinh danh thiet bi ------------------------------
// "MAP-" + 12 hex + null = 17 byte toi thieu (khop DEVICE_ID_RE trong app.js).
static char deviceId[20] = "";
static uint32_t bootId = 0;

inline void ensureIdentity() {
  if (deviceId[0]) return;
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "MAP-%02X%02X%02X%02X%02X%02X",
           static_cast<uint8_t>(mac >> 0), static_cast<uint8_t>(mac >> 8),
           static_cast<uint8_t>(mac >> 16), static_cast<uint8_t>(mac >> 24),
           static_cast<uint8_t>(mac >> 32), static_cast<uint8_t>(mac >> 40));
  bootId = esp_random();
  if (bootId == 0U) bootId = 1U;
}

// ------------------------------ MQTT client -----------------------------------
// netClient/mqtt chi duoc dung tu networkTask (mayapWebLinkUpdate va cac ham
// no goi truc tiep). Khong co ham nao khac trong file nay dung chung ngoai do.
#if MQTT_USE_TLS
static WiFiClientSecure netClient;
#else
static WiFiClient netClient;
#endif
static PubSubClient mqtt(netClient);

// Backoff RIENG cho MQTT, doc lap hoan toan voi backoff cua STA Wi-Fi
// (network_service.h) va Cloud Push (cloud_alert_link.h) - moi lop tu quan
// ly chu ky retry cua minh, khong anh huong lan nhau.
static BackoffTimer mqttBackoff{};

// ------------------------- Phien web (foreground/background) -----------------
// Chi doc/ghi tu networkTask (session den qua MQTT callback, cung chay trong
// mqtt.loop() goi tu networkTask) nen khong can mutex.
static bool webSessionActive = false;
static uint32_t webSessionExpiresAt = 0U;
static bool highPerfWifiApplied = false;  // tranh goi esp_wifi_set_ps lap lai
static uint32_t lastSnapshotPublishAt = 0U;

inline void applyWifiPowerMode(bool highPerformance) {
  if (highPerfWifiApplied == highPerformance) return;
  highPerfWifiApplied = highPerformance;
  // WIFI_PS_NONE: khong ngu, do tre thap nhat cho realtime. WIFI_PS_MIN_MODEM:
  // tiet kiem nang luong nhung van thuc day kip DTIM de nhan MQTT/lenh portal.
  esp_wifi_set_ps(highPerformance ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
  mayapSerialPrintf(false, "[WEBLINK] WiFi power mode -> %s\n",
                    highPerformance ? "PERFORMANCE" : "SAVE");
}

// ------------------------------- Chu de MQTT ----------------------------------
static char topicScratch[80];
inline const char *topicOf(const char *suffix) {
  snprintf(topicScratch, sizeof(topicScratch), "%s/%s/%s", MQTT_TOPIC_ROOT,
           deviceId, suffix);
  return topicScratch;
}

// --------------------------- Hop thu cau hinh/runtime --------------------------
// Ghi boi controlTask qua mayapWebSetConfig/mayapWebSetRuntime; doc boi
// networkTask. Bao ve boi webMux vi MachineConfig/MachineRuntime khong nho
// (vai chuc/vai tram byte) - copy trong critical section la ngan va an toan.
static MachineConfig knownConfig{};
static bool knownConfigValid = false;
static bool configDirty = false;      // co ban cap nhat can phat "config/reported"
static uint32_t webConfigRevision = 0U;

static MachineRuntime knownRuntime{};
static bool knownRuntimeValid = false;

// --------------------- Tuong quan lenh/luu cau hinh voi web --------------------
// pendingCommands/pendingConfigSave duoc GHI boi networkTask (khi nhan lenh tu
// web va queueCommand()/startConfigSave() thanh cong) va DOC+XOA boi ca hai
// task (networkTask khi het han, controlTask qua mayapWebConfirmCommand/
// mayapWebConfirmConfigSave khi MachineController xu ly xong) - can webMux.
struct PendingCommand {
  bool used = false;
  uint32_t commandId = 0;
  uint32_t queuedAt = 0;
  char requestId[WEB_REQUEST_ID_CAPACITY] = "";
};
static PendingCommand pendingCommands[COMMAND_QUEUE_SIZE];

// configSave (hmi.h) chi cho phep MOT giao dich luu dang cho tra loi tren toan
// he thong (ca web lan HMI dung chung 1 gate busy) nen khong can luu/doi chieu
// transactionId: pendingConfigSave.used dang bat nghia la giao dich HIEN CO
// chac chan la cua web, vi HMI khong the mo giao dich thu hai cung luc.
struct PendingConfigSave {
  bool used = false;
  uint32_t queuedAt = 0;
  char requestId[WEB_REQUEST_ID_CAPACITY] = "";
};
static PendingConfigSave pendingConfigSave;

// ------------------------------- Hop thu phat ACK -------------------------------
// mayapWebConfirmCommand/mayapWebConfirmConfigSave chay tren controlTask va
// KHONG duoc goi thang vao PubSubClient (I/O mang) - chung chi day ket qua vao
// day, networkTask se rut ra va publish that su.
struct AckOutboxItem {
  bool used = false;
  char requestId[WEB_REQUEST_ID_CAPACITY] = "";
  char result[16] = "";
  char message[64] = "";
};
static AckOutboxItem ackOutbox[COMMAND_QUEUE_SIZE + 2U];

inline void enqueueAckLocked(const char *requestId, const char *result,
                             const char *message) {
  if (!requestId || !requestId[0]) return;
  for (AckOutboxItem &slot : ackOutbox) {
    if (slot.used) continue;
    slot.used = true;
    snprintf(slot.requestId, sizeof(slot.requestId), "%s", requestId);
    snprintf(slot.result, sizeof(slot.result), "%s", result ? result : "");
    snprintf(slot.message, sizeof(slot.message), "%s", message ? message : "");
    return;
  }
  // Outbox day (rat hiem, toi da 6 ack cung luc): bo qua, web se tu timeout
  // va coi lenh la "chua phan hoi" - khong anh huong an toan thiet bi.
}

// -------------------------- Hop thu nhat ky (event log) -------------------------
// mayapWebPushEventLog() chay tren controlTask; chi sao chep snapshot vao day,
// networkTask moi thuc su lap va publish tung muc (co I/O mang).
static HmiEventSnapshot pendingEventSnapshot{};
static bool eventSnapshotDirty = false;
static uint32_t lastPublishedEventSequence = 0U;

// -------------------------------- Publish -------------------------------------
// Tat ca ham publishXxx() ben duoi chi duoc goi tu networkTask.
inline void publishJson(const char *suffix, const JsonDocument &doc,
                        bool retain) {
  if (!mqtt.connected()) return;
  char buffer[1024];  // config/reported (28 truong) la payload lon nhat, ~700-900 byte
  const size_t length = serializeJson(doc, buffer, sizeof(buffer));
  if (length == 0U || length >= sizeof(buffer)) return;
  mqtt.publish(topicOf(suffix), reinterpret_cast<const uint8_t *>(buffer),
               static_cast<unsigned int>(length), retain);
}

inline void publishPresence(bool online) {
  JsonDocument doc;
  doc["online"] = online;
  doc["bootId"] = bootId;
  doc["ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : "";
  doc["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  doc["fw"] = MAYAP_FIRMWARE_VERSION;
  doc["hw"] = MAYAP_HARDWARE_REVISION;
  publishJson("presence", doc, true);
}

inline void publishConfigReport(const MachineConfig &cfg, uint32_t revision) {
  JsonDocument doc;
  doc["v"] = 1;
  doc["bootId"] = bootId;
  doc["revision"] = revision;
  JsonObject c = doc["config"].to<JsonObject>();
  c["targetTemp"] = cfg.targetTemp;
  c["tempHysteresis"] = cfg.tempHysteresis;
  c["lowTempAlarm"] = cfg.lowTempAlarm;
  c["highTempAlarm"] = cfg.highTempAlarm;
  c["emergencyTemp"] = cfg.emergencyTemp;
  c["kp"] = cfg.kp;
  c["ki"] = cfg.ki;
  c["kd"] = cfg.kd;
  c["lowHumidityAlarm"] = cfg.lowHumidityAlarm;
  c["ventOnTemp"] = cfg.ventOnTemp;
  c["ventOffTemp"] = cfg.ventOffTemp;
  c["tempOffset"] = cfg.tempOffset;
  c["humidityOffset"] = cfg.humidityOffset;
  c["pidCycleSec"] = cfg.pidCycleSec;
  c["humidityAlarmDelaySec"] = cfg.humidityAlarmDelaySec;
  c["turnIntervalMin"] = cfg.turnIntervalMin;
  c["turnMaxRunSec"] = cfg.turnMaxRunSec;
  c["powerRestoreDelaySec"] = cfg.powerRestoreDelaySec;
  c["sensorTimeoutSec"] = cfg.sensorTimeoutSec;
  c["maxHeaterPower"] = cfg.maxHeaterPower;
  c["totalIncubationDays"] = cfg.totalIncubationDays;
  c["circulationFanEnabled"] = cfg.circulationFanEnabled;
  c["turningEnabled"] = cfg.turningEnabled;
  // Ten field khac firmware (autoResumeOnPowerLoss) vi web da dung ten nay
  // truoc: giu nguyen giao thuc web, chi anh xa ten trong firmware.
  c["autoResumeAfterPower"] = cfg.autoResumeOnPowerLoss;
  c["allowHeatWithoutBatch"] = cfg.allowHeatWithoutBatch;
  c["alarmEnabled"] = cfg.alarmEnabled;
  c["controlMode"] = static_cast<uint8_t>(cfg.controlMode);
  c["nextDirection"] = static_cast<uint8_t>(cfg.nextDirection);
  publishJson("config/reported", doc, true);
}

inline void publishSnapshot(const MachineRuntime &rt, uint32_t revision) {
  JsonDocument doc;
  doc["bootId"] = bootId;
  doc["revision"] = revision;
  JsonObject r = doc["runtime"].to<JsonObject>();
  r["temperature"] = rt.temperature;
  r["humidity"] = rt.humidity;
  r["machineState"] = rt.machineState;
  r["batchRunning"] = rt.batchRunning;
  r["currentDay"] = rt.currentDay;
  r["heaterOn"] = rt.heaterOn;
  r["heaterPower"] = rt.heaterPower;
  r["circulationFanOn"] = rt.circulationFanOn;
  r["ventFanOn"] = rt.ventFanOn;
  r["turnState"] = static_cast<uint8_t>(rt.turnState);
  r["nextTurnMinutes"] = rt.nextTurnMinutes;
  r["autoTuneState"] = static_cast<uint8_t>(rt.autoTuneState);
  r["autoTuneProgress"] = rt.autoTuneProgress;
  r["resumeConfirmationRequired"] = rt.resumeConfirmationRequired;
  publishJson("snapshot", doc, false);
}

inline void publishAck(const char *requestId, const char *result,
                       const char *message) {
  if (!requestId || !requestId[0]) return;
  JsonDocument doc;
  doc["requestId"] = requestId;
  doc["bootId"] = bootId;
  doc["result"] = result;
  doc["message"] = message ? message : "";
  publishJson("ack", doc, false);
}

inline void publishLogEntry(const HmiEventItem &item) {
  JsonDocument doc;
  doc["sequence"] = item.sequence;
  doc["epoch"] = item.epoch;
  doc["code"] = item.code;
  doc["value"] = item.value;
  doc["type"] = item.type;
  publishJson("log", doc, false);
}

// --------------------------- Xu ly ban tin den (networkTask) --------------------
inline HmiCommandType mapCommandAction(const char *action) {
  if (!action) return HmiCommandType::None;
  if (!strcmp(action, "batch_start")) return HmiCommandType::BatchStart;
  if (!strcmp(action, "batch_stop")) return HmiCommandType::BatchStop;
  if (!strcmp(action, "resume_yes")) return HmiCommandType::ResumeYes;
  if (!strcmp(action, "resume_no")) return HmiCommandType::ResumeNo;
  if (!strcmp(action, "autotune_start")) return HmiCommandType::AutoTuneStart;
  return HmiCommandType::None;
}

static uint32_t lastCommandSequence = 0U;
static char lastCommandRequestId[WEB_REQUEST_ID_CAPACITY] = "";

inline void handleCommandMessage(const JsonDocument &doc) {
  const char *requestId = doc["requestId"] | "";
  const uint32_t sequence = doc["sequence"] | 0UL;
  const char *action = doc["action"] | "";

  // Chong lap: web co the phat lai cung mot lenh khi mat goi ACK. Sequence
  // tang dan tu web; requestId trung cung la dau hieu lap.
  if (requestId[0] && !strcmp(requestId, lastCommandRequestId)) {
    publishAck(requestId, "duplicate", "");
    return;
  }
  if (sequence != 0U && lastCommandSequence != 0U &&
      sequence <= lastCommandSequence) {
    publishAck(requestId, "stale", "");
    return;
  }

  const HmiCommandType type = mapCommandAction(action);
  if (type == HmiCommandType::None) {
    publishAck(requestId, "unsupported", "");
    return;
  }

  uint32_t commandId = 0U;
  const uint16_t validForMs = type == HmiCommandType::AutoTuneStart
      ? COMMAND_AUTOTUNE_VALID_MS : COMMAND_DEFAULT_VALID_MS;
  const bool queued = queueCommand(type, validForMs, 0U, AlarmNone, &commandId);
  if (!queued) {
    publishAck(requestId, "busy", "");
    return;
  }

  lastCommandSequence = sequence;
  snprintf(lastCommandRequestId, sizeof(lastCommandRequestId), "%s", requestId);

  portENTER_CRITICAL(&webMux);
  for (PendingCommand &slot : pendingCommands) {
    if (slot.used) continue;
    slot.used = true;
    slot.commandId = commandId;
    slot.queuedAt = millis();
    snprintf(slot.requestId, sizeof(slot.requestId), "%s", requestId);
    break;
  }
  portEXIT_CRITICAL(&webMux);
  publishAck(requestId, "accepted", "");
}

inline void handleConfigSetMessage(const JsonDocument &doc) {
  const char *requestId = doc["requestId"] | "";
  const uint32_t revision = doc["revision"] | 0UL;

  portENTER_CRITICAL(&webMux);
  const bool busy = pendingConfigSave.used;
  const bool haveBase = knownConfigValid;
  MachineConfig candidate = knownConfig;
  portEXIT_CRITICAL(&webMux);

  if (busy) {
    publishAck(requestId, "busy", "");
    return;
  }
  if (!haveBase) {
    publishAck(requestId, "invalid", "CHUA CO CAU HINH GOC");
    return;
  }
  JsonVariantConst configObj = doc["config"];
  if (configObj.isNull()) {
    publishAck(requestId, "invalid", "THIEU CONFIG");
    return;
  }

  candidate.targetTemp = configObj["targetTemp"] | candidate.targetTemp;
  candidate.tempHysteresis = configObj["tempHysteresis"] | candidate.tempHysteresis;
  candidate.lowTempAlarm = configObj["lowTempAlarm"] | candidate.lowTempAlarm;
  candidate.highTempAlarm = configObj["highTempAlarm"] | candidate.highTempAlarm;
  candidate.emergencyTemp = configObj["emergencyTemp"] | candidate.emergencyTemp;
  candidate.kp = configObj["kp"] | candidate.kp;
  candidate.ki = configObj["ki"] | candidate.ki;
  candidate.kd = configObj["kd"] | candidate.kd;
  candidate.lowHumidityAlarm = configObj["lowHumidityAlarm"] | candidate.lowHumidityAlarm;
  candidate.ventOnTemp = configObj["ventOnTemp"] | candidate.ventOnTemp;
  candidate.ventOffTemp = configObj["ventOffTemp"] | candidate.ventOffTemp;
  candidate.tempOffset = configObj["tempOffset"] | candidate.tempOffset;
  candidate.humidityOffset = configObj["humidityOffset"] | candidate.humidityOffset;
  candidate.pidCycleSec = configObj["pidCycleSec"] | candidate.pidCycleSec;
  candidate.humidityAlarmDelaySec =
      configObj["humidityAlarmDelaySec"] | candidate.humidityAlarmDelaySec;
  candidate.turnIntervalMin = configObj["turnIntervalMin"] | candidate.turnIntervalMin;
  candidate.turnMaxRunSec = configObj["turnMaxRunSec"] | candidate.turnMaxRunSec;
  candidate.powerRestoreDelaySec =
      configObj["powerRestoreDelaySec"] | candidate.powerRestoreDelaySec;
  candidate.sensorTimeoutSec = configObj["sensorTimeoutSec"] | candidate.sensorTimeoutSec;
  candidate.maxHeaterPower = configObj["maxHeaterPower"] | candidate.maxHeaterPower;
  candidate.totalIncubationDays =
      configObj["totalIncubationDays"] | candidate.totalIncubationDays;
  candidate.circulationFanEnabled =
      configObj["circulationFanEnabled"] | candidate.circulationFanEnabled;
  candidate.turningEnabled = configObj["turningEnabled"] | candidate.turningEnabled;
  candidate.autoResumeOnPowerLoss =
      configObj["autoResumeAfterPower"] | candidate.autoResumeOnPowerLoss;
  candidate.allowHeatWithoutBatch =
      configObj["allowHeatWithoutBatch"] | candidate.allowHeatWithoutBatch;
  candidate.alarmEnabled = configObj["alarmEnabled"] | candidate.alarmEnabled;
  candidate.controlMode = static_cast<ControlMode>(
      configObj["controlMode"] | static_cast<uint8_t>(candidate.controlMode));
  candidate.nextDirection = static_cast<TurnDirection>(
      configObj["nextDirection"] | static_cast<uint8_t>(candidate.nextDirection));

  sanitizeConfig(candidate);

  if (!startConfigSave(candidate)) {
    publishAck(requestId, "busy", "");
    return;
  }

  portENTER_CRITICAL(&webMux);
  webConfigRevision = revision > webConfigRevision ? revision : webConfigRevision + 1U;
  pendingConfigSave.used = true;
  pendingConfigSave.queuedAt = millis();
  snprintf(pendingConfigSave.requestId, sizeof(pendingConfigSave.requestId), "%s",
           requestId);
  portEXIT_CRITICAL(&webMux);
  publishAck(requestId, "accepted", "");
}

// Gia dinh MOT trinh duyet dang theo doi may tai 1 thoi diem (dung thuc te
// cua san pham); neu nhieu tab/thiet bi web cung mo, "active" cua nguoi gui
// SAU CUNG se thang - khong co dieu phoi nhieu client dong thoi.
inline void handleSessionMessage(const JsonDocument &doc) {
  const bool active = doc["active"] | false;
  uint32_t ttlMs = doc["ttlMs"] | 0UL;
  const bool sync = doc["sync"] | false;
  const uint32_t now = millis();

  if (active) {
    if (ttlMs == 0U || ttlMs > WEB_SESSION_MAX_TTL_MS) ttlMs = WEB_SESSION_MAX_TTL_MS;
    webSessionActive = true;
    webSessionExpiresAt = now + ttlMs;
    applyWifiPowerMode(true);
  } else {
    webSessionActive = false;
    webSessionExpiresAt = now;
    applyWifiPowerMode(false);
  }

  if (sync) {
    portENTER_CRITICAL(&webMux);
    const bool haveConfig = knownConfigValid;
    const MachineConfig cfg = knownConfig;
    const uint32_t revision = webConfigRevision;
    portEXIT_CRITICAL(&webMux);
    if (haveConfig) publishConfigReport(cfg, revision);
    lastSnapshotPublishAt = 0U;  // ep publish snapshot ngay trong vong lap toi
  }
}

inline void mqttMessageCallback(char *topic, uint8_t *payload,
                                unsigned int length) {
  // Payload config/set co the toi ~700-800 byte (28 truong config + bao boc
  // requestId/revision). Gioi han khop voi mqtt.setBufferSize() o mayapWebLinkBegin().
  if (length >= 1536U) return;  // vuot qua kha nang buffer hop ly, bo qua an toan
  char buffer[1536];
  memcpy(buffer, payload, length);
  buffer[length] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, buffer, length) != DeserializationError::Ok) return;

  // "config/set" co dau '/' o giua nen phai kiem tra ca doan, khong chi ky tu
  // sau dau '/' cuoi cung (se chi ra "set", trung voi cac topic khac khong).
  if (strstr(topic, "/config/set")) {
    handleConfigSetMessage(doc);
    return;
  }
  const char *suffix = strrchr(topic, '/');
  if (!suffix) return;
  ++suffix;
  if (!strcmp(suffix, "command")) {
    handleCommandMessage(doc);
  } else if (!strcmp(suffix, "session")) {
    handleSessionMessage(doc);
  }
}

// ------------------------------ Vong doi ket noi -------------------------------
inline void subscribeAll() {
  mqtt.subscribe(topicOf("config/set"));
  mqtt.subscribe(topicOf("command"));
  mqtt.subscribe(topicOf("session"));
}

inline void attemptConnect(uint32_t now) {
  if (!mqttBackoff.ready(now)) return;

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "esp-%s", deviceId);
  char willTopic[80];
  snprintf(willTopic, sizeof(willTopic), "%s/%s/presence", MQTT_TOPIC_ROOT,
           deviceId);
  const char *willMessage = "{\"online\":false}";

  const char *user = MQTT_USERNAME[0] ? MQTT_USERNAME : nullptr;
  const char *pass = MQTT_PASSWORD[0] ? MQTT_PASSWORD : nullptr;
  const bool ok = mqtt.connect(clientId, user, pass, willTopic, 0, true,
                               willMessage, true);
  if (!ok) {
    mqttBackoff.onFailure(now);
    mayapSerialPrintf(false, "[WEBLINK] MQTT connect that bai state=%d, thu lai sau %lums\n",
                      mqtt.state(),
                      static_cast<unsigned long>(mqttBackoff.nextAttemptAt - now));
    return;
  }
  mqttBackoff.onSuccess();
  subscribeAll();
  publishPresence(true);
  portENTER_CRITICAL(&webMux);
  const bool haveConfig = knownConfigValid;
  const MachineConfig cfg = knownConfig;
  const uint32_t revision = webConfigRevision;
  portEXIT_CRITICAL(&webMux);
  if (haveConfig) publishConfigReport(cfg, revision);
  lastSnapshotPublishAt = 0U;
  mayapSerialPrintf(false, "[WEBLINK] MQTT da ket noi %s\n", deviceId);
}

inline void expirePendingCommands(uint32_t now) {
  char requestIdsToExpire[COMMAND_QUEUE_SIZE][WEB_REQUEST_ID_CAPACITY];
  uint8_t expireCount = 0U;
  bool configExpired = false;
  char configRequestId[WEB_REQUEST_ID_CAPACITY] = "";

  portENTER_CRITICAL(&webMux);
  for (PendingCommand &slot : pendingCommands) {
    if (!slot.used) continue;
    if (elapsedMs(now, slot.queuedAt) < WEB_COMMAND_ACK_TIMEOUT_MS) continue;
    snprintf(requestIdsToExpire[expireCount], WEB_REQUEST_ID_CAPACITY, "%s",
             slot.requestId);
    ++expireCount;
    slot.used = false;
  }
  if (pendingConfigSave.used &&
      elapsedMs(now, pendingConfigSave.queuedAt) >= WEB_CONFIG_SAVE_ACK_TIMEOUT_MS) {
    configExpired = true;
    snprintf(configRequestId, sizeof(configRequestId), "%s",
             pendingConfigSave.requestId);
    pendingConfigSave.used = false;
  }
  portEXIT_CRITICAL(&webMux);

  for (uint8_t i = 0; i < expireCount; ++i) publishAck(requestIdsToExpire[i], "expired", "");
  if (configExpired) publishAck(configRequestId, "expired", "");
}

inline void drainAckOutbox() {
  AckOutboxItem items[COMMAND_QUEUE_SIZE + 2U];
  uint8_t count = 0U;
  portENTER_CRITICAL(&webMux);
  for (AckOutboxItem &slot : ackOutbox) {
    if (!slot.used) continue;
    items[count] = slot;
    slot.used = false;
    ++count;
  }
  portEXIT_CRITICAL(&webMux);
  for (uint8_t i = 0; i < count; ++i) {
    publishAck(items[i].requestId, items[i].result, items[i].message);
  }
}

inline void serviceSessionTimeout(uint32_t now) {
  if (webSessionActive && timeReached(now, webSessionExpiresAt)) {
    // Web khong gui lai "active" dung han (dong tab/mat mang dot ngot): tu
    // dong coi nhu khong con ai theo doi, chuyen ve tiet kiem nang luong.
    webSessionActive = false;
    applyWifiPowerMode(false);
  }
}

// Nguon "can hieu nang cao" duy nhat: phien web dang active HOAC cong doi
// Wi-Fi tren HMI dang mo (AP+STA dang bat, dang phat song MAYAP-XXXX). Day
// la noi DUY NHAT trong toan bo firmware goi esp_wifi_set_ps() - luon di qua
// applyWifiPowerMode() de bien dem highPerfWifiApplied khong bao gio lech
// voi trang thai phan cung that (neu co noi thu hai tu goi thang, bien dem
// se "tuong" sai va bo qua lan dong bo sau, ket qua la giu nham che do).
// Chay MOI vong lap ke ca khi STA dang tat (dung luc cong doi Wi-Fi vua ngat
// STA de bat AP on dinh - xem network_service.h::portalBeginStarting), nen
// duoc goi truoc moi nhanh return som cua mayapWebLinkUpdate().
inline void serviceWifiPowerMode() {
  const WifiPortalStatus portal = mayapGetWifiPortalStatus();
  const bool portalActive = portal.state != WifiPortalState::Idle;
  applyWifiPowerMode(webSessionActive || portalActive);
}

inline void serviceConfigPublish() {
  portENTER_CRITICAL(&webMux);
  const bool dirty = configDirty;
  configDirty = false;
  const MachineConfig cfg = knownConfig;
  const uint32_t revision = webConfigRevision;
  portEXIT_CRITICAL(&webMux);
  if (dirty) publishConfigReport(cfg, revision);
}

inline void serviceSnapshotPublish(uint32_t now) {
  const uint32_t interval = webSessionActive ? WEB_SNAPSHOT_ACTIVE_INTERVAL_MS
                                             : WEB_SNAPSHOT_IDLE_INTERVAL_MS;
  if (!timeReached(now, lastSnapshotPublishAt + interval)) return;
  lastSnapshotPublishAt = now;
  portENTER_CRITICAL(&webMux);
  const bool valid = knownRuntimeValid;
  const MachineRuntime rt = knownRuntime;
  const uint32_t revision = webConfigRevision;
  portEXIT_CRITICAL(&webMux);
  if (valid) publishSnapshot(rt, revision);
}

inline void serviceEventLogPublish() {
  portENTER_CRITICAL(&webMux);
  const bool dirty = eventSnapshotDirty;
  const HmiEventSnapshot snapshot = pendingEventSnapshot;
  eventSnapshotDirty = false;
  portEXIT_CRITICAL(&webMux);
  if (!dirty || snapshot.sourceSequence == lastPublishedEventSequence) return;
  // items[0] la moi nhat; chi phat nhung su kien co sequence > lan phat truoc,
  // toi da vai muc moi lan goi de khong lam nghen vong lap networkTask.
  uint8_t published = 0U;
  for (uint8_t i = 0; i < snapshot.count && published < 5U; ++i) {
    const HmiEventItem &item = snapshot.items[i];
    if (item.sequence <= lastPublishedEventSequence) break;
    publishLogEntry(item);
    ++published;
  }
  lastPublishedEventSequence = snapshot.sourceSequence;
}

}  // namespace MayapRealtimeInternal

// ================================ API cong khai ================================

inline void mayapWebLinkBegin() {
  using namespace MayapRealtimeInternal;
  ensureIdentity();
  mqtt.setBufferSize(1536);
  mqtt.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqtt.setCallback(mqttMessageCallback);
#if MQTT_USE_TLS
  netClient.setInsecure();  // khong xac thuc CA: xem ghi chu o dau file cho ban thuong mai
#endif
  applyWifiPowerMode(false);
}

// Chi duoc goi tu networkTask (vong lap khong blocking, giong het
// mayapNetworkUpdate ma no chay canh).
inline void mayapWebLinkUpdate(uint32_t now) {
  using namespace MayapRealtimeInternal;
  // Phai chay TRUOC moi nhanh return ben duoi: cong doi Wi-Fi co the dang mo
  // ngay ca khi STA (va vi vay MQTT) dang tat han, nhung AP van can duoc giu
  // WIFI_PS_NONE de phat song on dinh trong luc do.
  serviceWifiPowerMode();

  const NetworkStatus status = mayapGetNetworkStatus();
  const bool staOnline = status.requestedMode == ConnectivityMode::Online &&
                         status.connected;
  if (!staOnline) {
    if (mqtt.connected()) mqtt.disconnect();
    // STA vua mat/chua co: cho phep lan ket noi MQTT tiep theo (khi STA co
    // lai) thu ngay, khong ke thua buoc backoff cua lan mat mang truoc do.
    mqttBackoff.reset(now);
    return;
  }
  if (!mqtt.connected()) {
    attemptConnect(now);
    return;
  }
  mqtt.loop();
  expirePendingCommands(now);
  drainAckOutbox();
  serviceSessionTimeout(now);
  serviceConfigPublish();
  serviceSnapshotPublish(now);
  serviceEventLogPublish();
}

// ------------------------- Hooks goi tu controlTask (machine_control.h) --------
// Tat ca cac ham duoi day CHI ghi vao hop thu webMux-protected, khong bao gio
// goi vao PubSubClient/WiFiClient (I/O mang phai o lai networkTask).

inline void mayapWebSetRuntime(const MachineRuntime &runtime) {
  using namespace MayapRealtimeInternal;
  portENTER_CRITICAL(&webMux);
  knownRuntime = runtime;
  knownRuntimeValid = true;
  portEXIT_CRITICAL(&webMux);
}

inline void mayapWebSetConfig(const MachineConfig &config) {
  using namespace MayapRealtimeInternal;
  portENTER_CRITICAL(&webMux);
  const bool changed = !knownConfigValid ||
      memcmp(&config, &knownConfig, sizeof(MachineConfig)) != 0;
  knownConfig = config;
  knownConfigValid = true;
  if (changed) {
    configDirty = true;
    // pendingConfigSave.used == true nghia la thay doi nay la ket qua truc
    // tiep cua mot config/set tu web: revision da duoc gan san trong
    // handleConfigSetMessage(), khong tang them de khop dung "revision" ma
    // web dang cho trong pending.revision. Ngoai ra (HMI sua tay...) thi tang.
    if (webConfigRevision == 0U) webConfigRevision = 1U;
    else if (!pendingConfigSave.used) ++webConfigRevision;
  }
  portEXIT_CRITICAL(&webMux);
}

inline void mayapWebConfirmCommand(uint32_t commandId, bool ok,
                                   const char *message) {
  using namespace MayapRealtimeInternal;
  portENTER_CRITICAL(&webMux);
  for (PendingCommand &slot : pendingCommands) {
    if (!slot.used || slot.commandId != commandId) continue;
    enqueueAckLocked(slot.requestId, ok ? "applied" : "rejected", message);
    slot.used = false;
    break;
  }
  portEXIT_CRITICAL(&webMux);
}

inline void mayapWebConfirmConfigSave(uint32_t transactionId, bool ok,
                                      const MachineConfig *stored) {
  using namespace MayapRealtimeInternal;
  (void)transactionId;
  (void)stored;  // config moi da/se toi qua mayapWebSetConfig() tu cung noi goi
  portENTER_CRITICAL(&webMux);
  if (pendingConfigSave.used) {
    enqueueAckLocked(pendingConfigSave.requestId, ok ? "applied" : "rejected",
                     ok ? "" : "LUU CAU HINH BI TU CHOI");
    pendingConfigSave.used = false;
  }
  portEXIT_CRITICAL(&webMux);
}

inline void mayapWebPushEventLog(const HmiEventSnapshot &snapshot) {
  using namespace MayapRealtimeInternal;
  portENTER_CRITICAL(&webMux);
  pendingEventSnapshot = snapshot;
  eventSnapshotDirty = true;
  portEXIT_CRITICAL(&webMux);
}

// Trang thai SONG cua kenh MQTT/Web (bo sung cho [NET] cua printStatus() vi
// do chi bao WiFi/STA, khong bao rieng MQTT da connect() hay chua) - dung cho
// lenh Serial CONFIG.
inline void mayapPrintWebStatus() {
  using namespace MayapRealtimeInternal;
  mayapSerialPrintf(false,
      "[WEB] mqtt_connected=%u web_session_active=%u mqtt_backoff_step=%u/%u\n",
      mqtt.connected(), webSessionActive,
      static_cast<unsigned>(mqttBackoff.step),
      static_cast<unsigned>(BACKOFF_STEP_COUNT - 1U));
}
