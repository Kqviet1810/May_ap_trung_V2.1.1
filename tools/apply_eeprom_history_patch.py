#!/usr/bin/env python3
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding='utf-8')


def write(path, text):
    (ROOT / path).write_text(text, encoding='utf-8')


def replace_once(text, old, new, label):
    if old not in text:
        raise SystemExit(f'PATCH FAIL: missing anchor {label}')
    if text.count(old) != 1:
        raise SystemExit(f'PATCH FAIL: anchor {label} count={text.count(old)}')
    return text.replace(old, new, 1)


# ---------------- config.h: AT24C32 map + version ----------------
path = 'MAYAP_INDUSTRIAL_v3_4_0/config.h'
text = read(path)
text = replace_once(text,
    'constexpr char MAYAP_FIRMWARE_VERSION[] = "3.8.2";',
    'constexpr char MAYAP_FIRMWARE_VERSION[] = "3.8.3";',
    'firmware version')
text = replace_once(text,
    'constexpr uint16_t EEPROM_REMINDERS_SLOT_BYTES = 0x0400U;\n',
    '''constexpr uint16_t EEPROM_REMINDERS_SLOT_BYTES = 0x0400U;\n\n// Lich su nhiet do 24 gio tren AT24C32: dung DUY NHAT vung con trong\n// 0x0B00..0x0FFF, tach khoi Config/Batch/Reminders. Moi mau 4 byte, 5 phut/mau,\n// 288 slot = 1152 byte; khong co con tro ghi co dinh de tranh tao wear hotspot.\nconstexpr uint16_t EEPROM_ADDR_TEMP_HISTORY = 0x0B00U;\nconstexpr uint16_t TEMP_HISTORY_SAMPLE_SEC = 300U;\nconstexpr uint16_t TEMP_HISTORY_SLOT_COUNT = 288U;\nconstexpr uint16_t TEMP_HISTORY_RECORD_BYTES = 4U;\nconstexpr uint16_t TEMP_HISTORY_STORAGE_BYTES =\n    TEMP_HISTORY_SLOT_COUNT * TEMP_HISTORY_RECORD_BYTES;\nstatic_assert(TEMP_HISTORY_STORAGE_BYTES == 1152U,\n              "History 24h/5phut phai dung 1152 byte");\n''',
    'history constants')
text = replace_once(text,
    'static_assert(EEPROM_ADDR_REMINDERS_B + EEPROM_REMINDERS_SLOT_BYTES <= EEPROM_CAPACITY_BYTES,\n              "Ban do EEPROM vuot 4KB");',
    '''static_assert(EEPROM_ADDR_REMINDERS_B + EEPROM_REMINDERS_SLOT_BYTES <= EEPROM_ADDR_TEMP_HISTORY,\n              "Reminders B de len History");\nstatic_assert(EEPROM_ADDR_TEMP_HISTORY + TEMP_HISTORY_STORAGE_BYTES <= EEPROM_CAPACITY_BYTES,\n              "History vuot dung luong AT24C32");\nstatic_assert(EEPROM_PAGE_SIZE == 32U, "History duoc tinh cho AT24C32 page 32 byte");''',
    'EEPROM map asserts')
write(path, text)


