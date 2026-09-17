// ============================================================================
// MAYAP WEB CONFIG - BAN THUONG MAI
// MQTT duoc Cloudflare Worker cap tu dong sau khi khach nhap dung ID + PIN.
// Khong dat tai khoan/mat khau MQTT trong file cong khai nay.
// ============================================================================
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
  // URL goc cua Cloudflare Worker (khong co dau / o cuoi), vi du:
  // 'https://mayap-push-worker.<ten-tai-khoan>.workers.dev' hoac
  // 'https://api.tenmiencuatoi.com' neu da gan custom domain.
  // De trong ('') se khien card "Thong bao" bao "Chua cau hinh" (xem push.js).
  cloudApiBase: 'https://mayap-push-worker.vietk-mayaptrung.workers.dev'
});
