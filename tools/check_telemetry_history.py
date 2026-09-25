#!/usr/bin/env python3
from pathlib import Path
import json
import re
import sqlite3

ROOT = Path(__file__).resolve().parents[1]

def text(path):
    return (ROOT / path).read_text(encoding='utf-8')

def require(cond, message):
    if not cond:
        raise SystemExit(message)

schema = text('cloudflare/schema.sql')
migration = text('cloudflare/migrations/0003_telemetry_history.sql')
db = text('cloudflare/src/db.js')
worker = text('cloudflare/src/index.js')
firmware = text('MAYAP_INDUSTRIAL_v3_4_0/cloud_alert_link.h')
app = text('app.js')
html = text('index.html')
css = text('styles.css')
sw = text('sw.js')
manifest = json.loads(text('release-manifest.json'))

require('CREATE TABLE IF NOT EXISTS telemetry_history' in schema, 'schema thieu telemetry_history')
require('idx_telemetry_device_time' in schema and 'idx_telemetry_time' in schema, 'schema thieu telemetry indexes')
require('telemetry_history' in migration, 'migration 0003 khong tao telemetry_history')
require('insertTelemetrySample' in db and 'getTelemetryHistory' in db and 'pruneTelemetryHistory' in db, 'db.js thieu telemetry helpers')
require("'/api/device/history'" in worker and 'pairingToken !== device.pairing_token' in worker, 'history API thieu route/xac thuc pairing token')
require('ctx.waitUntil(insertTelemetrySample' in worker, 'telemetry write phai best-effort qua ctx.waitUntil')
require('TELEMETRY_RETENTION_MS' in worker and 'pruneTelemetryHistory' in worker, 'thieu telemetry retention')
require('doc["temperature"] = processingRuntime.temperature' in firmware, 'firmware heartbeat thieu temperature')
require('doc["humidity"] = processingRuntime.humidity' in firmware, 'firmware heartbeat thieu humidity')
require(html.count('id="batchLogList"') == 1, 'batchLogList phai chi con 1 ID sau khi di chuyen')
require('id="temperatureChartCanvas"' in html and 'id="diagnosticsLogSetting"' in html, 'HTML thieu chart/diagnostics')
require('loadTelemetryHistory' in app and 'feedTelemetrySnapshot' in app and 'TELEMETRY_LIVE_SAMPLE_MS = 5000' in app, 'app.js thieu history/live sampler')
require('Telemetry chart: nhiet do 30 phut' in css, 'styles.css thieu telemetry styles')
require('chart.js' not in html.lower() and 'chart.js' not in app.lower(), 'khong duoc them Chart.js/CDN cho bieu do nho nay')
require("mayap-web-v11.8.0" in sw, 'sw cache chua bump 11.8.0')
require(manifest.get('web') == '11.8.0', 'release manifest web != 11.8.0')

# Dam bao khong vo tinh tang tan suat MQTT hien tai.
config = text('MAYAP_INDUSTRIAL_v3_4_0/config.h')
require(re.search(r'WEB_SNAPSHOT_ACTIVE_INTERVAL_MS\s*=\s*400UL', config), 'MQTT active snapshot interval da bi thay doi')
require(re.search(r'WEB_SNAPSHOT_IDLE_INTERVAL_MS\s*=\s*6000UL', config), 'MQTT idle snapshot interval da bi thay doi')

# Smoke-test schema/query/index bang SQLite (D1 dung SQLite semantics).
conn = sqlite3.connect(':memory:')
conn.executescript(schema)
rows = [
    ('MAP-001122AABBCC', 1000, 37.4, 58.0, 1),
    ('MAP-001122AABBCC', 2000, 37.5, 59.0, 1),
    ('MAP-FFEEDDCCBBAA', 2000, 36.9, 55.0, 0),
]
conn.executemany('INSERT INTO telemetry_history(device_id, recorded_at, temperature, humidity, batch_running) VALUES (?,?,?,?,?)', rows)
got = conn.execute('SELECT recorded_at, temperature FROM telemetry_history WHERE device_id=? AND recorded_at>=? ORDER BY recorded_at ASC', ('MAP-001122AABBCC', 1000)).fetchall()
require(got == [(1000, 37.4), (2000, 37.5)], f'history query sai: {got}')
plan = ' '.join(str(row) for row in conn.execute('EXPLAIN QUERY PLAN SELECT recorded_at, temperature FROM telemetry_history WHERE device_id=? AND recorded_at>=? ORDER BY recorded_at ASC', ('MAP-001122AABBCC', 1000)).fetchall())
require('idx_telemetry_device_time' in plan, f'query khong dung device/time index: {plan}')
conn.execute('DELETE FROM telemetry_history WHERE recorded_at < ?', (1500,))
require(conn.execute('SELECT COUNT(*) FROM telemetry_history').fetchone()[0] == 2, 'retention delete sai')

print('Telemetry history/chart regression checks: OK')
