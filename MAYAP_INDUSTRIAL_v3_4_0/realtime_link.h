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
#include <mbedtls/md.h>

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
static bool mqttTlsReady = !MQTT_USE_TLS;

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

// Danh sach nhac nho tuy chinh (v3.7.0) - cung mailbox pattern voi knownConfig
// o tren, nhung DOC LAP hoan toan (khong dan xen voi luu cau hinh dieu khien).
static ReminderSet knownReminders{};
static bool knownRemindersValid = false;
static bool remindersDirty = false;
static uint32_t webRemindersRevision = 0U;

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

// Giong het PendingConfigSave nhung cho "reminders/set" - gate .used RIENG,
// khong dung chung voi pendingConfigSave (2 loai luu doc lap, khong can
// chan lan nhau - xem ghi chu ReminderSaveTransaction trong hmi.h).
static PendingConfigSave pendingReminderSave;

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

// --------------------- Lich su nhiet do AT24C32 -> Web ----------------------
// Chi doc EEPROM khi web yeu cau; moi vong networkTask chi phat toi da 12
// bucket de khong chiem I2C/MQTT lau. Request duoc HMAC giong command/config.
static bool historyResponsePending = false;
static uint16_t historyWindowMinutes = 30U;
static uint16_t historyCursor = 0U;
static uint16_t historyCandidateCount = 0U;
static uint32_t historySnapshotEpoch = 0U;
static char historyRequestId[WEB_REQUEST_ID_CAPACITY] = "";

// -------------------------------- Publish -------------------------------------
// Tat ca ham publishXxx() ben duoi chi duoc goi tu networkTask.
inline void publishJson(const char *suffix, const JsonDocument &doc,
                        bool retain) {
  if (!mqtt.connected()) return;
  // config/reported (38 truong ke ca 8 truong "Nang cao") la payload lon
  // nhat, toi ~1000-1050 byte o truong hop xau nhat (so am/thap phan dai) -
  // qua sat gioi han 1024 cu, co the IM LANG khong gui duoc tuy gia tri
  // (length >= sizeof(buffer) bi loai ngay duoi). Nang len 1536 (khop
  // mqtt.setBufferSize() o mayapWebLinkBegin()) de co du du.
  char buffer[1536];
  const size_t length = serializeJson(doc, buffer, sizeof(buffer));
  if (length == 0U || length >= sizeof(buffer)) return;
  mqtt.publish(topicOf(suffix), reinterpret_cast<const uint8_t *>(buffer),
               static_cast<unsigned int>(length), retain);
}

inline void handleHistoryRequestMessage(const JsonDocument &doc) {
  const char *requestId = doc["requestId"] | "";
  if (!requestId[0]) return;
  uint16_t minutes = static_cast<uint16_t>(doc["minutes"] | 30U);
  if (minutes < 5U) minutes = 5U;
  if (minutes > 1440U) minutes = 1440U;

  const uint32_t epoch = mayapTemperatureHistoryLatestEpoch();
  const uint32_t interval = TEMP_HISTORY_SAMPLE_SEC;
  uint32_t candidates = (static_cast<uint32_t>(minutes) * 60UL + interval - 1UL) / interval + 1UL;
  if (candidates > TEMP_HISTORY_SLOT_COUNT) candidates = TEMP_HISTORY_SLOT_COUNT;

  historyWindowMinutes = minutes;
  historyCursor = 0U;
  historyCandidateCount = static_cast<uint16_t>(candidates);
  historySnapshotEpoch = epoch;
  snprintf(historyRequestId, sizeof(historyRequestId), "%s", requestId);
  historyResponsePending = true;
}

