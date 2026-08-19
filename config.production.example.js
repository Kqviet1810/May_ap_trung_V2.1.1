// Sao che file nay thanh config.js khi chuyen sang broker rieng.
// LUU Y: website GitHub Pages la ma tinh. Moi mat khau dat trong file nay
// deu co the bi xem. Ban thuong mai nen dung token ngan han do backend cap.
window.MAYAP_WEB_CONFIG = Object.freeze({
  mqttUrl: 'wss://mqtt.tenmiencuaban.vn:8084/mqtt',
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