# ---------------- history_store.h: wear-balanced ring ----------------
history_header = r'''#pragma once

#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <stdint.h>

// I2C mutex duoc tao/define trong file .ino truoc khi include header nay.
bool mayapI2cLock(uint32_t timeoutMs);
void mayapI2cUnlock();

struct MayapTemperatureHistoryPoint {
  uint32_t epoch = 0U;
  int16_t temperatureX10 = 0;
};

namespace MayapTemperatureHistoryInternal {

// Record 4 byte:
//   byte0: bucket[7:0]
//   byte1: bucket[11:8] | tempCode[3:0] << 4
//   byte2: tempCode[11:4]
//   byte3: CRC-8 (poly 0x07) cua 3 byte dau
// bucket = floor(epoch/300) mod 4096; tempCode = round(temp*10)+500.
// Slot vat ly = floor(epoch/300) mod 288. Khong metadata/index ghi dinh ky,
// nen moi slot chi bi viet lai xap xi 1 lan/ngay; moi page 32B nhan 8 lan/ngay.
static uint32_t lastSampleBucket = UINT32_MAX;
static volatile uint32_t latestRtcEpoch = 0U;

inline uint8_t crc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0U;
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1U) ^ 0x07U)
                          : static_cast<uint8_t>(crc << 1U);
    }
  }
  return crc;
}

inline uint16_t addressForBucket(uint32_t absoluteBucket) {
  const uint16_t slot = static_cast<uint16_t>(absoluteBucket % TEMP_HISTORY_SLOT_COUNT);
  return static_cast<uint16_t>(EEPROM_ADDR_TEMP_HISTORY +
                               slot * TEMP_HISTORY_RECORD_BYTES);
}

inline bool readRaw(uint16_t address, uint8_t out[TEMP_HISTORY_RECORD_BYTES]) {
  if (!mayapI2cLock(I2C_STORAGE_LOCK_TIMEOUT_MS)) return false;
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write(static_cast<uint8_t>(address >> 8U));
  Wire.write(static_cast<uint8_t>(address & 0xFFU));
  if (Wire.endTransmission(false) != 0U) {
    mayapI2cUnlock();
    return false;
  }
  const size_t got = Wire.requestFrom(EEPROM_I2C_ADDRESS,
                                      static_cast<uint8_t>(TEMP_HISTORY_RECORD_BYTES),
                                      static_cast<uint8_t>(true));
  bool ok = got == TEMP_HISTORY_RECORD_BYTES;
  for (uint8_t i = 0U; i < TEMP_HISTORY_RECORD_BYTES && ok; ++i) {
    if (!Wire.available()) { ok = false; break; }
    out[i] = static_cast<uint8_t>(Wire.read());
  }
  mayapI2cUnlock();
  return ok;
}

inline bool writeRaw(uint16_t address, const uint8_t data[TEMP_HISTORY_RECORD_BYTES]) {
  // Dia chi history luon boi so 4 va record 4B, nen khong bao gio vuot page 32B.
  if ((address % EEPROM_PAGE_SIZE) > EEPROM_PAGE_SIZE - TEMP_HISTORY_RECORD_BYTES) return false;
  if (!mayapI2cLock(I2C_STORAGE_LOCK_TIMEOUT_MS)) return false;
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write(static_cast<uint8_t>(address >> 8U));
  Wire.write(static_cast<uint8_t>(address & 0xFFU));
  const size_t written = Wire.write(data, TEMP_HISTORY_RECORD_BYTES);
  const uint8_t err = Wire.endTransmission(true);
  if (written != TEMP_HISTORY_RECORD_BYTES || err != 0U) {
    mayapI2cUnlock();
    return false;
  }

  // ACK polling: AT24C32 tu ghi noi bo toi da ~5ms; gioi han 20ms giong driver
  // storage chinh. Chi mot record 4B/5phut, khong nam tren duong dieu khien nong.
  const uint32_t started = millis();
  bool ready = false;
  do {
    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    ready = Wire.endTransmission(true) == 0U;
    if (!ready) delay(1);
  } while (!ready && static_cast<uint32_t>(millis() - started) < EEPROM_WRITE_TIMEOUT_MS);
  mayapI2cUnlock();
  return ready;
}

inline bool encode(uint32_t absoluteBucket, float temperature,
                   uint8_t out[TEMP_HISTORY_RECORD_BYTES]) {
  if (!isfinite(temperature) || temperature < -20.0f || temperature > 100.0f) return false;
  const int32_t temp10 = lroundf(temperature * 10.0f);
  const int32_t codeSigned = temp10 + 500;
  if (codeSigned < 0 || codeSigned > 4095) return false;
  const uint16_t tag = static_cast<uint16_t>(absoluteBucket & 0x0FFFU);
  const uint16_t code = static_cast<uint16_t>(codeSigned);
  out[0] = static_cast<uint8_t>(tag & 0xFFU);
  out[1] = static_cast<uint8_t>(((tag >> 8U) & 0x0FU) | ((code & 0x0FU) << 4U));
  out[2] = static_cast<uint8_t>((code >> 4U) & 0xFFU);
  out[3] = crc8(out, 3U);
  return true;
}

inline bool decode(uint32_t absoluteBucket,
                   const uint8_t raw[TEMP_HISTORY_RECORD_BYTES],
                   MayapTemperatureHistoryPoint &out) {
  if (crc8(raw, 3U) != raw[3]) return false;
  const uint16_t tag = static_cast<uint16_t>(raw[0] | ((raw[1] & 0x0FU) << 8U));
  if (tag != static_cast<uint16_t>(absoluteBucket & 0x0FFFU)) return false;
  const uint16_t code = static_cast<uint16_t>(((raw[1] >> 4U) & 0x0FU) |
                                              (static_cast<uint16_t>(raw[2]) << 4U));
  const int16_t temp10 = static_cast<int16_t>(static_cast<int32_t>(code) - 500);
  if (temp10 < -200 || temp10 > 1000) return false;
  out.epoch = absoluteBucket * TEMP_HISTORY_SAMPLE_SEC;
  out.temperatureX10 = temp10;
  return true;
}

inline bool readBucket(uint32_t absoluteBucket, MayapTemperatureHistoryPoint &out) {
  uint8_t raw[TEMP_HISTORY_RECORD_BYTES]{};
  if (!readRaw(addressForBucket(absoluteBucket), raw)) return false;
  return decode(absoluteBucket, raw, out);
}

inline bool writeBucket(uint32_t absoluteBucket, float temperature) {
  uint8_t raw[TEMP_HISTORY_RECORD_BYTES]{};
  if (!encode(absoluteBucket, temperature, raw)) return false;
  return writeRaw(addressForBucket(absoluteBucket), raw);
}

}  // namespace MayapTemperatureHistoryInternal

inline void mayapTemperatureHistorySample(uint32_t rtcEpoch, float temperature,
                                          bool sensorUsable) {
  using namespace MayapTemperatureHistoryInternal;
  if (rtcEpoch == 0U) return;
  __atomic_store_n(&latestRtcEpoch, rtcEpoch, __ATOMIC_RELEASE);
  if (!sensorUsable || !isfinite(temperature)) return;

  const uint32_t bucket = rtcEpoch / TEMP_HISTORY_SAMPLE_SEC;
  if (bucket == lastSampleBucket) return;

  // Sau reboot co the slot hien tai da duoc ghi truoc do. Doc kiem tra truoc
  // de khong ghi lai cung mot page vo ich. Neu doc fail/record cu thi ghi mau moi.
  MayapTemperatureHistoryPoint existing{};
  const bool alreadyStored = readBucket(bucket, existing);
  lastSampleBucket = bucket;  // history la best-effort; loi I2C khong duoc hammer moi 5ms.
  if (alreadyStored) return;
  (void)writeBucket(bucket, temperature);
}

inline uint32_t mayapTemperatureHistoryLatestEpoch() {
  return __atomic_load_n(&MayapTemperatureHistoryInternal::latestRtcEpoch,
                         __ATOMIC_ACQUIRE);
}

inline bool mayapTemperatureHistoryReadBucket(uint32_t absoluteBucket,
                                              MayapTemperatureHistoryPoint &out) {
  return MayapTemperatureHistoryInternal::readBucket(absoluteBucket, out);
}
'''
write('MAYAP_INDUSTRIAL_v3_4_0/history_store.h', history_header)