inline void serviceHistoryResponse() {
  if (!historyResponsePending || !mqtt.connected()) return;

  JsonDocument doc;
  doc["v"] = 1;
  doc["bootId"] = bootId;
  doc["requestId"] = historyRequestId;
  doc["windowMin"] = historyWindowMinutes;
  doc["intervalSec"] = TEMP_HISTORY_SAMPLE_SEC;
  doc["cursor"] = historyCursor;
  JsonArray samples = doc["samples"].to<JsonArray>();

  if (historySnapshotEpoch == 0U || historyCandidateCount == 0U) {
    doc["done"] = true;
    publishJson("history/reported", doc, false);
    historyResponsePending = false;
    return;
  }

  const uint32_t nowBucket = historySnapshotEpoch / TEMP_HISTORY_SAMPLE_SEC;
  const uint32_t firstBucket = nowBucket >= historyCandidateCount - 1U
      ? nowBucket - (historyCandidateCount - 1U) : 0U;
  const uint16_t end = static_cast<uint16_t>(
      min<uint32_t>(historyCandidateCount, static_cast<uint32_t>(historyCursor) + 12U));

  for (uint16_t i = historyCursor; i < end; ++i) {
    const uint32_t absoluteBucket = firstBucket + i;
    MayapTemperatureHistoryPoint point{};
    if (!mayapTemperatureHistoryReadBucket(absoluteBucket, point)) continue;
    JsonArray row = samples.add<JsonArray>();
    row.add(point.epoch);
    row.add(static_cast<float>(point.temperatureX10) / 10.0f);
  }

  historyCursor = end;
  const bool done = historyCursor >= historyCandidateCount;
  doc["done"] = done;
  publishJson("history/reported", doc, false);
  if (done) historyResponsePending = false;
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
  c["humidifierInstalled"] = cfg.humidifierInstalled;
  c["humidifierEnabled"] = cfg.humidifierEnabled;
  c["targetHumidity"] = cfg.targetHumidity;
  c["ventOnTemp"] = cfg.ventOnTemp;
  c["ventOffTemp"] = cfg.ventOffTemp;
  c["ventScheduleEnabled"] = cfg.ventScheduleEnabled;
  c["ventScheduleCount"] = cfg.ventScheduleCount;
  c["ventScheduleDurationMin"] = cfg.ventScheduleDurationMin;
  c["ventScheduleHour1"] = cfg.ventScheduleHour1;
  c["ventScheduleHour2"] = cfg.ventScheduleHour2;
  c["ventScheduleHour3"] = cfg.ventScheduleHour3;
  c["ventScheduleHour4"] = cfg.ventScheduleHour4;
  c["ventScheduleHour5"] = cfg.ventScheduleHour5;
  c["ventScheduleHour6"] = cfg.ventScheduleHour6;
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
  c["manualTurnReanchorsSchedule"] = cfg.manualTurnReanchorsSchedule;
  c["sirenSelfTestEnabled"] = cfg.sirenSelfTestEnabled;
  // Ten field khac firmware (autoResumeOnPowerLoss) vi web da dung ten nay
  // truoc: giu nguyen giao thuc web, chi anh xa ten trong firmware.
  c["autoResumeAfterPower"] = cfg.autoResumeOnPowerLoss;
  c["allowHeatWithoutBatch"] = cfg.allowHeatWithoutBatch;
  c["alarmEnabled"] = cfg.alarmEnabled;
  c["lightAfterBatchAlarmEnabled"] = cfg.lightAfterBatchAlarmEnabled;
  c["highTempAlarmWithoutBatch"] = cfg.highTempAlarmWithoutBatch;
  c["controlMode"] = static_cast<uint8_t>(cfg.controlMode);
  c["nextDirection"] = static_cast<uint8_t>(cfg.nextDirection);
  // 8 truong "Nang cao" (schema 8, xem config.h) - THIEU o day tu luc them
  // tinh nang "Nang cao" la LOI GOC gay web KHONG BAO GIO dong bo duoc: web
  // (CONFIG_KEYS trong app.js) doi hoi DU CA 38 truong moi coi 1 goi config/
  // reported la hop le (validateFullConfig), thieu dung 8 truong nay khien
  // MOI lan bao cau hinh tu ESP32 bi web tu choi vinh vien - khong lien quan
  // gi den mang/broker, day la loi giao thuc that su.
  c["heaterStuckMinRiseC"] = cfg.heaterStuckMinRiseC;
  c["heaterStuckDurationSec"] = cfg.heaterStuckDurationSec;
  c["tempRateLimitC"] = cfg.tempRateLimitC;
  c["tempRateWindowSec"] = cfg.tempRateWindowSec;
  c["tempOscillationCrossLimit"] = cfg.tempOscillationCrossLimit;
  c["tempOscillationWindowSec"] = cfg.tempOscillationWindowSec;
  c["autotuneRelayPowerPercent"] = cfg.autotuneRelayPowerPercent;
  c["autotuneBandC"] = cfg.autotuneBandC;
  publishJson("config/reported", doc, true);
}

