#!/usr/bin/env python3
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
