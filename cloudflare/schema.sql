-- MAYAP Cloud Push - D1 schema
-- Ap dung: wrangler d1 execute mayap_push --remote --file=./schema.sql
-- (bo --remote de chay tren D1 local luc dev)

CREATE TABLE IF NOT EXISTS devices (
  id                  INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id           TEXT NOT NULL UNIQUE,
  device_name         TEXT NOT NULL DEFAULT '',
  device_key_hash     TEXT NOT NULL,
  pairing_token       TEXT,
  created_at          INTEGER NOT NULL,
  last_seen           INTEGER,
  status              TEXT NOT NULL DEFAULT 'unknown',
  -- Trang thai me ap tai lan heartbeat GAN NHAT (0/1) - dung de canh bao
  -- "mat ket noi thiet bi" CHI bao khi dang co me ap chay (xem
  -- checkDeviceConnectivity trong src/index.js). Neu D1 da co tu truoc, chay
  -- them: ALTER TABLE devices ADD COLUMN batch_running INTEGER NOT NULL DEFAULT 0;
  batch_running       INTEGER NOT NULL DEFAULT 0
);

-- 1 trinh duyet (1 endpoint) co the lien ket voi NHIEU device_id cung luc
-- (nguoi dung theo doi nhieu may tren cung 1 dien thoai) - vi vay UNIQUE la
-- CAP (device_id, endpoint), KHONG PHAI rieng endpoint (rieng endpoint tung
-- lam lan bat thong bao cho may B tu dong "cuop" endpoint khoi may A).
-- Neu D1 da co bang cu (endpoint UNIQUE rieng), chay migrate:
--   CREATE TABLE push_subscriptions_new (id INTEGER PRIMARY KEY AUTOINCREMENT,
--     device_id TEXT NOT NULL, endpoint TEXT NOT NULL, p256dh TEXT NOT NULL,
--     auth TEXT NOT NULL, user_agent TEXT, created_at INTEGER NOT NULL,
--     updated_at INTEGER NOT NULL, UNIQUE(device_id, endpoint));
--   INSERT INTO push_subscriptions_new SELECT * FROM push_subscriptions;
--   DROP TABLE push_subscriptions;
--   ALTER TABLE push_subscriptions_new RENAME TO push_subscriptions;
--   CREATE INDEX IF NOT EXISTS idx_push_subscriptions_device ON push_subscriptions(device_id);
CREATE TABLE IF NOT EXISTS push_subscriptions (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id     TEXT NOT NULL,
  endpoint      TEXT NOT NULL,
  p256dh        TEXT NOT NULL,
  auth          TEXT NOT NULL,
  user_agent    TEXT,
  created_at    INTEGER NOT NULL,
  updated_at    INTEGER NOT NULL,
  UNIQUE(device_id, endpoint)
);
CREATE INDEX IF NOT EXISTS idx_push_subscriptions_device ON push_subscriptions(device_id);

-- Trang thai ON/OFF cua tung loai canh bao tren tung thiet bi - dung de:
--  1) chong spam phia server (rao an toan doc lap voi ESP32, xem worker src/index.js)
--  2) biet khi nao can gui thong bao "da binh thuong" (state chuyen active -> false)
CREATE TABLE IF NOT EXISTS alarm_state (
  device_id       TEXT NOT NULL,
  alarm_type      TEXT NOT NULL,
  active          INTEGER NOT NULL DEFAULT 0,
  first_sent_at   INTEGER,
  last_sent_at    INTEGER,
  last_message    TEXT,
  PRIMARY KEY (device_id, alarm_type)
);

-- Lich su canh bao (tuy chon, phuc vu debug/"Kiem tra thong bao" - co the bo neu muon toi gian hon).
CREATE TABLE IF NOT EXISTS alarm_log (
  id                  INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id           TEXT NOT NULL,
  alarm_type          TEXT NOT NULL,
  severity            TEXT NOT NULL,
  state               TEXT NOT NULL,
  message             TEXT NOT NULL,
  temperature         REAL,
  humidity            REAL,
  notification_sent   INTEGER NOT NULL DEFAULT 0,
  created_at          INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_alarm_log_device ON alarm_log(device_id, created_at DESC);