// Danh sach nhac nho tuy chinh hien co - web dung de dong bo lai form khi mo
// trang/doi thiet bi (giong het vai tro cua "config/reported" voi MachineConfig).
inline void publishReminderReport(const ReminderSet &reminders, uint32_t revision) {
  JsonDocument doc;
  doc["v"] = 1;
  doc["bootId"] = bootId;
  doc["revision"] = revision;
  JsonArray items = doc["reminders"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_CUSTOM_REMINDERS; ++i) {
    if (reminders.items[i].day == 0U) continue;  // O TRONG - khong gui
    JsonObject item = items.add<JsonObject>();
    item["day"] = reminders.items[i].day;
    item["label"] = reminders.items[i].label;
  }
  publishJson("reminders/reported", doc, true);
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
  r["humidifierOn"] = rt.humidifierOn;
  r["lightOn"] = rt.lightOn;
  r["sirenOn"] = rt.sirenOn;
  r["turnState"] = static_cast<uint8_t>(rt.turnState);
  r["nextTurnMinutes"] = rt.nextTurnMinutes;
  r["autoTuneState"] = static_cast<uint8_t>(rt.autoTuneState);
  r["autoTuneProgress"] = rt.autoTuneProgress;
  r["resumeConfirmationRequired"] = rt.resumeConfirmationRequired;
  r["batchOverdueConfirmationPending"] = rt.batchOverdueConfirmationPending;
  // Danh sach loi dang active, da sap xep theo displayPriority giam dan boi
  // FaultManager::copyActiveForHmi() - phan tu [0] la loi quan trong nhat.
  // Web dung de to mau o Trang thai + hien popup chi tiet khi bam vao.
  JsonArray faults = r["activeFaults"].to<JsonArray>();
  for (uint8_t i = 0U; i < rt.activeFaultDisplayCount; ++i) {
    JsonObject f = faults.add<JsonObject>();
    f["code"] = rt.activeFaults[i].code;
    f["severity"] = rt.activeFaults[i].severity;
  }
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
  // Nut "Cap nhat" tren web CHI yeu cau may kiem tra ngay (bo qua nhip 6h),
  // KHONG tu tai ve/nap - van phai xac nhan vat ly tren HMI (xem ota_web_
  // update.h + hmi.h::openFirmwareWebConfirm()).
  if (!strcmp(action, "firmware_check_now")) return HmiCommandType::FirmwareWebCheckNow;
  // Quay lai firmware truoc do (xem ota_rollback.h) - KHAC voi cap nhat
  // (chi "kiem tra", phai xac nhan vat ly tren HMI), lenh nay ap dung
  // NGAY qua web vi ban chat la doi ve firmware DA TUNG chay on dinh
  // truoc do (khong phai 1 ban hoan toan moi/chua kiem chung), va nguoi
  // dung can quay lai duoc TU XA dung luc may dang gap loi sau khi cap
  // nhat, khong phai luc nao cung o canh may that.
  if (!strcmp(action, "firmware_rollback")) return HmiCommandType::FirmwareRollback;
  // Nut den/coi tren dashboard (outputStrip) - xem handleCommandMessage() ve
  // cach truyen alarmMask rieng cho alarm_ack (LightToggle khong can tham so).
  if (!strcmp(action, "light_toggle")) return HmiCommandType::LightToggle;
  if (!strcmp(action, "alarm_ack")) return HmiCommandType::AlarmAck;
  // F-06: xac nhan "tiep tuc u am" khi me qua han ngay du kien - khong bi
  // han che nguon (F-09) vi day la quyet dinh van hanh, khong phai su co
  // can kiem tra vat ly.
  if (!strcmp(action, "batch_overdue_continue")) return HmiCommandType::BatchOverdueContinue;
  return HmiCommandType::None;
}

static uint32_t lastCommandSequence = 0U;
static char lastCommandRequestId[WEB_REQUEST_ID_CAPACITY] = "";

// F-01 (audit truoc phat hanh v3.7.1): MQTT_USERNAME/MQTT_PASSWORD mac dinh
// la chuoi rong (xem config.h) - nghia la firmware ket noi AN DANH toi
// broker CONG KHAI mac dinh (broker.emqx.io) neu khong ai doi lai qua
// build_flags. O tinh huong do BAT KY AI tren internet biet duoc deviceId
// (in san tren tem QR may) deu gui duoc lenh dieu khien that (dung me, huy
// resume, tat coi khan cap, doi cau hinh...) ma khong can xac thuc gi -
// day la loi CRITICAL cua audit, vi ban build phat hanh chinh thuc (workflow
// CI) khong truyen build-flag nao de doi cac macro nay ca.
//
// Ham nay la hang rao an toan MAC DINH: false bat cu khi nao broker chua
// duoc cau hinh usernam/password rieng (tuc van dung cap mac dinh nguy hiem
// o tren). handleCommandMessage/handleConfigSetMessage/handleReminderSetMessage
// deu TU CHOI xu ly khi false - chi con luong publish MOT CHIEU (snapshot/
// presence/log, khong ai doi duoc gi tu xa) la con hoat dong. Publish/telemetry
// khong bi khoa vi rieng no khong the thay doi hanh vi may.
//
// De bat lai dieu khien tu xa qua MQTT: dinh nghia MAYAP_MQTT_HOST/USERNAME/
// PASSWORD (ly tuong them MAYAP_MQTT_USE_TLS=1) tro toi MOT BROKER RIENG
// truoc khi include config.h - vi du qua build_flags trong platformio.ini
// hoac --build-property khi goi arduino-cli trong workflow CI. KHONG sua
// truc tiep gia tri mac dinh trong config.h.
inline bool mqttCommandChannelTrusted() {
  const char *commandKey = mayapCommandKey();
  return MQTT_USERNAME[0] != '\0' && MQTT_PASSWORD[0] != '\0' &&
         commandKey && commandKey[0] != '\0';
}

