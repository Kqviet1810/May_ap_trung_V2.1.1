const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const dir = 'MAYAP_INDUSTRIAL_v4_0_0/';
const read = name => fs.readFileSync(path.join(__dirname, '..', name), 'utf8');

test('startup permission follows a non-splash LCD transfer and successful presence probe', () => {
  const source = read(dir + 'hmi.h');
  const render = source.slice(source.indexOf('void render(uint32_t now)'), source.indexOf('// 7. API HMI'));
  assert.ok(render.indexOf('lcd.sendBuffer()') < render.indexOf('mayapBootAcknowledgeHomeFrame()'));
  assert.match(render, /if \(!splashActive\) mayapBootAcknowledgeHomeFrame/);
  assert.match(render, /if \(!startupFrameOk\)[\s\S]*?return;/);
  assert.match(render, /startupFrameOk = splashActive \|\| mayapBootOperationsReady\(\) \|\|\s*probeLcdUnlocked\(\)/);
  const machine = read(dir + 'machine_control.h');
  for (const action of ['startBatch', 'startAutoTune', 'enterTestMode'])
    assert.match(machine, new RegExp(`bool ${action}\\([^)]*\\) \\{\\s*if \\(!mayapBootOperationsReady\\(\\)\\)`));
  assert.match(machine, /mayapApplyStartupOutputPolicy\(request, mayapBootOperationsReady\(\)/);
  assert.match(machine, /const bool operationsReady = mayapBootOperationsReady\(\);[\s\S]*?if \(operationsReady\) \{[\s\S]*?processResume\(now\);[\s\S]*?updateTurning\(now\);/);
});
test('healthy static LCD menus are never periodically blanked/reinitialized or redrawn twice', () => {
  for (const file of ['hmi.h', 'config.h'])
    assert.doesNotMatch(read(dir + file), /LCD_FULL_REINIT|HMI_IDLE_SELFHEAL|HMI_VERIFY_REDRAW|uiVerifyFrames|lastLcdFullReinit/);
  assert.match(read(dir + 'hmi.h'), /if \(!ok\) \{\s*lcdReady = false;/);
  const assets = read(dir + 'boot_assets.h');
  assert.match(assets, /BOOT_LOGO_HEIGHT = 32U/);
  assert.match(assets, /BOOT_STATUS_HEIGHT = 12U/);
});
test('Tiny keeps v4 wire framing but uses whole-wave RMT and directly registered IRAM GPIO handlers', () => {
  const source = read(dir + 'attiny_bus.h');
  assert.doesNotMatch(source, /ESP_TIMER_TASK|esp_timer_start_once|attachInterrupt\(|noInterrupts\(/);
  assert.match(source, /txConfig.flags.io_od_mode = 1U/);
  assert.match(source, /txConfig.flags.eot_level = 1U/);
  assert.match(source, /txSymbols_\[9\]/);
  assert.match(source, /gpio_isr_handler_add\([^;]*busIsr, nullptr/);
  assert.match(source, /busIsr\(void \*\)[\s\S]*?esp_timer_get_time\(\)/);
  const encoder = read(dir + 'hmi.h');
  const isr = encoder.slice(encoder.indexOf('void IRAM_ATTR rotaryEncoderIsr'), encoder.indexOf('void beginRotary()'));
  assert.match(isr, /gpio_ll_get_level\(&GPIO/);
  assert.doesNotMatch(isr, /gpio_get_level|digitalRead|micros\(|Serial/);
  assert.doesNotMatch(encoder, /attachInterrupt\(/); // Arduino wrapper also lives in flash in core 3.3.11.
});
