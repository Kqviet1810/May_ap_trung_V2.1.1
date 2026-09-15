import { createHash } from 'node:crypto';

const deviceId = String(process.env.MAYAP_DEVICE_ID || '').trim();
const deviceSecret = String(process.env.MAYAP_DEVICE_SECRET || '');
const factoryPin = String(process.env.MAYAP_FACTORY_PIN || '');
const pepper = String(process.env.DEVICE_KEY_PEPPER || '');
const deviceName = String(process.env.MAYAP_DEVICE_NAME || deviceId)
  .replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, 64);

if (!/^MAP-[A-F0-9]{12}$/.test(deviceId)) throw new Error('MAYAP_DEVICE_ID khong hop le');
if (deviceSecret.length < 32 || deviceSecret.length > 128) throw new Error('MAYAP_DEVICE_SECRET can 32-128 ky tu');
if (!/^\d{6}$/.test(factoryPin)) throw new Error('MAYAP_FACTORY_PIN phai dung 6 chu so');
if (pepper.length < 32) throw new Error('DEVICE_KEY_PEPPER can it nhat 32 ky tu');

const digest = (value) => createHash('sha256').update(`${pepper}:${value}`, 'utf8').digest('hex');
const quote = (value) => `'${String(value).replaceAll("'", "''")}'`;
const now = Date.now();

process.stdout.write(
  `INSERT INTO devices ` +
  `(device_id, device_name, device_key_hash, pairing_token, web_pin_hash, created_at, last_seen, status, batch_running) VALUES (` +
  `${quote(deviceId)}, ${quote(deviceName)}, ${quote(digest(deviceSecret))}, NULL, ` +
  `${quote(digest(factoryPin))}, ${now}, NULL, 'unknown', 0);\n`
);
