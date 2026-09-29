// Browser QA with isolated mock transport. Never connects to a real machine.
// Usage: MAYAP_PLAYWRIGHT=<package path> node tools/test_web_experience.cjs <output-dir>
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require(process.env.MAYAP_PLAYWRIGHT || 'playwright');
const root = path.resolve(__dirname, '..');
const out = path.resolve(process.argv[2] || path.join(root, 'work', 'web-qa'));
fs.mkdirSync(out, { recursive: true });
const source = fs.readFileSync(path.join(root, 'app.js'), 'utf8').replace('  init();', `
  window.__qa = { state, REQUIRED_CONFIG_KEYS, VENT_PROFILE_KEYS, handleConfigReport,
    handleSnapshot, showPage, buildConfig, createDevice, renderSelector, connectionStatus };
  init();`);
const firmware = fs.readFileSync(path.join(root, 'MAYAP_INDUSTRIAL_v4_0_0/config.h'), 'utf8');
const defaults = {};
for (const match of firmware.matchAll(/\b(?:float|bool|uint8_t|uint16_t|uint32_t)\s+(\w+)\s*=\s*(true|false|\d+(?:\.\d+)?)(?:f|U|UL)?\s*;/g))
  defaults[match[1]] = match[2] === 'true' ? true : match[2] === 'false' ? false : Number(match[2]);
const fakeMqtt = `
window.__transport = { subscriptions: [], sessions: [], connects: 0, dropFirst: false };
window.mqtt = { connect() {
  window.__transport.connects++;
  const handlers = {}, client = { connected: false, disconnecting: false,
    on(name, fn) { (handlers[name] ||= []).push(fn); return client; },
    emit(name, ...args) { (handlers[name] || []).forEach(fn => fn(...args)); },
    subscribe(filters, cb) { window.__transport.subscriptions.push(filters);
      setTimeout(() => cb(null, Object.entries(filters).map(([topic, v]) => ({ topic, qos: v.qos }))), 20); },
    unsubscribe() {}, reconnect() { client.emit('connect'); },
    end() { client.disconnecting = true; client.emit('close'); },
    publish(topic, wire, options, cb) {
      cb?.(); if (!topic.endsWith('/session') || !wire) return;
      const msg = JSON.parse(wire); window.__transport.sessions.push({ at: performance.now(), ...msg });
      if (!msg.active || !msg.sync) return;
      if (window.__transport.dropFirst) { window.__transport.dropFirst = false; return; }
      setTimeout(() => window.__deliver?.(), 25);
    }
  };
  setTimeout(() => { client.connected = true; client.emit('connect'); }, 20);
  return client;
} };`;

async function setup(browser, options = {}) {
  let authFailures = options.authFailures || 0;
  const context = await browser.newContext({ viewport: { width: options.width || 390, height: 844 },
    isMobile: Boolean(options.mobile), hasTouch: Boolean(options.mobile), serviceWorkers: 'block', colorScheme: options.scheme || 'light' });
  await context.addInitScript(({ theme, paired, defaults, dropFirst }) => {
    window.__nativeStorageGet = Storage.prototype.getItem;
    if (theme) localStorage.setItem('mayap.theme', theme);
    if (paired) {
      localStorage.setItem('mayap.web.v10.devices', JSON.stringify([{ id: 'MAP-1234567890AB', name: 'Máy ấp nhà mình', pairingToken: 'qa-token' }]));
      localStorage.setItem('mayap.web.v10.selected', 'MAP-1234567890AB');
    }
    window.__deliver = () => {
      const h = window.__qa, d = h.state.devices[0]; if (!d) return;
      const config = Object.fromEntries(h.REQUIRED_CONFIG_KEYS.concat(h.VENT_PROFILE_KEYS).map(key => [key, defaults[key] ?? 0]));
      config.ventAutoEnabled = true;
      d.presence = { online: true, fw: '4.0.0', proto: 2, ssid: 'Wi-Fi gia đình' }; d.presenceAt = Date.now();
      h.handleConfigReport(d, { revision: 1, bootId: 123, config });
      h.handleSnapshot(d, { revision: 1, bootId: 123, runtime: { temperature: 37.5, humidity: 58,
        heaterPower: 25, circulationFanOn: true, ventFanOn: false, turningEnabled: true,
        batchRunning: true, currentDay: 7, turnState: 3, nextTurnMinutes: 52, faults: [] } });
    };
    document.addEventListener('DOMContentLoaded', () => { window.__transport.dropFirst = dropFirst; });
  }, { theme: options.theme, paired: options.paired !== false, defaults, dropFirst: Boolean(options.dropFirst) });
  await context.route('**/*', async route => {
    const url = new URL(route.request().url());
    if (url.origin === 'http://127.0.0.1:8765') {
      if (url.pathname === '/app.js') return route.fulfill({ contentType: 'application/javascript', body: source });
      if (url.pathname === '/vendor/mqtt.min.js') return route.fulfill({ contentType: 'application/javascript', body: fakeMqtt });
      return route.continue();
    }
    let body = { success: false, error: 'QA fixture' };
    if (url.pathname.endsWith('/mqtt-session') && authFailures-- > 0)
      return route.fulfill({status:503,contentType:'application/json',body:JSON.stringify(body)});
    if (url.pathname.endsWith('/mqtt-session') || url.pathname.endsWith('/verify-pin')) body = {
      success: true, pairing_token: 'qa-token', device_name: 'Máy ấp nhà mình',
      mqtt: { url: 'wss://qa.invalid/mqtt', username: 'qa', password: 'qa' },
      control: { sessionId: 'qa-session', sessionKey: '07'.repeat(32), expiresAt: Math.floor(Date.now()/1000)+300 } };
    if (url.pathname.endsWith('/firmware/latest')) body = { success: true, version: '4.0.0' };
    return route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
  });
  const page = await context.newPage(), errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('http://127.0.0.1:8765', { waitUntil: 'networkidle' });
  if (options.paired !== false) await page.waitForFunction(() => window.__qa.state.devices[0]?.snapshotAt);
  assert.deepEqual(errors, [], 'Browser exceptions');
  return { context, page, errors };
}