# ---------------- include header before realtime_link ----------------
path = 'MAYAP_INDUSTRIAL_v3_4_0/MAYAP_INDUSTRIAL_v3_4_0.ino'
text = read(path)
text = replace_once(text,
    '#include "hmi.h"\n// realtime_link.h',
    '#include "hmi.h"\n#include "history_store.h"\n// realtime_link.h',
    'history include')
write(path, text)


# ---------------- machine_control: sample every RTC bucket ----------------
path = 'MAYAP_INDUSTRIAL_v3_4_0/machine_control.h'
text = read(path)
text = replace_once(text,
    '    serviceBatchLog(now);\n    serviceHealthMonitor(now);\n    updateLed(now);',
    '''    serviceBatchLog(now);\n    serviceHealthMonitor(now);\n    // Lich su nhiet tach khoi Flash ESP32/Cloud: chi ghi AT24C32 moi 5 phut.\n    // Ham tu bo qua neu RTC/cam bien khong hop le va tu tranh ghi lap sau reboot.\n    mayapTemperatureHistorySample(rtc_.valid() ? rtc_.epoch() : 0U,\n                                  temperature_, sensorUsable_);\n    updateLed(now);''',
    'machine history sample')
write(path, text)


# ---------------- realtime_link: signed on-demand history over MQTT ----------------
path = 'MAYAP_INDUSTRIAL_v3_4_0/realtime_link.h'
text = read(path)
text = replace_once(text,
    'static uint32_t lastPublishedEventSequence = 0U;\n',
    '''static uint32_t lastPublishedEventSequence = 0U;\n\n// --------------------- Lich su nhiet do AT24C32 -> Web ----------------------\n// Chi doc EEPROM khi web yeu cau; moi vong networkTask chi phat toi da 12\n// bucket de khong chiem I2C/MQTT lau. Request duoc HMAC giong command/config.\nstatic bool historyResponsePending = false;\nstatic uint16_t historyWindowMinutes = 30U;\nstatic uint16_t historyCursor = 0U;\nstatic uint16_t historyCandidateCount = 0U;\nstatic uint32_t historySnapshotEpoch = 0U;\nstatic char historyRequestId[WEB_REQUEST_ID_CAPACITY] = "";\n''',
    'history realtime state')

insert_marker = 'inline void publishPresence(bool online) {'
history_functions = r'''inline void handleHistoryRequestMessage(const JsonDocument &doc) {
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

'''
if history_functions.strip() in text:
    raise SystemExit('PATCH FAIL: history functions already present')
text = replace_once(text, insert_marker, history_functions + insert_marker, 'history publish functions')

text = replace_once(text,
    '  mqtt.subscribe(topicOf("command"));\n  mqtt.subscribe(topicOf("session"));',
    '  mqtt.subscribe(topicOf("command"));\n  mqtt.subscribe(topicOf("history/request"));\n  mqtt.subscribe(topicOf("session"));',
    'history subscribe')
text = replace_once(text,
    '''  if (strstr(topic, "/reminders/set")) {\n    verifyAndDispatch("reminders/set", [](const JsonDocument &doc) { handleReminderSetMessage(doc); });\n    return;\n  }\n  const char *suffix = strrchr(topic, '/');''',
    '''  if (strstr(topic, "/reminders/set")) {\n    verifyAndDispatch("reminders/set", [](const JsonDocument &doc) { handleReminderSetMessage(doc); });\n    return;\n  }\n  if (strstr(topic, "/history/request")) {\n    verifyAndDispatch("history/request", [](const JsonDocument &doc) { handleHistoryRequestMessage(doc); });\n    return;\n  }\n  const char *suffix = strrchr(topic, '/');''',
    'history signed dispatch')
text = replace_once(text,
    '  serviceSnapshotPublish(now);\n  serviceEventLogPublish();',
    '  serviceSnapshotPublish(now);\n  serviceEventLogPublish();\n  serviceHistoryResponse();',
    'history service call')
write(path, text)


