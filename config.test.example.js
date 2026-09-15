// CHI DUNG TREN BAN THU NGHIEM CO GIAM SAT.
// Broker cong cong khong co ACL va khong duoc dung cho may giao khach.
window.MAYAP_WEB_CONFIG = Object.freeze({
  appVersion: '3.8.0',
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
  configTimeoutMs: 15000,
  cloudApiBase: ''
});