async function swipe(page, x, y, dx, dy = 0) {
  const cdp = await page.context().newCDPSession(page);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y }] });
  for (let i=1; i<=6; ++i) await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x:x+dx*i/6, y:y+dy*i/6 }] });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await cdp.detach();
}

async function main() {
  const executablePath = process.env.MAYAP_CHROME || (process.platform==='win32' ? 'C:/Program Files/Google/Chrome/Application/chrome.exe' : undefined);
  const browser = await chromium.launch({ executablePath, headless: true });
  const results = [];
  try {
    for (const width of [320, 390, 430, 768, 1440]) for (const theme of ['light', 'dark']) {
      const { context, page, errors } = await setup(browser, { width, mobile: width < 800, theme });
      for (const tab of ['device', 'batch', 'settings']) {
        await page.evaluate(tab => window.__qa.showPage(tab), tab);
        const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
        assert.equal(overflow, false, `${width}/${theme}/${tab} horizontal overflow`);
      }
      assert.equal(await page.title(), 'MAYAP · Máy ấp trứng');
      const contrast = await page.evaluate(() => {
        const style=getComputedStyle(document.documentElement), canvas=document.createElement('canvas'), ctx=canvas.getContext('2d');
        function luminance(color) { ctx.fillStyle=color;ctx.fillRect(0,0,1,1);const rgb=ctx.getImageData(0,0,1,1).data;
          return Array.from(rgb).slice(0,3).map(x=>x/255).map(x=>x<=.04045 ? x/12.92 : ((x+.055)/1.055)**2.4)
            .reduce((sum,x,i)=>sum+x*[.2126,.7152,.0722][i],0); }
        const value=name=>name==='white' ? '#fff' : style.getPropertyValue('--'+name).trim();
        return [['ink','surface'],['muted','surface'],['label','tile'],['primary2','primarySoft'],['white','primary'],['white','dangerAction'],['white','attentionBg']]
          .map(([fg,bg])=>{const a=luminance(value(fg)),b=luminance(value(bg));return {fg,bg,ratio:(Math.max(a,b)+.05)/(Math.min(a,b)+.05)};});
      });
      for (const pair of contrast) assert.ok(pair.ratio>=4.5, `${theme}: ${pair.fg}/${pair.bg} contrast ${pair.ratio}`);
      assert.equal(await page.locator('#ventSettingCard').isVisible(), true);
      assert.equal(await page.locator('#ventProfileFields').evaluate(el => el.disabled), false);
      assert.equal(await page.locator('#sensorForm').evaluate(el => el.closest('.settingGroup').getAttribute('aria-labelledby')), 'settings-temperature');
      assert.equal(await page.locator('#ventForm').evaluate(el => el.closest('.settingGroup').getAttribute('aria-labelledby')), 'settings-ventilation');
      assert.equal(await page.locator('#wifiForm').innerText().then(text => /ESP32|HMI/.test(text)), false);
      if (width === 390 || width === 1440) {
        await page.evaluate(() => window.scrollTo({ top:0, behavior:'instant' }));
        await page.screenshot({ path: path.join(out, `MAYAP-Web-${width}-${theme}.png`), fullPage: true });
        await page.locator('#ventSettingCard > summary').click();
        await page.locator('#ventSettingCard .settingHintNote > summary').click();
        await page.evaluate(() => window.scrollTo({ top:0, behavior:'instant' }));
        await page.screenshot({ path: path.join(out, `MAYAP-Quạt-hút-${width}-${theme}.png`), fullPage: true });
        if (width === 390) {
          await page.evaluate(() => { const box=document.getElementById('ventSettingCard').getBoundingClientRect();
            window.scrollTo({top:window.scrollY+box.top-96,behavior:'instant'}); });
          await page.screenshot({ path:path.join(out,`MAYAP-Quạt-hút-mobile-${theme}.png`) });
        }
      }
      // Legacy config preserves thermal fan controls and disables unsupported stage profiles.
      await page.evaluate(() => { const h=window.__qa,d=h.state.devices[0]; const config={...d.config};
        h.VENT_PROFILE_KEYS.forEach(key=>delete config[key]); h.handleConfigReport(d,{config,revision:2,bootId:123}); });
      assert.equal(await page.locator('#ventProfileFields').evaluate(el => el.disabled), true);
      assert.equal(await page.locator('#ventSettingCard').isVisible(), true);
      assert.match(await page.locator('#ventSummary').textContent(), /ngưỡng nhiệt độ/);
      assert.deepEqual(errors, []);
      results.push(`${width}/${theme}: three tabs, grouping, fan capability, no overflow`);
      await context.close();
    }
    const { context, page } = await setup(browser, { width:390, mobile:true, theme:'light', dropFirst:true });
    const sessions = await page.evaluate(() => window.__transport.sessions);
    assert.ok(sessions.length >= 2);
    const retryMs = sessions[1].at-sessions[0].at;
    assert.ok(retryMs >= 650 && retryMs < 1200);
    assert.equal(await page.evaluate(() => window.__transport.subscriptions.length), 1);
    await swipe(page, 260, 35, -140);
    assert.equal(await page.evaluate(() => document.body.dataset.page), 'batch');
    await swipe(page, 260, 35, -140);
    assert.equal(await page.evaluate(() => document.body.dataset.page), 'settings');
    await swipe(page, 150, 35, 140);
    assert.equal(await page.evaluate(() => document.body.dataset.page), 'batch');
    await swipe(page, 200, 300, 0, -120);
    assert.equal(await page.evaluate(() => document.body.dataset.page), 'batch');
    await page.locator('#batchName').fill('Mẻ đang sửa');
    const box = await page.locator('#batchName').boundingBox();
    await swipe(page, box.x+box.width-20, box.y+box.height/2, -100);
    assert.equal(await page.evaluate(() => document.body.dataset.page), 'batch');
    await page.evaluate(() => { window.__qa.showPage('settings'); window.__qa.showPage('batch'); });
    assert.equal(await page.locator('#batchName').inputValue(), 'Mẻ đang sửa');
    await page.evaluate(() => document.getElementById('deviceDialog').showModal());
    await swipe(page, 260, 200, -140);
    assert.equal(await page.evaluate(() => document.body.dataset.page), 'batch');
    results.push(`Touch swipe: adjacent tabs, vertical scroll, input drag, dirty values, modal guards. Lost first sync retry ${Math.round(retryMs)}ms.`);
    await context.close();
    const pairing = await setup(browser, { width:390, mobile:true, paired:false });
    let navigations = 0; pairing.page.on('framenavigated', frame => { if (frame===pairing.page.mainFrame()) navigations++; });
    await pairing.page.locator('#addDeviceBtn').click();
    await pairing.page.locator('#newDeviceId').fill('MAP-1234567890AB');
    await pairing.page.locator('#newDevicePin').fill('1234');
    await pairing.page.locator('#addDeviceForm button[type="submit"]').click();
    await pairing.page.waitForFunction(() => window.__qa.state.devices[0]?.snapshotAt);
    await pairing.page.waitForTimeout(650);
    assert.equal(navigations, 0, 'Pairing must not reload the document');
    assert.equal(await pairing.page.evaluate(() => window.__transport.connects), 1);
    assert.equal(await pairing.page.evaluate(() => window.__nativeStorageGet.call(localStorage, 'mayap.web.v10.mqtt.private')), null,
      'Credentials must not persist in native storage');
    assert.deepEqual(pairing.errors, []);
    results.push('Pairing: legacy PIN accepted, auth remains, no reload, one MQTT client, credentials absent from persistent storage.');
    await pairing.context.close();
    const retry = await setup(browser, { authFailures:1, width:390, mobile:true });
    assert.equal(await retry.page.evaluate(() => window.__transport.connects), 1);
    assert.deepEqual(retry.errors, []);
    results.push('Transient startup authentication 503: retries in background and connects without reload. Text/action contrast >=4.5 in both themes.');
    await retry.context.close();
    const system = await setup(browser, {scheme:'dark', width:390, mobile:true});
    assert.equal(await system.page.evaluate(() => getComputedStyle(document.documentElement).getPropertyValue('--surface').trim()), '#162431');
    results.push('System theme follows dark OS preference.');
    await system.context.close();
    fs.writeFileSync(path.join(out,'web-browser-qa.json'),JSON.stringify({ passed:true, results },null,2));
    console.log(results.join('\n'));
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode=1; });