# ---------------- Cloud Worker: authorize signed read request ----------------
path = 'cloudflare/src/index.js'
text = read(path)
text = replace_once(text,
    "const MQTT_WRITE_CHANNELS = new Set(['command', 'config/set', 'reminders/set']);",
    "const MQTT_WRITE_CHANNELS = new Set(['command', 'config/set', 'reminders/set', 'history/request']);",
    'worker history signing channel')
write(path, text)


# ---------------- index.html: restore chart, move logs to Settings ----------------
path = 'index.html'
text = read(path)
old_log = '''<section aria-labelledby="batchLogTitle" class="panel batchLogPanel">\n<div class="panelHead"><div><p class="eyebrow">NHẬT KÝ MẺ ẤP</p><h2 id="batchLogTitle">Máy vừa thực hiện</h2></div><span class="logHint">30 mục gần nhất</span></div>\n<div class="currentActivity"><span class="activityPulse idle" id="activityPulse"></span><div><strong id="currentActivityText">Sẵn sàng</strong><small id="currentActivityMeta">Không có tác vụ đang chạy</small></div></div>\n<div aria-live="polite" class="batchLogList" id="batchLogList"></div>\n</section>'''
chart_html = '''<section aria-labelledby="temperatureChartTitle" class="panel telemetryPanel">\n<div class="panelHead telemetryHead"><div><p class="eyebrow">NHIỆT ĐỘ · 30 PHÚT</p><h2 id="temperatureChartTitle">Diễn biến nhiệt độ</h2></div><div class="telemetryLegend" aria-label="Chú thích biểu đồ"><span><i class="liveLine"></i>Nhiệt độ</span><span><i class="setLine"></i>Mức đặt</span></div></div>\n<div class="telemetryMeta"><strong id="temperatureChartValue">—</strong><span id="temperatureChartStatus">Đang chờ dữ liệu</span></div>\n<div class="telemetryCanvasWrap"><canvas aria-label="Biểu đồ nhiệt độ 30 phút gần nhất" id="temperatureChartCanvas" role="img"></canvas><div class="telemetryEmpty" id="temperatureChartEmpty">Đang chờ dữ liệu nhiệt độ…</div></div>\n<div class="telemetryAxis" aria-hidden="true"><span>−30 phút</span><span>−15 phút</span><span>Bây giờ</span></div>\n</section>'''
text = replace_once(text, old_log, chart_html, 'batch log -> chart')

firmware_marker = '<details class="settingCard">\n<summary><span class="settingIcon"><svg fill="none" height="22" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" viewBox="0 0 24 24" width="22"><path d="M12 15V3"/><path d="m7 10 5 5 5-5"/><path d="M20 21H4"/></svg><span class="updateDot" hidden id="firmwareUpdateDot"></span></span><span class="settingSummary"><strong>Cập nhật firmware</strong>'
log_setting = '''<details class="settingCard" id="diagnosticsLogSetting">\n<summary><span class="settingIcon"><svg fill="none" height="22" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" viewBox="0 0 24 24" width="22"><path d="M4 5h16M4 12h16M4 19h16"/><circle cx="7" cy="5" r="1" fill="currentColor" stroke="none"/><circle cx="7" cy="12" r="1" fill="currentColor" stroke="none"/><circle cx="7" cy="19" r="1" fill="currentColor" stroke="none"/></svg></span><span class="settingSummary"><strong>Nhật ký &amp; chẩn đoán</strong><small>30 hoạt động gần nhất</small></span><span aria-hidden="true" class="disclosure"></span></summary>\n<div class="settingBody diagnosticsLogBody">\n<div class="currentActivity"><span class="activityPulse idle" id="activityPulse"></span><div><strong id="currentActivityText">Sẵn sàng</strong><small id="currentActivityMeta">Không có tác vụ đang chạy</small></div></div>\n<div aria-live="polite" class="batchLogList" id="batchLogList"></div>\n</div>\n</details>\n'''
text = replace_once(text, firmware_marker, log_setting + firmware_marker, 'move logs to settings')
write(path, text)


# ---------------- app.js: chart uses signed MQTT history from EEPROM ----------------
path = 'app.js'
text = read(path)
page_meta_anchor = '''  const pageMeta = {\n    device: ['Thiết bị', 'Theo dõi và điều khiển máy.'],\n    batch: ['Mẻ ấp', 'Thiết lập và quản lý mẻ ấp.'],\n    settings: ['Cài đặt', 'Thông số vận hành và kết nối.']\n  };\n'''
telemetry_state = '''  const pageMeta = {\n    device: ['Thiết bị', 'Theo dõi và điều khiển máy.'],\n    batch: ['Mẻ ấp', 'Thiết lập và quản lý mẻ ấp.'],\n    settings: ['Cài đặt', 'Thông số vận hành và kết nối.']\n  };\n\n  const TELEMETRY_WINDOW_MS = 30 * 60 * 1000;\n  const TELEMETRY_LIVE_SAMPLE_MS = 5000;\n  const TELEMETRY_HISTORY_REFRESH_MS = 5 * 60 * 1000;\n  const TELEMETRY_MAX_POINTS = 500;\n  const telemetryChart = {\n    deviceId: '', points: [], historyLoaded: false, historyLoadedAt: 0,\n    historyLoading: false, historyRetryAt: 0, historyRequestSeq: 0,\n    activeRequestId: '', lastSampleAt: 0, renderRaf: 0,\n  };\n'''
text = replace_once(text, page_meta_anchor, telemetry_state, 'app telemetry state')
text = replace_once(text,
    "    if (name === 'batch') renderBatchLogs();",
    "    if (name === 'batch') { loadTelemetryHistory(); requestTemperatureChartRender(); }\n    if (name === 'settings') renderBatchLogs();",
    'showPage chart/log')

