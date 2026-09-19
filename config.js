// MAYAP Web config + browser-session hardening.
// MQTT credential chi giu trong RAM cua tab/browser process, KHONG ghi xuong localStorage.
(() => {
  'use strict';

  const CLOUD_API_BASE = 'https://mayap-push-worker.vietk-mayaptrung.workers.dev';
  const MQTT_PRIVATE_KEY = 'mayap.web.v10.mqtt.private';
  const DEVICES_KEY = 'mayap.web.v10.devices';
  const BROWSER_ID_KEY = 'mayap.web.v10.browser.id';

  window.MAYAP_WEB_CONFIG = Object.freeze({
    mqttUrl: '',
    mqttUsername: '',
    mqttPassword: '',
    topicRoot: 'mayap/v1',
    reconnectPeriodMs: 3000,
    connectTimeoutMs: 8000,
    keepaliveSeconds: 60,
    sessionTtlMs: 15000,
    sessionRefreshMs: 9000,
    staleAfterMs: 90000,
    commandTimeoutMs: 10000,
    configTimeoutMs: 15000,
    cloudApiBase: CLOUD_API_BASE,
  });

  const nativeGetItem = Storage.prototype.getItem;
  const nativeSetItem = Storage.prototype.setItem;
  const nativeRemoveItem = Storage.prototype.removeItem;
  let mqttPrivateInMemory = null;
  try {
    mqttPrivateInMemory = nativeGetItem.call(localStorage, MQTT_PRIVATE_KEY);
    nativeRemoveItem.call(localStorage, MQTT_PRIVATE_KEY);
  } catch (_) {}

  Storage.prototype.getItem = function getItem(key) {
    if (this === localStorage && String(key) === MQTT_PRIVATE_KEY) return mqttPrivateInMemory;
    return nativeGetItem.call(this, key);
  };
  Storage.prototype.setItem = function setItem(key, value) {
    if (this === localStorage && String(key) === MQTT_PRIVATE_KEY) {
      mqttPrivateInMemory = String(value);
      return;
    }
    return nativeSetItem.call(this, key, value);
  };
  Storage.prototype.removeItem = function removeItem(key) {
    if (this === localStorage && String(key) === MQTT_PRIVATE_KEY) {
      mqttPrivateInMemory = null;
      try { nativeRemoveItem.call(localStorage, MQTT_PRIVATE_KEY); } catch (_) {}
      return;
    }
    return nativeRemoveItem.call(this, key);
  };

  function randomBrowserId() {
    if (crypto.randomUUID) return crypto.randomUUID();
    const bytes = new Uint8Array(16);
    crypto.getRandomValues(bytes);
    return Array.from(bytes, (v) => v.toString(16).padStart(2, '0')).join('');
  }

  function browserId() {
    try {
      let id = nativeGetItem.call(localStorage, BROWSER_ID_KEY) || '';
      if (!/^[A-Za-z0-9_-]{16,80}$/.test(id)) {
        id = randomBrowserId();
        nativeSetItem.call(localStorage, BROWSER_ID_KEY, id);
      }
      return id;
    } catch (_) {
      if (!window.__mayapEphemeralBrowserId) window.__mayapEphemeralBrowserId = randomBrowserId();
      return window.__mayapEphemeralBrowserId;
    }
  }

  function browserName() {
    const ua = navigator.userAgent || '';
    let name = 'Browser';
    if (/Edg\//.test(ua)) name = 'Edge';
    else if (/Firefox\//.test(ua)) name = 'Firefox';
    else if (/CriOS\//.test(ua)) name = 'Chrome iOS';
    else if (/Chrome\//.test(ua)) name = 'Chrome';
    else if (/Safari\//.test(ua)) name = 'Safari';
    const platform = navigator.userAgentData?.platform || navigator.platform || '';
    return `${name}${platform ? ` · ${platform}` : ''}`.slice(0, 80);
  }

  function storedDevices() {
    try {
      const rows = JSON.parse(nativeGetItem.call(localStorage, DEVICES_KEY) || '[]');
      return Array.isArray(rows) ? rows : [];
    } catch (_) {
      return [];
    }
  }

  function sessionForDevice(deviceId) {
    const id = String(deviceId || '').toUpperCase();
    const row = storedDevices().find((item) => String(item?.id || '').toUpperCase() === id);
    return row && row.pairingToken ? String(row.pairingToken) : '';
  }

  function browserSessions() {
    const cid = browserId();
    return storedDevices()
      .filter((item) => item?.id && item?.pairingToken)
      .map((item) => ({
        device_id: String(item.id).toUpperCase(),
        client_id: cid,
        pairing_token: String(item.pairingToken),
      }));
  }

  function storeDevices(rows) {
    try { nativeSetItem.call(localStorage, DEVICES_KEY, JSON.stringify(rows)); } catch (_) {}
  }

  function clearDeviceSession(deviceId) {
    const id = String(deviceId || '').toUpperCase();
    const rows = storedDevices();
    let changed = false;
    rows.forEach((item) => {
      if (String(item?.id || '').toUpperCase() === id && item.pairingToken) {
        item.pairingToken = '';
        changed = true;
      }
    });
    if (changed) storeDevices(rows);
    mqttPrivateInMemory = null;
  }

  const nativeFetch = window.fetch.bind(window);

  window.fetch = async function mayapSecureFetch(input, init = undefined) {
    const rawUrl = input instanceof Request ? input.url : String(input);
    let url;
    try { url = new URL(rawUrl, location.href); } catch (_) { return nativeFetch(input, init); }
    if (!url.href.startsWith(CLOUD_API_BASE)) return nativeFetch(input, init);

    const sourceInit = init || {};
    const method = String(sourceInit.method || (input instanceof Request ? input.method : 'GET')).toUpperCase();
    const nextInit = { ...sourceInit, method };
    const headers = new Headers(sourceInit.headers || (input instanceof Request ? input.headers : undefined));
    nextInit.headers = headers;

    const statusMatch = url.pathname.match(/^\/api\/device\/(MAP-[A-F0-9]{12})\/status$/);
    if (method === 'GET' && statusMatch) {
      const token = sessionForDevice(statusMatch[1]);
      if (token) {
        url.searchParams.set('client_id', browserId());
        url.searchParams.set('pairing_token', token);
      }
      return nativeFetch(url.toString(), nextInit);
    }

    if ((method === 'POST' || method === 'DELETE') && typeof sourceInit.body === 'string') {
      let body;
      try { body = JSON.parse(sourceInit.body); } catch (_) { body = null; }
      if (body && typeof body === 'object' && !Array.isArray(body)) {
        const cid = browserId();
        if (url.pathname === '/api/device/verify-pin') {
          body.client_id = cid;
          body.client_name = browserName();
        } else if (
          url.pathname === '/api/device/mqtt-session' ||
          url.pathname === '/api/device/sign-mqtt' ||
          (url.pathname === '/api/push/subscribe' && method === 'POST')
        ) {
          body.client_id = cid;
          if (!body.pairing_token && body.device_id) body.pairing_token = sessionForDevice(body.device_id);
        } else if (url.pathname === '/api/device/change-pin') {
          body.client_id = cid;
          body.client_name = browserName();
        } else if (
          url.pathname === '/api/push/test' ||
          (url.pathname === '/api/push/subscribe' && method === 'DELETE')
        ) {
          body.browser_sessions = browserSessions();
        }
        headers.set('Content-Type', 'application/json');
        nextInit.body = JSON.stringify(body);
      }
    }

    return nativeFetch(url.toString(), nextInit);
  };

  async function checkSessions() {
    const sessions = browserSessions();
    for (const session of sessions) {
      try {
        const res = await nativeFetch(`${CLOUD_API_BASE}/api/device/session-check`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(session),
        });
        if (res.status === 401) {
          clearDeviceSession(session.device_id);
          location.reload();
          return;
        }
      } catch (_) {}
    }
  }

  setTimeout(checkSessions, 15_000);
  setInterval(checkSessions, 60_000);

  document.addEventListener('DOMContentLoaded', () => {
    const pinInput = document.getElementById('newDevicePin');
    if (pinInput) {
      pinInput.minLength = 6;
      pinInput.maxLength = 8;
      pinInput.pattern = '[0-9]{6,8}';
      pinInput.placeholder = 'PIN 6–8 số';
    }
  });
})();
