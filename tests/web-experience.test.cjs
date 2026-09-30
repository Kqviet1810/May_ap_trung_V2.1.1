const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const { webcrypto } = require('node:crypto');
const { EventEmitter } = require('node:events');
const protocol = require('../protocol_v2.js');

function browser(overrides = {}) {
  const source = fs.readFileSync(require.resolve('../app.js'), 'utf8').replace(/  init\(\);\s*\}\)\(\);\s*$/, `
    renderDevice = () => {}; renderReminderList = () => {}; renderPushStatus = () => {};
    applyConfigToUi = () => {}; clearInvalid = () => {};
    invalidate = (form, id) => { window.invalidField = id; return false; };
    Object.assign(window.hooks, { state, subscribeDevice, activateSelectedSession,
      selectedNeedsSync, deactivateSession, connectMqtt, supportsVentProfile,
      swipeDestination, buildConfig, validateVentForm, REQUIRED_CONFIG_KEYS,
      VENT_PROFILE_KEYS, createDevice, connectionStatus, recoverBrowserConnection,
      refreshMqttSession, postCloudJson, isDeviceOnline, sendCommand });
  })();`);
  let now = 0, timerId = 0;
  const timers = new Map(), elements = new Map(), clients = [];
  const window = { hooks: {}, addEventListener() {}, MayapProtocolV2: protocol,
    MAYAP_WEB_CONFIG: { cloudApiBase:'https://test.invalid', mqttUrl: 'wss://test.invalid/mqtt', mqttUsername: 'test', mqttPassword: 'test', sessionRefreshMs: 3000, ...overrides },
    mqtt: { connect(url, options) { const c = new EventEmitter(); c.connected = false;
      c.end = () => { c.disconnecting = true; c.emit('close'); }; c.publish = () => {};
      c.subscribe = () => {}; clients.push(c); return c; } } };
  const document = { hidden: false, addEventListener() {}, getElementById: id => elements.get(id) };
  const context = { window, document, crypto: webcrypto, URL, URLSearchParams, TextEncoder, AbortController,
    localStorage: { getItem: () => null, setItem() {} }, console,
    performance: { now: () => now },
    setTimeout(fn, delay) { const id = ++timerId; timers.set(id, { fn, delay }); return id; },
    clearTimeout: id => timers.delete(id), setInterval: () => ++timerId, clearInterval() {} };
  vm.runInNewContext(source, context);
  const h = window.hooks, device = h.createDevice('MAP-1234567890AB', 'Máy thử', 'token');
  h.state.devices = [device]; h.state.selectedId = device.id;
  return { ...h, device, document, elements, window, clients, timers, context,
    run(delay) { for (const [id, t] of [...timers]) if (t.delay === delay) { timers.delete(id); now += delay; t.fn(); } } };
}

function connected(h) {
  const calls = [];
  h.state.mqttConnected = true;
  h.state.mqtt = { connected: true, subscribe: (filters, callback) => calls.push({ filters, callback }),
    publish: (topic, wire, options, callback) => { h.published.push({ topic, body: JSON.parse(wire), options }); callback?.(); } };
  h.published = [];
  return calls;
}

test('one batched SUBSCRIBE preserves all seven topics and QoS, deduplicating simultaneous requests', async () => {
  const h = browser(), calls = connected(h);
  const a = h.subscribeDevice(h.device.id), b = h.subscribeDevice(h.device.id);
  assert.equal(calls.length, 1);
  const entries = Object.entries(calls[0].filters);
  assert.equal(entries.length, 7);
  for (const [topic, options] of entries) assert.equal(options.qos, topic.endsWith('/snapshot') ? 0 : 1);
  calls[0].callback(null, entries.map(([topic, options]) => ({ topic, qos: options.qos })));
  await Promise.all([a, b]);
  await h.subscribeDevice(h.device.id);
  assert.equal(calls.length, 1);
});

test('rejected or partial SUBACK cannot authorize data synchronization', async () => {
  for (const granted of [[], [{ topic: 'wrong', qos: 0 }]]) {
    const h = browser(), calls = connected(h);
    const request = h.subscribeDevice(h.device.id);
    calls[0].callback(null, granted);
    await assert.rejects(request, /SUBSCRIBE_REJECTED/);
    assert.equal(h.state.subscriptions.size, 0);
  }
});

test('lost SUBACK times out; late or previous-connection SUBACK never populates subscriptions', async () => {
  for (const timeout of [true, false]) {
    const h = browser(), calls = connected(h);
    const request = h.subscribeDevice(h.device.id);
    if (timeout) h.run(8000); else h.state.subscriptionEpoch++;
    calls[0].callback(null, Object.entries(calls[0].filters).map(([topic, x]) => ({ topic, qos: x.qos })));
    await assert.rejects(request, timeout ? /SUBSCRIBE_TIMEOUT/ : /CONNECTION_CHANGED/);
    assert.equal(h.state.subscriptions.size, 0);
  }
});

test('lost first QoS0 sync is retried at 700ms; complete data stops extra syncs', () => {
  const h = browser(); connected(h);
  h.activateSelectedSession(true);
  assert.equal(h.published.length, 1);
  h.run(700);
  assert.equal(h.published.length, 2);
  h.device.config = Object.fromEntries(h.REQUIRED_CONFIG_KEYS.map(key => [key, 0]));
  h.device.snapshotAt = Date.now(); h.device.snapshot = { revision: 0 };
  h.run(1600);
  assert.equal(h.published.length, 2);
});

