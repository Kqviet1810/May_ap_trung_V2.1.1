// ============================================================================
// MAYAP WEB CONFIG - BAN KIEM TRA GITHUB PAGES
// Firmware ESP32 phai bat MAYAP_USE_PUBLIC_TEST_BROKER = 1 de test cung broker.
// KHONG dung broker cong cong cho may thuong mai.
// ============================================================================
window.MAYAP_WEB_CONFIG = Object.freeze({
  mqttUrl: 'wss://broker.emqx.io:8084/mqtt',
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
  configTimeoutMs: 15000
});
