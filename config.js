// ============================================================================
// MAYAP WEB CONFIG - MAC DINH AN TOAN (FAIL-CLOSED)
// File duoc commit khong chua broker/credential production. Khi trien khai,
// tao file nay tu config.production.example.js trong pipeline rieng.
// ============================================================================
window.MAYAP_WEB_CONFIG = Object.freeze({
  appVersion: '3.8.0',
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
  // URL goc cua Cloudflare Worker (khong co dau / o cuoi), vi du:
  // 'https://mayap-push-worker.<ten-tai-khoan>.workers.dev' hoac
  // 'https://api.tenmiencuatoi.com' neu da gan custom domain.
  // De trong ('') se khien card "Thong bao" bao "Chua cau hinh" (xem push.js).
  cloudApiBase: ''
});