text = replace_once(text,
    "      log: `${base}/log`,\n      config: `${base}/config/set`,",
    "      log: `${base}/log`,\n      historyReport: `${base}/history/reported`,\n      config: `${base}/config/set`,",
    'app history report topic')
text = replace_once(text,
    "      command: `${base}/command`,\n      session: `${base}/session`",
    "      command: `${base}/command`,\n      historyRequest: `${base}/history/request`,\n      session: `${base}/session`",
    'app history request topic')
text = text.replace(
    '[outputTopics.presence, outputTopics.snapshot, outputTopics.report, outputTopics.remindersReport, outputTopics.ack, outputTopics.log]',
    '[outputTopics.presence, outputTopics.snapshot, outputTopics.report, outputTopics.remindersReport, outputTopics.ack, outputTopics.log, outputTopics.historyReport]')
text = replace_once(text,
    '(presence|snapshot|config/reported|reminders/reported|ack|log)$`));',
    '(presence|snapshot|config/reported|reminders/reported|ack|log|history/reported)$`));',
    'app parse history topic')
text = replace_once(text,
    "      else if (parsedTopic.channel === 'log') handleLog(device, payload);",
    "      else if (parsedTopic.channel === 'log') handleLog(device, payload);\n      else if (parsedTopic.channel === 'history/reported') handleTemperatureHistory(device, payload);",
    'app history dispatch')
text = replace_once(text,
    '    device.snapshotAt = Date.now();\n    device.bootId = Number(snapshot.bootId || device.bootId || 0);',
    '    device.snapshotAt = Date.now();\n    feedTelemetrySnapshot(device, snapshot);\n    device.bootId = Number(snapshot.bootId || device.bootId || 0);',
    'app feed live snapshot')