inline int mqttHexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool mqttDecodeHex32(const char *text, uint8_t out[32]) {
  if (!text || strlen(text) != 64U) return false;
  for (size_t i = 0; i < 32U; ++i) {
    const int hi = mqttHexNibble(text[i * 2U]);
    const int lo = mqttHexNibble(text[i * 2U + 1U]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

inline bool mqttVerifySignedWrite(const char *channel, const JsonDocument &envelope,
                        JsonDocument &bodyDoc) {
  if (!channel || !channel[0]) return false;
  const char *body = envelope["body"] | "";
  const char *signatureHex = envelope["sig"] | "";
  const char *commandKeyHex = mayapCommandKey();
  if (!body[0] || strlen(body) >= 1350U || !commandKeyHex || !commandKeyHex[0]) return false;

  uint8_t key[32] = {0};
  uint8_t provided[32] = {0};
  if (!mqttDecodeHex32(commandKeyHex, key) || !mqttDecodeHex32(signatureHex, provided)) return false;

  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  bool ok = mbedtls_md_setup(&ctx, info, 1) == 0 &&
  mbedtls_md_hmac_starts(&ctx, key, sizeof(key)) == 0;
  static const char PREFIX[] = "mayap-mqtt-write:v1\n";
  static const char NL[] = "\n";
  if (ok) ok = mbedtls_md_hmac_update(&ctx,
      reinterpret_cast<const unsigned char *>(PREFIX), strlen(PREFIX)) == 0;
  if (ok) ok = mbedtls_md_hmac_update(&ctx,
      reinterpret_cast<const unsigned char *>(deviceId), strlen(deviceId)) == 0;
  if (ok) ok = mbedtls_md_hmac_update(&ctx,
      reinterpret_cast<const unsigned char *>(NL), 1U) == 0;
  if (ok) ok = mbedtls_md_hmac_update(&ctx,
      reinterpret_cast<const unsigned char *>(channel), strlen(channel)) == 0;
  if (ok) ok = mbedtls_md_hmac_update(&ctx,
      reinterpret_cast<const unsigned char *>(NL), 1U) == 0;
  if (ok) ok = mbedtls_md_hmac_update(&ctx,
      reinterpret_cast<const unsigned char *>(body), strlen(body)) == 0;
  uint8_t expected[32] = {0};
  if (ok) ok = mbedtls_md_hmac_finish(&ctx, expected) == 0;
  mbedtls_md_free(&ctx);
  if (!ok) return false;

  uint8_t diff = 0U;
  for (size_t i = 0; i < sizeof(expected); ++i) diff |= expected[i] ^ provided[i];
  if (diff != 0U) return false;
  return deserializeJson(bodyDoc, body) == DeserializationError::Ok;
}

inline void handleCommandMessage(const JsonDocument &doc) {
  const char *requestId = doc["requestId"] | "";
  if (!mqttCommandChannelTrusted()) {
    publishAck(requestId, "unauthorized", "BROKER CONG KHAI - LENH TU XA BI KHOA");
    return;
  }
  const uint32_t sequence = doc["sequence"] | 0UL;
  const uint32_t messageBootId = doc["bootId"] | 0UL;
  const char *action = doc["action"] | "";

  // Lenh dieu khien PHAI co requestId, sequence va bootId hop le.
  // bootId thay doi moi lan khoi dong, nen packet cua boot cu bi vo hieu.
  if (!requestId[0] || sequence == 0U) {
    publishAck(requestId, "invalid", "");
    return;
  }
  if (messageBootId == 0U || messageBootId != bootId) {
    publishAck(requestId, "stale", "");
    return;
  }

  // Chong lap trong cung boot: requestId khong duoc lap va sequence phai tang.
  if (!strcmp(requestId, lastCommandRequestId)) {
    publishAck(requestId, "duplicate", "");
    return;
  }
  if (lastCommandSequence != 0U && sequence <= lastCommandSequence) {
    publishAck(requestId, "stale", "");
    return;
  }

  const HmiCommandType type = mapCommandAction(action);
  if (type == HmiCommandType::None) {
    publishAck(requestId, "unsupported", "");
    return;
  }

  // Rollback thay doi firmware dang boot. Kenh MQTT hien dung credential
  // chung, nen rollback tu xa bi khoa. Rollback van dung duoc tren HMI
  // voi man xac nhan CO/HUY da co san.
  if (type == HmiCommandType::FirmwareRollback) {
    publishAck(requestId, "rejected", "QUAY LAI CAN XAC NHAN TAI MAY");
    return;
  }

  // "Coi bao" tren dashboard tat tam giong het nut ACK tren HMI (xem
  // requestAlarmAcknowledge() trong hmi.h) - can dung mat na canh bao
  // DANG active, khong phai AlarmNone, neu khong AlarmAck se khong xoa/tat
  // duoc gi ca (xem case HmiCommandType::AlarmAck trong machine_control.h).
  uint32_t alarmMaskParam = AlarmNone;
  if (type == HmiCommandType::AlarmAck) {
    portENTER_CRITICAL(&webMux);
    alarmMaskParam = knownRuntimeValid
        ? (knownRuntime.alarmMask & ALARM_KNOWN_MASK) : AlarmNone;
    portEXIT_CRITICAL(&webMux);
  }

  uint32_t commandId = 0U;
  const uint16_t validForMs = type == HmiCommandType::AutoTuneStart
      ? COMMAND_AUTOTUNE_VALID_MS : COMMAND_DEFAULT_VALID_MS;
  // F-09: danh dau lenh nay den tu MQTT (tu xa) - AlarmAck se tu choi rieng
  // 2 hanh dong can xac nhan vat ly (xoa loi dao/tat coi khan cap) neu nguon
  // la Remote, xem case HmiCommandType::AlarmAck trong processHmiTransactions().
  const bool queued = queueCommand(type, validForMs, 0U, alarmMaskParam, &commandId,
                                    HmiCommandSource::Remote);
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
  if (!mqttCommandChannelTrusted()) {
    publishAck(requestId, "unauthorized", "BROKER CONG KHAI - LENH TU XA BI KHOA");
    return;
  }
  const uint32_t revision = doc["revision"] | 0UL;
  if (!requestId[0] || revision == 0U) {
    publishAck(requestId, "invalid", "");
    return;
  }

  portENTER_CRITICAL(&webMux);
  const bool busy = pendingConfigSave.used;
  const bool haveBase = knownConfigValid;
  const uint32_t currentRevision = webConfigRevision;
  MachineConfig candidate = knownConfig;
  portEXIT_CRITICAL(&webMux);

  if (currentRevision != 0U && revision <= currentRevision) {
    publishAck(requestId, "stale", "");
    return;
  }
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
  candidate.humidifierEnabled = configObj["humidifierEnabled"] | candidate.humidifierEnabled;
  candidate.targetHumidity = configObj["targetHumidity"] | candidate.targetHumidity;
  candidate.ventOnTemp = configObj["ventOnTemp"] | candidate.ventOnTemp;
  candidate.ventOffTemp = configObj["ventOffTemp"] | candidate.ventOffTemp;
  candidate.ventScheduleEnabled = configObj["ventScheduleEnabled"] | candidate.ventScheduleEnabled;
  candidate.ventScheduleCount = configObj["ventScheduleCount"] | candidate.ventScheduleCount;
  candidate.ventScheduleDurationMin = configObj["ventScheduleDurationMin"] | candidate.ventScheduleDurationMin;
  candidate.ventScheduleHour1 = configObj["ventScheduleHour1"] | candidate.ventScheduleHour1;
  candidate.ventScheduleHour2 = configObj["ventScheduleHour2"] | candidate.ventScheduleHour2;
  candidate.ventScheduleHour3 = configObj["ventScheduleHour3"] | candidate.ventScheduleHour3;
  candidate.ventScheduleHour4 = configObj["ventScheduleHour4"] | candidate.ventScheduleHour4;
  candidate.ventScheduleHour5 = configObj["ventScheduleHour5"] | candidate.ventScheduleHour5;
  candidate.ventScheduleHour6 = configObj["ventScheduleHour6"] | candidate.ventScheduleHour6;
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
  candidate.manualTurnReanchorsSchedule =
      configObj["manualTurnReanchorsSchedule"] | candidate.manualTurnReanchorsSchedule;
  candidate.sirenSelfTestEnabled =
      configObj["sirenSelfTestEnabled"] | candidate.sirenSelfTestEnabled;
  candidate.autoResumeOnPowerLoss =
      configObj["autoResumeAfterPower"] | candidate.autoResumeOnPowerLoss;
  candidate.allowHeatWithoutBatch =
      configObj["allowHeatWithoutBatch"] | candidate.allowHeatWithoutBatch;
  candidate.alarmEnabled = configObj["alarmEnabled"] | candidate.alarmEnabled;
  candidate.lightAfterBatchAlarmEnabled =
      configObj["lightAfterBatchAlarmEnabled"] | candidate.lightAfterBatchAlarmEnabled;
  candidate.highTempAlarmWithoutBatch =
      configObj["highTempAlarmWithoutBatch"] | candidate.highTempAlarmWithoutBatch;
  candidate.controlMode = static_cast<ControlMode>(
      configObj["controlMode"] | static_cast<uint8_t>(candidate.controlMode));
  candidate.nextDirection = static_cast<TurnDirection>(
      configObj["nextDirection"] | static_cast<uint8_t>(candidate.nextDirection));
  // 8 truong "Nang cao" (schema 8) - cung bi THIEU o day tu luc them tinh
  // nang, khien luu tu form "Nang cao" tren web ROI VAO IM LANG (khong loi,
  // nhung khong truong nao trong 8 truong nay thuc su duoc ap dung).
  candidate.heaterStuckMinRiseC =
      configObj["heaterStuckMinRiseC"] | candidate.heaterStuckMinRiseC;
  candidate.heaterStuckDurationSec =
      configObj["heaterStuckDurationSec"] | candidate.heaterStuckDurationSec;
  candidate.tempRateLimitC = configObj["tempRateLimitC"] | candidate.tempRateLimitC;
  candidate.tempRateWindowSec =
      configObj["tempRateWindowSec"] | candidate.tempRateWindowSec;
  candidate.tempOscillationCrossLimit =
      configObj["tempOscillationCrossLimit"] | candidate.tempOscillationCrossLimit;
  candidate.tempOscillationWindowSec =
      configObj["tempOscillationWindowSec"] | candidate.tempOscillationWindowSec;
  candidate.autotuneRelayPowerPercent =
      configObj["autotuneRelayPowerPercent"] | candidate.autotuneRelayPowerPercent;
  candidate.autotuneBandC = configObj["autotuneBandC"] | candidate.autotuneBandC;

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

// Web da phan tich/xac thuc TOAN BO o phia web (parse ngay thang tu nhien,
// bao loi trung/khong hop le tren form...) - firmware CHI nhan mang (day,
// label) da xu ly san va sanitizeReminderSet() lam luoi an toan cuoi (xem
// config.h). Luon thay THE TOAN BO danh sach (khong merge tung phan tu),
// giong het huong tiep can cua "config/set" o tren.
inline void handleReminderSetMessage(const JsonDocument &doc) {
  const char *requestId = doc["requestId"] | "";
  if (!mqttCommandChannelTrusted()) {
    publishAck(requestId, "unauthorized", "BROKER CONG KHAI - LENH TU XA BI KHOA");
    return;
  }
  const uint32_t revision = doc["revision"] | 0UL;
  if (!requestId[0] || revision == 0U) {
    publishAck(requestId, "invalid", "");
    return;
  }

  portENTER_CRITICAL(&webMux);
  const bool busy = pendingReminderSave.used;
  const uint32_t currentRevision = webRemindersRevision;
  portEXIT_CRITICAL(&webMux);
  if (currentRevision != 0U && revision <= currentRevision) {
    publishAck(requestId, "stale", "");
    return;
  }
  if (busy) {
    publishAck(requestId, "busy", "");
    return;
  }

  JsonVariantConst remindersArr = doc["reminders"];
  if (!remindersArr.is<JsonArrayConst>()) {
    publishAck(requestId, "invalid", "THIEU REMINDERS");
    return;
  }

  ReminderSet candidate{};
  uint8_t slot = 0U;
  for (JsonVariantConst entry : remindersArr.as<JsonArrayConst>()) {
    if (slot >= MAX_CUSTOM_REMINDERS) break;  // web da gioi han 10, day la luoi du phong
    const int day = entry["day"] | 0;
    const char *label = entry["label"] | "";
    if (day <= 0 || !label[0]) continue;  // muc khong hop le: bo qua thay vi tu choi ca goi
    candidate.items[slot].day = static_cast<uint8_t>(constrain(day, 1, 200));
    snprintf(candidate.items[slot].label, sizeof(candidate.items[slot].label), "%s", label);
    ++slot;
  }

  if (!startReminderSave(candidate)) {
    publishAck(requestId, "busy", "");
    return;
  }

  portENTER_CRITICAL(&webMux);
  webRemindersRevision = revision > webRemindersRevision ? revision : webRemindersRevision + 1U;
  pendingReminderSave.used = true;
  pendingReminderSave.queuedAt = millis();
  snprintf(pendingReminderSave.requestId, sizeof(pendingReminderSave.requestId), "%s",
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
    const HmiEventSnapshot recentEvents = pendingEventSnapshot;
    const bool haveReminders = knownRemindersValid;
    const ReminderSet reminders = knownReminders;
    const uint32_t remindersRevision = webRemindersRevision;
    portEXIT_CRITICAL(&webMux);
    if (haveConfig) publishConfigReport(cfg, revision);
    if (haveReminders) publishReminderReport(reminders, remindersRevision);
    lastSnapshotPublishAt = 0U;  // ep publish snapshot ngay trong vong lap toi
    // Trinh duyet MOI mo/vua ket noi lai chi nhan duoc cac su kien XAY RA TU
    // LUC DO VE SAU qua topic "log" (MQTT khong co lich su, chi phat tuc
    // thoi) - "Nhat ky me ap" tren web vi vay trong/thieu neu bo lo su kien
    // xay ra TRUOC do (vd bat/tat me tu HMI, hoac tu 1 trinh duyet khac dang
    // mo). Phat lai toan bo backlog dang giu (toi da HMI_EVENT_DISPLAY_CAPACITY
    // muc, theo thu tu CU->MOI) moi khi co yeu cau "sync" de trinh duyet nay
    // bat kip lich su that cua may - web da tu dedupe theo "sequence" (xem
    // handleLog() trong app.js) nen phat lai muc da co san KHONG gay trung,
    // chi don gian khong lam gi neu trinh duyet do da nhan roi.
    for (uint8_t offset = recentEvents.count; offset > 0U; --offset) {
      publishLogEntry(recentEvents.items[offset - 1U]);
    }
  }
}

inline void mqttMessageCallback(char *topic, uint8_t *payload,
                      unsigned int length) {
  // Ban tin ghi duoc boc trong envelope {body,sig}; 1536 byte van la tran
  // chung cua PubSubClient va buffer cuc bo.
  if (length >= 1536U) return;
  char buffer[1536];
  memcpy(buffer, payload, length);
  buffer[length] = ' ';

  JsonDocument wireDoc;
  if (deserializeJson(wireDoc, buffer, length) != DeserializationError::Ok) return;

  auto verifyAndDispatch = [&](const char *channel, auto handler) {
    JsonDocument bodyDoc;
    if (!mqttVerifySignedWrite(channel, wireDoc, bodyDoc)) {
      const char *legacyRequestId = wireDoc["requestId"] | "";
      if (legacyRequestId[0]) publishAck(legacyRequestId, "unauthorized", "CHU KY LENH KHONG HOP LE");
      return;
    }
    handler(bodyDoc);
  };

  if (strstr(topic, "/config/set")) {
    verifyAndDispatch("config/set", [](const JsonDocument &doc) { handleConfigSetMessage(doc); });
    return;
  }
  if (strstr(topic, "/reminders/set")) {
    verifyAndDispatch("reminders/set", [](const JsonDocument &doc) { handleReminderSetMessage(doc); });
    return;
  }
  if (strstr(topic, "/history/request")) {
    verifyAndDispatch("history/request", [](const JsonDocument &doc) { handleHistoryRequestMessage(doc); });
    return;
  }
  const char *suffix = strrchr(topic, '/');
  if (!suffix) return;
  ++suffix;
  if (!strcmp(suffix, "command")) {
    verifyAndDispatch("command", [](const JsonDocument &doc) { handleCommandMessage(doc); });
  } else if (!strcmp(suffix, "session")) {
    // Session chi dieu chinh tan suat snapshot/Wi-Fi power, khong thay doi
    // control setpoint/actuator nen de unsigned de giu web nhe va tu phuc hoi.
    handleSessionMessage(wireDoc);
  }
}

// ------------------------------ Vong doi ket noi -------------------------------
inline void subscribeAll() {
  mqtt.subscribe(topicOf("config/set"));
  mqtt.subscribe(topicOf("reminders/set"));
  mqtt.subscribe(topicOf("command"));
  mqtt.subscribe(topicOf("history/request"));
  mqtt.subscribe(topicOf("session"));
}

inline void attemptConnect(uint32_t now) {
  if (!mqttTlsReady || !MQTT_BROKER_HOST[0]) return;
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
  // F-01: canh bao ro moi lan ket noi neu dang dung broker/tai khoan mac
  // dinh - lenh dieu khien tu xa dang bi khoa (xem mqttCommandChannelTrusted()).
  if (!mqttCommandChannelTrusted()) {
    mayapSerialPrintf(false,
        "[WEBLINK] CANH BAO: broker CONG KHAI khong xac thuc - LENH DIEU "
        "KHIEN TU XA (start/stop/ACK/config/nhac nho) DA BI KHOA de an toan. "
        "Dinh nghia MAYAP_MQTT_HOST/USERNAME/PASSWORD tro toi broker rieng "
        "de bat lai.\n");
  }
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
  bool remindersExpired = false;
  char reminderRequestId[WEB_REQUEST_ID_CAPACITY] = "";
  if (pendingReminderSave.used &&
      elapsedMs(now, pendingReminderSave.queuedAt) >= WEB_REMINDER_SAVE_ACK_TIMEOUT_MS) {
    remindersExpired = true;
    snprintf(reminderRequestId, sizeof(reminderRequestId), "%s",
             pendingReminderSave.requestId);
    pendingReminderSave.used = false;
  }
  portEXIT_CRITICAL(&webMux);

  for (uint8_t i = 0; i < expireCount; ++i) publishAck(requestIdsToExpire[i], "expired", "");
  if (configExpired) publishAck(configRequestId, "expired", "");
  if (remindersExpired) publishAck(reminderRequestId, "expired", "");
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

// Nguon "can hieu nang cao": phien web dang active, HOAC cong doi Wi-Fi tren
// HMI dang mo (AP+STA dang bat, dang phat song MAYAP-XXXX), HOAC dang co
// canh bao/loi con hoat dong (alarmMask != AlarmNone) - truong hop thu 3 nay
// dam bao goi canh bao qua Cloud Push (cloud_alert_link.h, chay cung
// networkTask) di voi do tre thap nhat co the, khong phai cho WiFi "thuc
// day" tu WIFI_PS_MIN_MODEM luc dang co su co that su can bao gap. Chi doc
// knownRuntime (da duoc controlTask ghi san qua mayapWebSetRuntime(), bao ve
// bang webMux - xem hook o duoi file) - KHONG dong cham gi den controlTask/
// vong dieu khien PID, giu dung yeu cau on dinh phan dieu khien la uu tien
// tuyet doi.
//
// Day la noi DUY NHAT trong toan bo firmware goi esp_wifi_set_ps() - luon di
// qua applyWifiPowerMode() de bien dem highPerfWifiApplied khong bao gio
// lech voi trang thai phan cung that (neu co noi thu hai tu goi thang, bien
// dem se "tuong" sai va bo qua lan dong bo sau, ket qua la giu nham che do).
// Chay MOI vong lap ke ca khi STA dang tat (dung luc cong doi Wi-Fi vua ngat
// STA de bat AP on dinh - xem network_service.h::portalBeginStarting), nen
// duoc goi truoc moi nhanh return som cua mayapWebLinkUpdate().
inline void serviceWifiPowerMode() {
  const WifiPortalStatus portal = mayapGetWifiPortalStatus();
  const bool portalActive = portal.state != WifiPortalState::Idle;
  portENTER_CRITICAL(&webMux);
  const bool alarmActive = knownRuntimeValid && knownRuntime.alarmMask != AlarmNone;
  portEXIT_CRITICAL(&webMux);
  applyWifiPowerMode(webSessionActive || portalActive || alarmActive);
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

inline void serviceReminderPublish() {
  portENTER_CRITICAL(&webMux);
  const bool dirty = remindersDirty;
  remindersDirty = false;
  const ReminderSet reminders = knownReminders;
  const uint32_t revision = webRemindersRevision;
  portEXIT_CRITICAL(&webMux);
  if (dirty) publishReminderReport(reminders, revision);
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
  mqttTlsReady = TLS_ROOT_CA[0] != '\0';
  if (mqttTlsReady) {
    netClient.setCACert(TLS_ROOT_CA);
  } else {
    mayapSerialPrintf(true,
        "[WEBLINK] TLS bi khoa: chua nhung MAYAP_TLS_ROOT_CA - KHONG ha cap insecure\n");
  }
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
  serviceReminderPublish();
  serviceSnapshotPublish(now);
  serviceEventLogPublish();
  serviceHistoryResponse();
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

inline void mayapWebSetReminders(const ReminderSet &reminders) {
  using namespace MayapRealtimeInternal;
  portENTER_CRITICAL(&webMux);
  const bool changed = !knownRemindersValid ||
      memcmp(&reminders, &knownReminders, sizeof(ReminderSet)) != 0;
  knownReminders = reminders;
  knownRemindersValid = true;
  if (changed) {
    remindersDirty = true;
    if (webRemindersRevision == 0U) webRemindersRevision = 1U;
    else if (!pendingReminderSave.used) ++webRemindersRevision;
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

inline void mayapWebConfirmReminderSave(uint32_t transactionId, bool ok,
                                        const ReminderSet *stored) {
  using namespace MayapRealtimeInternal;
  (void)transactionId;
  (void)stored;  // danh sach moi da/se toi qua mayapWebSetReminders() tu cung noi goi
  portENTER_CRITICAL(&webMux);
  if (pendingReminderSave.used) {
    enqueueAckLocked(pendingReminderSave.requestId, ok ? "applied" : "rejected",
                     ok ? "" : "LUU NHAC NHO BI TU CHOI");
    pendingReminderSave.used = false;
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
