// MAYAP Web Push - thay the hoan toan kenh Telegram cu.
// ESP32 -> Cloudflare Worker -> Web Push -> trinh duyet nay (kha ca khi
// website dang KHONG mo, nho Service Worker dang ky o sw.js).
//
// Module nay chi lo phan "Push Subscription" (dang ky/huy/test) - KHONG dung
// polling gia lap, dung dung chuan Service Worker + Push API + Notification API.
(() => {
  'use strict';

  const STORAGE_KEY = 'mayap.push.v1';

  function cloudApiBase() {
    const base = String(window.MAYAP_WEB_CONFIG?.cloudApiBase || '').replace(/\/+$/, '');
    return base;
  }

  function apiUrl(path) {
    const base = cloudApiBase();
    if (!base) return null;
    return `${base}${path}`;
  }

  function isSupported() {
    return 'serviceWorker' in navigator && 'PushManager' in window && 'Notification' in window;
  }

  // iPadOS 13+ gia lam Mac Safari (UA khong con chua "iPad") nen phai kiem
  // tra them "co cam ung + maxTouchPoints" de phan biet voi Mac that.
  function isIos() {
    const ua = navigator.userAgent || '';
    if (/iphone|ipad|ipod/i.test(ua)) return true;
    return /Macintosh/.test(ua) && navigator.maxTouchPoints > 1;
  }

  function isStandalone() {
    if (window.navigator.standalone === true) return true; // Safari iOS
    return window.matchMedia && window.matchMedia('(display-mode: standalone)').matches;
  }

  // Web Push yeu cau applicationServerKey dang Uint8Array, nhung VAPID public
  // key tra ve tu server la chuoi base64url - can tu chuyen doi (khong co ham
  // dung san trong trinh duyet).
  function urlBase64ToUint8Array(base64String) {
    const padding = '='.repeat((4 - (base64String.length % 4)) % 4);
    const base64 = (base64String + padding).replace(/-/g, '+').replace(/_/g, '/');
    const raw = atob(base64);
    const output = new Uint8Array(raw.length);
    for (let i = 0; i < raw.length; ++i) output[i] = raw.charCodeAt(i);
    return output;
  }

  function loadLinked() {
    try { return JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}'); } catch (_) { return {}; }
  }
  function saveLinked(data) {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(data)); } catch (_) {}
  }

  async function registerServiceWorker() {
    if (location.protocol === 'file:') throw new Error('Can chay qua HTTPS/http server, khong the la file:// ');
    const registration = await navigator.serviceWorker.register('./sw.js');
    await navigator.serviceWorker.ready;
    return registration;
  }

  async function fetchVapidPublicKey() {
    const url = apiUrl('/api/push/vapid-public-key');
    if (!url) throw new Error('Chua cau hinh cloudApiBase trong config.js');
    const res = await fetch(url);
    if (!res.ok) throw new Error(`Khong lay duoc VAPID public key (HTTP ${res.status})`);
    const data = await res.json();
    if (!data?.publicKey) throw new Error('Server chua cau hinh VAPID_PUBLIC_KEY');
    return data.publicKey;
  }

  // Goi /api/push/subscribe la thao tac UPSERT re/an toan goi lai nhieu lan -
  // dung ca khi bat thong bao lan dau LAN khi tu "vien lai" link cho mot
  // subscription da co san (vi du sau khi trinh duyet tu xoay subscription o
  // su kien pushsubscriptionchange, xem sw.js) ma khong can nguoi dung bam lai.
  async function linkSubscription(deviceId, subscription, pairingToken) {
    const url = apiUrl('/api/push/subscribe');
    if (!url) throw new Error('Chua cau hinh cloudApiBase trong config.js');
    const res = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        device_id: deviceId,
        pairing_token: pairingToken || '',
        subscription: subscription.toJSON ? subscription.toJSON() : subscription,
      }),
    });
    const body = await res.json().catch(() => ({}));
    if (!res.ok || !body.success) throw new Error(body.error || `Worker tu choi (HTTP ${res.status})`);
    const linked = loadLinked();
    linked[deviceId] = { endpoint: subscription.endpoint, linkedAt: Date.now() };
    saveLinked(linked);
  }

  // reason co the la: 'unsupported' | 'ios-needs-install' | 'denied' | 'error'
  //
  // QUAN TRONG cho Safari/iOS: requestPermission() phai la thao tac async
  // DAU TIEN sau cu cham cua nguoi dung, khong duoc co await nao (vi du
  // dang ky Service Worker) chen truoc no. WebKit gan quyen "user gesture"
  // rat chat - de mot await khac chay truoc se lam mat "user activation",
  // khien requestPermission() lang le khong hien hop thoai hoac tu choi,
  // dù nut van goi duoc binh thuong tren Chrome/Android (khong bi rang buoc
  // nay). Vi vay xin quyen TRUOC, dang ky Service Worker SAU.
  async function enable(deviceId, options = {}) {
    if (!deviceId) return { ok: false, reason: 'no-device' };
    if (!isSupported()) return { ok: false, reason: 'unsupported' };
    if (!cloudApiBase()) return { ok: false, reason: 'not-configured' };
    if (isIos() && !isStandalone()) return { ok: false, reason: 'ios-needs-install' };

    if (Notification.permission === 'denied') return { ok: false, reason: 'denied' };
    const permission = await Notification.requestPermission();
    if (permission !== 'granted') return { ok: false, reason: 'denied' };

    let registration;
    try {
      registration = await registerServiceWorker();
    } catch (error) {
      return { ok: false, reason: 'error', error: String(error?.message || error) };
    }

    try {
      const publicKey = await fetchVapidPublicKey();
      let subscription = await registration.pushManager.getSubscription();
      if (!subscription) {
        subscription = await registration.pushManager.subscribe({
          userVisibleOnly: true,
          applicationServerKey: urlBase64ToUint8Array(publicKey),
        });
      }

      await linkSubscription(deviceId, subscription, options.pairingToken);
      return { ok: true };
    } catch (error) {
      return { ok: false, reason: 'error', error: String(error?.message || error) };
    }
  }

  async function disable(deviceId) {
    if (!isSupported()) return { ok: true };
    try {
      const registration = await navigator.serviceWorker.getRegistration('./sw.js');
      const subscription = await registration?.pushManager.getSubscription();
      if (subscription) {
        const url = apiUrl('/api/push/subscribe');
        if (url) {
          await fetch(url, {
            method: 'DELETE',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ endpoint: subscription.endpoint }),
          }).catch(() => {});
        }
        await subscription.unsubscribe();
      }
    } catch (_) {
      // Khong chan UI neu huy that bai - trang thai se tu dong bo lai o lan kiem tra tiep theo.
    }
    if (deviceId) {
      const linked = loadLinked();
      delete linked[deviceId];
      saveLinked(linked);
    }
    return { ok: true };
  }

  async function currentSubscriptionEndpoint() {
    if (!isSupported()) return '';
    const registration = await navigator.serviceWorker.getRegistration('./sw.js');
    const subscription = await registration?.pushManager.getSubscription();
    return subscription?.endpoint || '';
  }

  async function testNotification() {
    const endpoint = await currentSubscriptionEndpoint();
    if (!endpoint) return { ok: false, reason: 'not-subscribed' };
    const url = apiUrl('/api/push/test');
    if (!url) return { ok: false, reason: 'no-config' };
    try {
      const res = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ endpoint }),
      });
      const body = await res.json().catch(() => ({}));
      return { ok: Boolean(body.success), notificationSent: body.notification_sent || 0 };
    } catch (error) {
      return { ok: false, reason: 'error', error: String(error?.message || error) };
    }
  }

  // Trang thai tong hop de UI hien thi 1 trong 4 muc: chua ho tro / iOS chua
  // cai PWA / chua cap quyen / chua bat / da bat.
  //
  // Neu trinh duyet dang giu 1 subscription hop le (da cap quyen) nhung no
  // KHONG khop endpoint da luu cho deviceId nay (vi du sau khi trinh duyet tu
  // xoay subscription - su kien pushsubscriptionchange trong sw.js khong tu
  // goi lai server duoc vi Service Worker khong co localStorage), ham nay TU
  // dang ky lai voi Worker (upsert, an toan goi nhieu lan) thay vi bao sai
  // trang thai cho nguoi dung.
  async function getState(deviceId) {
    if (!isSupported()) return { status: 'unsupported' };
    if (!cloudApiBase()) return { status: 'not-configured' };
    if (isIos() && !isStandalone()) return { status: 'ios-needs-install' };
    if (Notification.permission === 'denied') return { status: 'denied' };
    const registration = await navigator.serviceWorker.getRegistration('./sw.js');
    const subscription = await registration?.pushManager.getSubscription();
    if (!subscription || Notification.permission !== 'granted') return { status: 'not-enabled' };

    if (deviceId) {
      const linked = loadLinked();
      if (linked[deviceId]?.endpoint !== subscription.endpoint) {
        try { await linkSubscription(deviceId, subscription); } catch (_) {
          return { status: 'error', endpoint: subscription.endpoint };
        }
      }
    }
    return { status: 'enabled', endpoint: subscription.endpoint };
  }

  window.MayapPush = {
    isSupported,
    isIos,
    isStandalone,
    enable,
    disable,
    testNotification,
    getState,
  };
})();