chart_code = r'''

  // ==================== Bieu do nhiet do 30 phut / AT24C32 ====================
  // ESP32 luu 24h vao EEPROM ngoai (5 phut/mau). Web chi yeu cau 30 phut qua
  // MQTT khi can va tiep tuc chen mau live 5s tu snapshot; khong ghi Cloud/D1.
  function telemetryEnsureDevice(device = currentDevice()) {
    const deviceId = device?.id || '';
    if (telemetryChart.deviceId === deviceId) return;
    telemetryChart.deviceId = deviceId;
    telemetryChart.points = [];
    telemetryChart.historyLoaded = false;
    telemetryChart.historyLoadedAt = 0;
    telemetryChart.historyLoading = false;
    telemetryChart.historyRetryAt = 0;
    telemetryChart.historyRequestSeq += 1;
    telemetryChart.activeRequestId = '';
    telemetryChart.lastSampleAt = 0;
    requestTemperatureChartRender();
  }

  function telemetryPoint(raw) {
    const t = Number(raw?.t ?? raw?.time);
    const temperature = Number(raw?.temperature);
    if (!Number.isFinite(t) || t <= 0 || !Number.isFinite(temperature) || temperature < -20 || temperature > 100) return null;
    return { t, temperature };
  }

  function telemetryMerge(points) {
    const cutoff = Date.now() - TELEMETRY_WINDOW_MS - 10 * 60_000;
    const map = new Map();
    [...telemetryChart.points, ...points].forEach((raw) => {
      const point = telemetryPoint(raw);
      if (!point || point.t < cutoff) return;
      map.set(Math.round(point.t / 1000), point);
    });
    telemetryChart.points = [...map.values()].sort((a, b) => a.t - b.t).slice(-TELEMETRY_MAX_POINTS);
  }

  function telemetrySetStatus(text) {
    const element = $('temperatureChartStatus');
    if (element) element.textContent = text;
  }

  async function loadTelemetryHistory(force = false) {
    const device = currentDevice();
    telemetryEnsureDevice(device);
    if (!device) {
      telemetrySetStatus('Chưa chọn thiết bị');
      requestTemperatureChartRender();
      return;
    }
    const fresh = telemetryChart.historyLoaded &&
      Date.now() - telemetryChart.historyLoadedAt < TELEMETRY_HISTORY_REFRESH_MS;
    if (!force && (fresh || telemetryChart.historyLoading || Date.now() < telemetryChart.historyRetryAt)) return;
    if (!state.mqttConnected || !state.mqtt?.connected) {
      telemetrySetStatus('Đang chờ kết nối thiết bị');
      telemetryChart.historyRetryAt = Date.now() + 5000;
      return;
    }
    if (!device.pairingToken) {
      telemetrySetStatus('Cần xác thực PIN để đọc lịch sử');
      telemetryChart.historyRetryAt = Date.now() + 60_000;
      return;
    }

    telemetryChart.historyLoading = true;
    telemetryChart.historyRetryAt = 0;
    const seq = ++telemetryChart.historyRequestSeq;
    const requestId = `hist-${Date.now().toString(36)}-${seq.toString(36)}`.slice(0, 39);
    telemetryChart.activeRequestId = requestId;
    telemetrySetStatus('Đang đọc EEPROM 30 phút gần nhất…');
    try {
      const body = { v: 1, requestId, minutes: 30 };
      const envelope = await signMqttWrite(device, 'history/request', body);
      if (telemetryChart.deviceId !== device.id || telemetryChart.activeRequestId !== requestId) return;
      publish(topics(device.id).historyRequest, envelope, { qos: 1, retain: false });
      window.setTimeout(() => {
        if (telemetryChart.activeRequestId !== requestId || !telemetryChart.historyLoading) return;
        telemetryChart.historyLoading = false;
        telemetryChart.historyRetryAt = Date.now() + 30_000;
        telemetrySetStatus('Không nhận được lịch sử · vẫn cập nhật trực tiếp');
        requestTemperatureChartRender();
      }, 8000);
    } catch (error) {
      if (telemetryChart.activeRequestId !== requestId) return;
      telemetryChart.historyLoading = false;
      telemetryChart.historyRetryAt = Date.now() + 30_000;
      telemetrySetStatus(error?.message || 'Không đọc được lịch sử');
    }
  }

  function handleTemperatureHistory(device, payload) {
    if (!device || device.id !== state.selectedId) return;
    telemetryEnsureDevice(device);
    if (String(payload?.requestId || '') !== telemetryChart.activeRequestId) return;
    const samples = Array.isArray(payload?.samples) ? payload.samples : [];
    telemetryMerge(samples.map((row) => ({
      t: Number(row?.[0]) * 1000,
      temperature: Number(row?.[1]),
    })));
    if (payload?.done) {
      telemetryChart.historyLoading = false;
      telemetryChart.historyLoaded = true;
      telemetryChart.historyLoadedAt = Date.now();
      telemetryChart.historyRetryAt = 0;
      telemetrySetStatus(telemetryChart.points.length
        ? 'EEPROM 5 phút · cập nhật trực tiếp'
        : 'EEPROM chưa có dữ liệu · cập nhật trực tiếp');
    }
    requestTemperatureChartRender();
  }

  function feedTelemetrySnapshot(device, snapshot) {
    if (!device || device.id !== state.selectedId) return;
    telemetryEnsureDevice(device);
    const temperature = Number(snapshot?.runtime?.temperature);
    if (!Number.isFinite(temperature) || temperature < -20 || temperature > 100) return;
    const value = $('temperatureChartValue');
    if (value) value.textContent = `${numberVi(temperature)}°C`;
    const now = Date.now();
    if (!telemetryChart.lastSampleAt || now - telemetryChart.lastSampleAt >= TELEMETRY_LIVE_SAMPLE_MS) {
      telemetryChart.lastSampleAt = now;
      telemetryMerge([{ t: now, temperature }]);
    }
    if (document.body.dataset.page === 'batch' &&
        (!telemetryChart.historyLoaded || Date.now() - telemetryChart.historyLoadedAt >= TELEMETRY_HISTORY_REFRESH_MS)) {
      loadTelemetryHistory();
    }
    requestTemperatureChartRender();
  }

  function requestTemperatureChartRender() {
    if (telemetryChart.renderRaf || typeof requestAnimationFrame !== 'function') return;
    telemetryChart.renderRaf = requestAnimationFrame(() => {
      telemetryChart.renderRaf = 0;
      renderTemperatureChart();
    });
  }

  function renderTemperatureChart() {
    const canvas = $('temperatureChartCanvas');
    if (!canvas) return;
    const wrap = canvas.parentElement;
    const widthCss = Math.max(280, Math.floor(wrap?.clientWidth || canvas.clientWidth || 280));
    const heightCss = Math.max(190, Math.floor(wrap?.clientHeight || canvas.clientHeight || 240));
    const dpr = Math.max(1, Math.min(2, Number(window.devicePixelRatio) || 1));
    const width = Math.floor(widthCss * dpr);
    const height = Math.floor(heightCss * dpr);
    if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, widthCss, heightCss);

    const now = Date.now();
    const start = now - TELEMETRY_WINDOW_MS;
    const points = telemetryChart.points.filter((point) => point.t >= start && point.t <= now + 5000);
    const empty = $('temperatureChartEmpty');
    if (empty) empty.hidden = points.length > 0;

    const css = getComputedStyle(document.documentElement);
    const color = (name, fallback) => css.getPropertyValue(name).trim() || fallback;
    const gridColor = color('--lineSoft', '#dce7e3');
    const textColor = color('--muted', '#6a7d78');
    const liveColor = color('--primary', '#0d9488');
    const setColor = color('--warning', '#e29b1d');
    const left = 42, right = 10, top = 12, bottom = 12;
    const plotW = Math.max(1, widthCss - left - right);
    const plotH = Math.max(1, heightCss - top - bottom);
    const target = Number(currentDevice()?.config?.targetTemp ?? $('batchTarget')?.value);
    const values = points.map((point) => point.temperature);
    if (Number.isFinite(target)) values.push(target);
    let yMin = values.length ? Math.min(...values) : 36.5;
    let yMax = values.length ? Math.max(...values) : 38.5;
    const spread = Math.max(0.5, yMax - yMin);
    const pad = Math.max(0.25, spread * 0.22);
    yMin = Math.floor((yMin - pad) * 10) / 10;
    yMax = Math.ceil((yMax + pad) * 10) / 10;
    if (yMax - yMin < 1) { const mid = (yMax + yMin) / 2; yMin = mid - 0.5; yMax = mid + 0.5; }
    const xFor = (t) => left + ((t - start) / TELEMETRY_WINDOW_MS) * plotW;
    const yFor = (v) => top + (1 - ((v - yMin) / (yMax - yMin))) * plotH;

    ctx.font = '11px Inter, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif';
    ctx.textAlign = 'right'; ctx.textBaseline = 'middle'; ctx.lineWidth = 1;
    for (let i = 0; i < 4; i += 1) {
      const ratio = i / 3, y = top + ratio * plotH, value = yMax - ratio * (yMax - yMin);
      ctx.strokeStyle = gridColor; ctx.setLineDash([]); ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(widthCss - right, y); ctx.stroke();
      ctx.fillStyle = textColor; ctx.fillText(`${numberVi(value)}°`, left - 7, y);
    }
    if (Number.isFinite(target) && target >= yMin && target <= yMax) {
      const y = yFor(target); ctx.strokeStyle = setColor; ctx.lineWidth = 1.25; ctx.setLineDash([6, 5]);
      ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(widthCss - right, y); ctx.stroke();
    }
    if (points.length) {
      ctx.strokeStyle = liveColor; ctx.lineWidth = 2.25; ctx.lineJoin = 'round'; ctx.lineCap = 'round'; ctx.setLineDash([]); ctx.beginPath();
      points.forEach((point, index) => { const x = xFor(point.t), y = yFor(point.temperature); if (!index) ctx.moveTo(x, y); else ctx.lineTo(x, y); });
      ctx.stroke();
      const last = points[points.length - 1]; ctx.fillStyle = liveColor; ctx.beginPath(); ctx.arc(xFor(last.t), yFor(last.temperature), 3, 0, Math.PI * 2); ctx.fill();
      const value = $('temperatureChartValue'); if (value) value.textContent = `${numberVi(last.temperature)}°C`;
    } else {
      const value = $('temperatureChartValue'); if (value && !value.textContent) value.textContent = '—';
    }
  }
'''
text = replace_once(text, '\n  function renderBatchLogs() {', chart_code + '\n\n  function renderBatchLogs() {', 'insert chart code')
text = replace_once(text,
    "  document.addEventListener('visibilitychange', () => {",
    "  window.addEventListener('resize', requestTemperatureChartRender, { passive: true });\n\n  document.addEventListener('visibilitychange', () => {",
    'chart resize')
