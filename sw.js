'use strict';
const CACHE = 'mayap-web-v11.0.0';
const APP_SHELL = [
  './', './index.html', './styles.css', './app.js', './push.js', './manifest.webmanifest',
  './icons/icon-192.png', './icons/icon-512.png', './icons/badge-72.png'
];
self.addEventListener('install', (event) => {
  // Dung tung cache.add() + catch rieng thay vi cache.addAll() (all-or-nothing):
  // 1 file loi (404/mang cham luc cai dat) tung lam TOAN BO Service Worker
  // khong cai dat duoc, chan luon ca tinh nang Push (phu thuoc SW). Thieu 1
  // file trong app shell chi lam file do khong duoc cache truoc, khong chan
  // ca trang.
  event.waitUntil(
    caches.open(CACHE).then((cache) => Promise.all(
      APP_SHELL.map((url) => cache.add(url).catch(() => {}))
    ))
  );
  self.skipWaiting();
});
self.addEventListener('activate', (event) => {
  event.waitUntil(caches.keys().then((keys) => Promise.all(
    keys.filter((key) => key !== CACHE).map((key) => caches.delete(key))
  )));
  self.clients.claim();
});
self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return;
  const url = new URL(event.request.url);
  if (url.pathname.endsWith('/config.js')) {
    event.respondWith(fetch(event.request).catch(() => caches.match(event.request)));
    return;
  }
  if (url.origin !== self.location.origin) return;
  // Network-first cho HTML/JS/CSS: luon co gang lay ban moi nhat tu mang
  // truoc, chi dung cache khi mat mang. Cache-first (cu) tung khien trang
  // "khong bao gio tu cap nhat" cho nguoi dung da tung mo qua 1 lan, vi no
  // tra ve cache ngay ma khong kiem tra mang, kho nhan ra ke ca sau khi
  // Service Worker moi da activate (dac biet dai tren iOS PWA).
  const isCoreAsset = /\.(?:html|js|css)$/.test(url.pathname) || url.pathname.endsWith('/');
  if (isCoreAsset) {
    event.respondWith(
      fetch(event.request).then((response) => {
        const copy = response.clone();
        caches.open(CACHE).then((cache) => cache.put(event.request, copy));
        return response;
      }).catch(() => caches.match(event.request))
    );
    return;
  }
  event.respondWith(caches.match(event.request).then((cached) => cached || fetch(event.request).then((response) => {
    const copy = response.clone();
    caches.open(CACHE).then((cache) => cache.put(event.request, copy));
    return response;
  })));
});

// ------------------------------- Web Push -----------------------------------
// Nhan push tu Cloudflare Worker (thay Telegram) va hien notification that su
// cua he dieu hanh - hoat dong ke ca khi khong co tab nao cua website dang mo.
self.addEventListener('push', (event) => {
  let data = {};
  try { data = event.data ? event.data.json() : {}; } catch (_) {
    data = { title: 'MAYAP', body: event.data ? event.data.text() : '' };
  }

  const title = data.title || 'MAYAP';
  const options = {
    body: data.body || '',
    icon: data.icon || './icons/icon-192.png',
    badge: data.badge || './icons/badge-72.png',
    data: data.data || {},
    tag: data.data?.alarmType ? `mayap-${data.data.deviceId || ''}-${data.data.alarmType}` : undefined,
    // Canh bao ACTIVE thay the ban cu cung loai (khong xep chong nhieu thong
    // bao "van con loi X" giong nhau); tin RESOLVED luon la thong bao rieng
    // (khong ghi de) de nguoi dung con thay ro da tung co canh bao.
    renotify: data.data?.state === 'active',
  };

  event.waitUntil(self.registration.showNotification(title, options));
});

self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  const targetUrl = new URL(event.notification.data?.url || './', self.location.href).href;
  event.waitUntil(
    self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then((clients) => {
      for (const client of clients) {
        if (client.url === targetUrl && 'focus' in client) return client.focus();
      }
      if (self.clients.openWindow) return self.clients.openWindow(targetUrl);
      return undefined;
    })
  );
});

self.addEventListener('pushsubscriptionchange', (event) => {
  // Trinh duyet tu xoay subscription (het han/thu hoi khoa) - trang web se tu
  // phat hien va dang ky lai o lan mo tiep theo qua MayapPush.getState() trong
  // push.js, khong can xu ly gi them o day.
});
