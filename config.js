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
    // Reconnect nhanh hon nhung khong qua gay gat de tranh reconnect storm.
    reconnectPeriodMs: 2000,
    // WSS can DNS + TLS + WebSocket + MQTT CONNECT; 8s qua sat khi mang yeu.
    connectTimeoutMs: 15000,
    // Phat hien socket nua-song som hon, van du rong cho mang di dong/Wi-Fi yeu.
    keepaliveSeconds: 30,
    // TTL dai hon nhieu so voi refresh de tab bi giat ngan khong lam ESP32 roi
    // ve che do snapshot cham. Refresh 3s dong thoi tu phuc hoi rat nhanh neu
    // goi sync dau tien bi roi truoc khi SUBACK hoan tat.
    sessionTtlMs: 45000,
    sessionRefreshMs: 3000,
    // Presence la retained nen KHONG duoc giu nhan "online" qua lau neu
    // snapshot/config thuc te da dung. 8s > snapshot idle 6s va >> active 400ms.
    staleAfterMs: 8000,
    commandTimeoutMs: 10000,
    configTimeoutMs: 15000,
    cloudApiBase: CLOUD_API_BASE,
  });

  // ---------------------------------------------------------------------------
  // 1) Khong luu MQTT password tren dia.
  // App cu van goi localStorage.getItem/setItem cho key nay; ta giu API tuong
  // thich nhung chuyen RIENG key MQTT sang bien RAM. Neu browser dang co gia
  // tri cu tren dia, doc 1 lan vao RAM roi xoa ngay khoi localStorage.
  // ---------------------------------------------------------------------------
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

  // ---------------------------------------------------------------------------
  // 2) Gan client_id on dinh cho tung browser va tu dong gui kem session token
  // o nhung API can quyen. App.js/push.js cu khong can biet chi tiet schema moi.
  // ---------------------------------------------------------------------------
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

  // Browser bi day khoi slot (vi browser thu 4 dang nhap) se bi server tu choi
  // session-check. Trong toi da ~60 giay trang cu tu xoa token va reload; do
  // MQTT password chi nam trong RAM, reload cung cat luon ket noi MQTT hien tai.
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
      } catch (_) {
        // Mat mang tam thoi khong duoc phep tu dang xuat browser hop le.
      }
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