write(path, text)


# ---------------- styles.css ----------------
path = 'styles.css'
text = read(path).rstrip() + '\n'
telemetry_css = r'''

/* ====================== Telemetry chart: nhiet do 30 phut ====================== */
.telemetryPanel{margin-top:14px;padding:18px 20px;min-width:0}
.telemetryHead{align-items:center}
.telemetryPanel h2{font-size:var(--fs-lg);font-weight:700}
.telemetryLegend{display:flex;align-items:center;justify-content:flex-end;gap:12px;flex-wrap:wrap;color:var(--muted);font-size:var(--fs-2xs)}
.telemetryLegend span{display:inline-flex;align-items:center;gap:6px;white-space:nowrap}
.telemetryLegend i{display:inline-block;width:18px;height:2px;border-radius:2px;background:var(--primary)}
.telemetryLegend i.setLine{height:0;background:transparent;border-top:2px dashed var(--warning)}
.telemetryMeta{display:flex;align-items:baseline;justify-content:space-between;gap:12px;margin-top:12px;min-width:0}
.telemetryMeta strong{font-size:var(--fs-xl);font-variant-numeric:tabular-nums;white-space:nowrap}
.telemetryMeta span{min-width:0;color:var(--muted);font-size:var(--fs-2xs);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;text-align:right}
.telemetryCanvasWrap{position:relative;margin-top:8px;width:100%;height:250px;min-height:190px;border:1px solid var(--lineSoft);border-radius:var(--radius-sm);background:var(--tile);overflow:hidden}
#temperatureChartCanvas{display:block;width:100%;height:100%}
.telemetryEmpty{position:absolute;inset:0;display:grid;place-items:center;padding:20px;color:var(--muted);font-size:var(--fs-xs);text-align:center;pointer-events:none}
.telemetryEmpty[hidden]{display:none}
.telemetryAxis{display:flex;justify-content:space-between;gap:8px;margin:7px 3px 0 42px;color:var(--muted);font-size:var(--fs-2xs);font-variant-numeric:tabular-nums}
.diagnosticsLogBody .currentActivity{margin-top:0}
@media(max-width:720px){.telemetryPanel{padding:13px 15px}.telemetryHead{align-items:flex-start}.telemetryLegend{gap:8px}.telemetryCanvasWrap{height:220px}.telemetryAxis{margin-left:38px}}
@media(min-height:481px){body[data-page="batch"] .telemetryPanel{flex:1 1 auto;min-height:250px;margin-top:0;overflow:hidden;display:flex;flex-direction:column}body[data-page="batch"] .telemetryCanvasWrap{flex:1 1 auto;height:auto;min-height:180px}}
@media(max-height:480px){body[data-page="batch"] .telemetryPanel{display:flex;flex-direction:column;min-height:260px;margin-top:14px;overflow:hidden}}
@media(min-width:1025px){body[data-page="batch"] .telemetryPanel{margin-top:0;min-height:0}}
'''
if '.telemetryPanel{' in text:
    raise SystemExit('PATCH FAIL: telemetry CSS already exists')