test('sync retries stop when hidden, after device change, or on deactivation', () => {
  for (const change of [h => { h.document.hidden = true; }, h => { h.state.selectedId = 'other'; },
    h => h.deactivateSession(h.device.id)]) {
    const h = browser(); connected(h); h.activateSelectedSession(true); change(h);
    const count = h.published.length; h.run(700); h.run(1600);
    assert.equal(h.published.length, count);
  }
});

test('pairing reuses current credentials; replacing a client isolates stale callbacks', () => {
  const h = browser(); h.connectMqtt(); h.connectMqtt();
  assert.equal(h.clients.length, 1);
  const first = h.clients[0]; h.state.mqttCredentials = ['different']; h.connectMqtt();
  const second = h.clients[1]; second.connected = true; second.emit('connect');
  first.emit('close'); first.emit('offline'); first.emit('reconnect');
  assert.equal(h.state.mqttConnected, true);
  assert.equal(h.state.mqtt, second);
});

test('fan config requires complete capabilities and preserves legacy schedules', () => {
  const h = browser();
  h.device.config = Object.fromEntries(h.REQUIRED_CONFIG_KEYS.map(key => [key, 0]));
  h.device.config.ventScheduleEnabled = true; h.device.config.highTempAlarm = 39;
  for (const [id, value] of Object.entries({ ventOn: 38, ventOff: 37.8 })) h.elements.set(id, { value });
  assert.equal(h.supportsVentProfile(h.device.config), false);
  const legacy = h.buildConfig('vent');
  assert.equal(legacy.ventScheduleEnabled, true);
  assert.equal(Object.hasOwn(legacy, 'ventAutoEnabled'), false);
  for (const key of h.VENT_PROFILE_KEYS) { h.device.config[key] = 10; h.elements.set(key, { value: 10, checked: true }); }
  h.elements.get('ventProfileLevel').value = 1;
  h.elements.get('ventCycleMinutes').value = 40;
  assert.equal(h.validateVentForm(), true);
  assert.equal(h.buildConfig('vent').ventScheduleEnabled, false);
  h.elements.get('ventCycleMinutes').value = 45;
  assert.equal(h.validateVentForm(), false);
  assert.equal(h.window.invalidField, 'ventCycleMinutes');
});

test('swipe changes adjacent tabs only and rejects vertical, short, slow or edge swipes', () => {
  const h = browser();
  assert.equal(h.swipeDestination('device', -100, 5, 250), 'batch');
  assert.equal(h.swipeDestination('batch', -100, 5, 250), 'settings');
  assert.equal(h.swipeDestination('settings', 100, 5, 250), 'batch');
  for (const args of [['device', 100, 5, 250], ['settings', -100, 5, 250], ['device', -50, 1, 200],
    ['device', -100, 90, 200], ['device', -100, 1, 900]]) assert.equal(h.swipeDestination(...args), null);
});

test('pairing keeps the page and auth; production loads the pinned local MQTT client in order', () => {
  const app = fs.readFileSync(require.resolve('../app.js'), 'utf8');
  const html = fs.readFileSync(require.resolve('../index.html'), 'utf8');
  assert.doesNotMatch(app, /window\.location\.reload/);
  assert.match(app, /await verifyDevicePin\(id, pin\)/);
  assert.match(html, /defer src="\.\/vendor\/mqtt\.min\.js"/);
  assert.ok(html.indexOf('vendor/mqtt.min.js') < html.indexOf('./app.js'));
});

test('startup auth failure retries with backoff; expired pairing never retries credentials', async () => {
  for (const status of [503, 403]) {
    const h = browser(); let calls = 0;
    h.context.fetch = async () => { calls++; return { ok:false, status, json:async()=>({success:false}) }; };
    await h.refreshMqttSession();
    assert.equal(h.state.mqttSessionState, status===403 ? 'auth-required' : 'error');
    const at = h.state.authRetryAt;
    assert.ok(at > Date.now());
    await h.recoverBrowserConnection(); assert.equal(calls, 1);
    h.state.authRetryAt = 0; await h.recoverBrowserConnection();
    assert.equal(calls, status===403 ? 1 : 2);
    if (status===503) assert.equal(h.state.authRetryDelay, 20000);
    assert.equal(h.clients.length, 0);
  }
});

test('startup auth request is bounded and aborts before retry, leaving writes untouched', async () => {
  const h = browser();
  h.context.fetch = async (url, options) => new Promise((resolve,reject) => {
    options.signal.addEventListener('abort', () => reject(new Error('aborted')));
  });
  const request = h.refreshMqttSession();
  h.run(10000); await request;
  assert.equal(h.state.mqttSessionState, 'error');
  assert.equal(h.clients.length, 0);
});

test('short stale telemetry is degraded; explicit LWT and long silence still block commands', async () => {
  const h = browser({ staleAfterMs:8000, offlineAfterMs:30000 }); connected(h);
  h.device.presence = { online:true };
  h.device.snapshot = { revision:1 };
  for (const [age, expected] of [[1000,'online'], [9000,'degraded'], [31000,'offline']]) {
    h.device.snapshotAt = Date.now() - age;
    assert.equal(h.connectionStatus(h.device), expected);
    assert.equal(h.isDeviceOnline(h.device), expected !== 'offline');
  }
  h.device.snapshotAt = Date.now(); h.device.presence.online = false;
  assert.equal(h.connectionStatus(h.device), 'offline');
  h.device.presence.online = true; h.state.mqttConnected = false;
  assert.notEqual(h.connectionStatus(h.device), 'online');
  assert.equal(h.isDeviceOnline(h.device), false);
});