write(path, text + telemetry_css.lstrip('\n'))


# ---------------- service worker / manifest ----------------
path = 'sw.js'
text = replace_once(read(path), "const CACHE = 'mayap-web-v11.7.5';", "const CACHE = 'mayap-web-v11.8.0';", 'sw cache')
write(path, text)
manifest = json.loads(read('release-manifest.json'))
manifest['release'] = '3.8.3'
manifest['firmware'] = '3.8.3'
manifest['web'] = '11.8.0'
write('release-manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')


# ---------------- regression checker ----------------
checker = r'''#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def text(path):
    return (ROOT / path).read_text(encoding='utf-8')

def need(cond, message):
    if not cond:
        raise SystemExit('EEPROM HISTORY CHECK FAIL: ' + message)

cfg = text('MAYAP_INDUSTRIAL_v3_4_0/config.h')
hist = text('MAYAP_INDUSTRIAL_v3_4_0/history_store.h')
rt = text('MAYAP_INDUSTRIAL_v3_4_0/realtime_link.h')
mc = text('MAYAP_INDUSTRIAL_v3_4_0/machine_control.h')
app = text('app.js')
html = text('index.html')
worker = text('cloudflare/src/index.js')

def num(name, base=10):
    m = re.search(rf'{name}\s*=\s*(0x[0-9A-Fa-f]+|\d+)U', cfg)
    need(m, 'thieu ' + name)
    return int(m.group(1), 0)

need(num('EEPROM_CAPACITY_BYTES') == 4096, 'khong phai AT24C32 4KB')
need(num('EEPROM_PAGE_SIZE') == 32, 'page AT24C32 phai 32B')
base = num('EEPROM_ADDR_TEMP_HISTORY')
slots = num('TEMP_HISTORY_SLOT_COUNT')
rec = num('TEMP_HISTORY_RECORD_BYTES')
interval = num('TEMP_HISTORY_SAMPLE_SEC')
need(base == 0x0B00, 'history base phai 0x0B00')
need(slots == 288 and rec == 4 and interval == 300, 'layout 24h/5phut sai')
need(base + slots * rec <= 4096, 'history vuot EEPROM')
need((base % 32) == 0 and (32 % rec) == 0, 'record phai can page')
need('lastSampleBucket' in hist and 'crc8' in hist, 'thieu anti-hotspot/CRC')
need('EEPROM_ADDR_TEMP_HISTORY +' in hist, 'history khong dung vung rieng')
need('mayapTemperatureHistorySample' in mc, 'MachineController chua sample history')
need('history/request' in rt and 'history/reported' in rt, 'MQTT history contract thieu')
need('verifyAndDispatch("history/request"' in rt, 'history request khong HMAC')
need("'history/request'" in worker, 'Worker chua cho ky history/request')
need('temperatureChartCanvas' in html and html.count('id="batchLogList"') == 1, 'UI chart/log sai')
need('history/reported' in app and "signMqttWrite(device, 'history/request'" in app, 'web MQTT history sai')
need('/api/device/history' not in app, 'web van phu thuoc Cloud history')
need('telemetry_history' not in worker, 'Worker runtime khong duoc luu telemetry')

# Wear worst-case: 4-byte record, page 32B => 8 writes/page/day. Datasheet minimum
# 1,000,000 page-write cycles @25C => >300 nam ly thuyet; chi check kien truc.
writes_per_page_day = 32 // rec
need(writes_per_page_day == 8, 'wear distribution khong nhu thiet ke')
print(f'EEPROM history checks: OK base=0x{base:04X} bytes={slots*rec} writes/page/day={writes_per_page_day}')
'''
write('tools/check_eeprom_history.py', checker)

# add checker to CI gates
path = '.github/workflows/reliability-checks.yml'
text = read(path)
text = replace_once(text,
    '      - name: Static reliability regression checks\n        run: python3 tools/check_v381_reliability.py\n',
    '      - name: Static reliability regression checks\n        run: python3 tools/check_v381_reliability.py\n\n      - name: EEPROM temperature history regression checks\n        run: python3 tools/check_eeprom_history.py\n',
    'reliability history check')
write(path, text)

path = '.github/workflows/build-firmware.yml'
text = read(path)
text = replace_once(text,
    '      - name: Kiem tra reliability truoc build/release\n        run: python3 tools/check_v381_reliability.py\n',
    '      - name: Kiem tra reliability truoc build/release\n        run: python3 tools/check_v381_reliability.py\n\n      - name: Kiem tra lich su nhiet AT24C32\n        run: python3 tools/check_eeprom_history.py\n',
    'build history check')
write(path, text)

print('EEPROM temperature history patch applied')
