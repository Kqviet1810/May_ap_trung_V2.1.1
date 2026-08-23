import { hashDeviceKey, verifyDeviceKey, randomToken, isValidDeviceId } from './auth.js';
import {
  getDeviceByDeviceId,
  insertDevice,
  touchDevice,
  touchDeviceHeartbeat,
  setDeviceStatus,
  renameDevice,
  setDevicePinHash,
  getStaleOnlineDevices,
  getRecoveredOfflineDevices,
  getSubscriptionsForDevice,
  getSubscriptionByEndpoint,
  upsertSubscription,
  deleteSubscriptionByEndpoint,
  getAlarmState,
  upsertAlarmState,
  insertAlarmLog,
} from './db.js';
import { sendWebPush, buildNotificationPayload } from './push.js';

// Ran an toan phia server, DOC LAP voi anti-spam cua ESP32 (xem cloud_alert_link.h):
// du firmware co loi va goi lien tuc, worker cung khong ban push nhanh hon
// muc nay cho CUNG mot (device_id, alarm_type) khi trang thai khong doi.
const MIN_ALARM_COOLDOWN_MS = 15_000;

// ESP32 heartbeat moi 15s (CLOUD_HEARTBEAT_INTERVAL_MS trong config.h) - cho
// phep truot ~5 lan (mat goi/backoff luc mang chap chon) truoc khi coi la
// "mat ket noi that su" de tranh bao gia luc mang giat nhe. Day la do tre
// nhanh nhat hop ly dat duoc: Cron Trigger cua Cloudflare toi da 1 phut/lan
// (gioi han nen tang), nen ~75s la can bang tot nhat giua "nhanh" va
// "khong bao gia" voi kien truc heartbeat+cron polling nay.
const DEVICE_OFFLINE_THRESHOLD_MS = 75 * 1000;

function corsHeaders(env) {
  return {
    'Access-Control-Allow-Origin': env.ALLOWED_ORIGIN || '*',
    'Access-Control-Allow-Methods': 'GET, POST, DELETE, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Access-Control-Max-Age': '86400',
  };
}

function json(env, data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8', ...corsHeaders(env) },
  });
}

async function readJson(request) {
  try {
    return await request.json();
  } catch (_) {
    return null;
  }
}

// -------------------------- Endpoint: dang ky thiet bi --------------------------
// Trust-on-first-use: lan dau goi voi 1 device_id chua ton tai se TAO thiet bi
// va luu hash cua device_key gui len; cac lan sau PHAI gui dung device_key cu
// (khong cho ai "chiem" mot device_id da co bang cach dang ky de len lai).
async function handleRegister(request, env) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const deviceKey = String(body?.device_key || '');
  const deviceName = body?.device_name ? String(body.device_name).slice(0, 64) : '';

  if (!isValidDeviceId(deviceId) || deviceKey.length < 8) {
    return json(env, { success: false, error: 'device_id/device_key khong hop le' }, 400);
  }

  const now = Date.now();
  const existing = await getDeviceByDeviceId(env.DB, deviceId);
  if (!existing) {
    const deviceKeyHash = await hashDeviceKey(deviceKey, env.DEVICE_KEY_PEPPER);
    const pairingToken = randomToken(12);
    await insertDevice(env.DB, { deviceId, deviceName, deviceKeyHash, pairingToken, now });
    return json(env, { success: true, device_id: deviceId, pairing_token: pairingToken, created: true });
  }

  const valid = await verifyDeviceKey(deviceKey, env.DEVICE_KEY_PEPPER, existing.device_key_hash);
  if (!valid) {
    return json(env, { success: false, error: 'device_key khong khop voi thiet bi da dang ky' }, 401);
  }
  // KHONG truyen deviceName o day: ESP32 luon gui device_name = chinh
  // device_id cua no (khong co gia tri gi hon), truyen vao se GHI DE mat ten
  // than thien nguoi dung da tu doi qua web moi lan ESP32 dang ky lai (moi
  // lan khoi dong lai). Ten hien thi gio HOAN TOAN do web quan ly (xem
  // handleRenameDevice) - firmware khong con vai tro gi voi truong nay.
  await touchDevice(env.DB, deviceId, 'online', now);
  return json(env, { success: true, device_id: deviceId, pairing_token: existing.pairing_token, created: false });
}

// -------------------------- Endpoint: heartbeat --------------------------
async function handleHeartbeat(request, env) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const deviceKey = String(body?.device_key || '');

  const device = await getDeviceByDeviceId(env.DB, deviceId);
  if (!device) return json(env, { success: false, error: 'device chua dang ky' }, 404);
  const valid = await verifyDeviceKey(deviceKey, env.DEVICE_KEY_PEPPER, device.device_key_hash);
  if (!valid) return json(env, { success: false, error: 'device_key sai' }, 401);

  await touchDeviceHeartbeat(env.DB, deviceId, Date.now(), Boolean(body?.batch_running));
  return json(env, { success: true });
}

// -------------------------- Endpoint: bao dong / canh bao --------------------------
async function handleAlarm(request, env) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const deviceKey = String(body?.device_key || '');
  const alarmType = String(body?.alarm_type || '').trim();
  const message = String(body?.message || '').slice(0, 300);
  const severity = ['info', 'warning', 'critical', 'system'].includes(body?.severity) ? body.severity : 'warning';
  const state = body?.state === 'resolved' ? 'resolved' : 'active';
  const temperature = Number.isFinite(Number(body?.temperature)) ? Number(body.temperature) : null;
  const humidity = Number.isFinite(Number(body?.humidity)) ? Number(body.humidity) : null;

  if (!isValidDeviceId(deviceId) || !alarmType || !message) {
    return json(env, { success: false, error: 'thieu device_id/alarm_type/message' }, 400);
  }

  const device = await getDeviceByDeviceId(env.DB, deviceId);
  if (!device) return json(env, { success: false, error: 'device chua dang ky' }, 404);
  const valid = await verifyDeviceKey(deviceKey, env.DEVICE_KEY_PEPPER, device.device_key_hash);
  if (!valid) return json(env, { success: false, error: 'device_key sai' }, 401);

  const now = Date.now();
  await touchDevice(env.DB, deviceId, 'online', now);

  // Rao chong spam phia server: chi chan khi TRANG THAI khong doi va con qua
  // moi (chuyen active<->resolved luon duoc phep gui ngay, vi la tin quan trong).
  const priorState = await getAlarmState(env.DB, deviceId, alarmType);
  const stateActive = state === 'active';
  const unchanged = priorState && Boolean(priorState.active) === stateActive;
  const tooSoon = priorState?.last_sent_at && now - priorState.last_sent_at < MIN_ALARM_COOLDOWN_MS;
  const throttle = unchanged && tooSoon;

  let notificationSent = 0;
  if (!throttle) {
    const subscriptions = await getSubscriptionsForDevice(env.DB, deviceId);
    const notification = buildNotificationPayload({
      deviceId,
      deviceName: device.device_name,
      alarmType,
      severity,
      state,
      message,
      temperature,
      humidity,
    });

    // Gui song song toi tat ca subscription cua thiet bi (Promise.all) thay vi
    // tuan tu tung cai mot - 1 endpoint cham/treo (vd may push service dang
    // qua tai) truoc day se lam nghen ca hang doi, khien nhung nguoi con lai
    // duoc lien ket voi cung thiet bi nhan thong bao tre theo.
    const results = await Promise.all(subscriptions.map((sub) => sendWebPush(env, sub, notification)));
    const staleEndpoints = [];
    results.forEach((result, i) => {
      if (result.ok) {
        notificationSent += 1;
      } else if (result.gone) {
        // Subscription het han/bi thu hoi phia trinh duyet - don dep de lan
        // sau khong con thu gui vao mot endpoint da chet.
        staleEndpoints.push(subscriptions[i].endpoint);
      }
    });
    await Promise.all(staleEndpoints.map((endpoint) => deleteSubscriptionByEndpoint(env.DB, endpoint)));

    await upsertAlarmState(env.DB, {
      deviceId,
      alarmType,
      active: stateActive,
      firstSentAt: now,
      lastSentAt: now,
      lastMessage: message,
    });
  }

  await insertAlarmLog(env.DB, {
    deviceId,
    alarmType,
    severity,
    state,
    message,
    temperature,
    humidity,
    notificationSent: notificationSent > 0,
    now,
  });

  return json(env, { success: true, notification_sent: notificationSent, throttled: throttle });
}

// -------------------------- Endpoint: dang ky / huy Push subscription --------------------------
async function handleSubscribe(request, env) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const pairingToken = body?.pairing_token ? String(body.pairing_token) : '';
  const sub = body?.subscription;

  if (!isValidDeviceId(deviceId) || !sub?.endpoint || !sub?.keys?.p256dh || !sub?.keys?.auth) {
    return json(env, { success: false, error: 'thieu device_id hoac subscription khong hop le' }, 400);
  }

  const device = await getDeviceByDeviceId(env.DB, deviceId);
  if (!device) {
    return json(env, { success: false, error: 'device chua dang ky - hay bat may va cho ket noi mang truoc' }, 404);
  }
  // pairing_token la lop bao ve TUY CHON (dung khi co QR dan tren may): neu
  // thiet bi co pairing_token va nguoi goi CO gui token, phai khop. Neu
  // nguoi goi khong gui token (luong don gian, chi biet device_id), van cho
  // qua - danh doi da duoc noi ro trong tai lieu bao mat.
  if (pairingToken && device.pairing_token && pairingToken !== device.pairing_token) {
    return json(env, { success: false, error: 'pairing_token sai' }, 401);
  }

  await upsertSubscription(env.DB, {
    deviceId,
    endpoint: sub.endpoint,
    p256dh: sub.keys.p256dh,
    auth: sub.keys.auth,
    userAgent: request.headers.get('User-Agent') || '',
    now: Date.now(),
  });

  return json(env, { success: true });
}

async function handleUnsubscribe(request, env) {
  const body = await readJson(request);
  const endpoint = String(body?.endpoint || '');
  if (!endpoint) return json(env, { success: false, error: 'thieu endpoint' }, 400);
  await deleteSubscriptionByEndpoint(env.DB, endpoint);
  return json(env, { success: true });
}

// -------------------------- Endpoint: gui thu 1 thong bao test --------------------------
async function handleTestPush(request, env) {
  const body = await readJson(request);
  const endpoint = String(body?.endpoint || '');
  if (!endpoint) return json(env, { success: false, error: 'thieu endpoint' }, 400);

  const sub = await getSubscriptionByEndpoint(env.DB, endpoint);
  if (!sub) return json(env, { success: false, error: 'chua dang ky thong bao tren trinh duyet nay' }, 404);
  const device = await getDeviceByDeviceId(env.DB, sub.device_id);
  const deviceLabel = device?.device_name || sub.device_id;

  const notification = {
    title: `🔔 Test thành công - ${deviceLabel}`,
    body: 'Thiết bị này đã kết nối thông báo thành công.',
    icon: './icons/icon-192.png',
    badge: './icons/badge-72.png',
    data: {
      deviceId: sub.device_id, alarmType: 'TEST', severity: 'info', state: 'active',
      url: `./?device=${encodeURIComponent(sub.device_id || '')}`, ts: Date.now(),
    },
  };
  const result = await sendWebPush(env, sub, notification);
  if (!result.ok && result.gone) await deleteSubscriptionByEndpoint(env.DB, endpoint);
  // Tra ve them status/error khi that bai - de xem duoc ly do that qua tab
  // Network cua trinh duyet ma khong bat buoc phai chay wrangler tail.
  return json(env, {
    success: result.ok,
    notification_sent: result.ok ? 1 : 0,
    ...(result.ok ? {} : { push_status: result.status, push_error: result.error || '' }),
  });
}

// PIN rieng cua nguoi dung (KHAC device_key cua firmware) - gate cho "them
// thiet bi" va "doi ten may" tren web, tranh nguoi la biet device_id la them/
// sua duoc thiet bi cua nguoi khac. NULL = chua tung doi, coi nhu dang la
// PIN mac dinh xuat xuong "1111".
async function verifyDevicePin(env, device, pin) {
  const value = String(pin || '');
  if (!device.web_pin_hash) return value === '1111';
  return verifyDeviceKey(value, env.DEVICE_KEY_PEPPER, device.web_pin_hash);
}

function isValidPin(pin) {
  return typeof pin === 'string' && /^[0-9]{4,8}$/.test(pin);
}

// -------------------------- Endpoint: xac thuc PIN (dung khi them thiet bi) --------------------------
async function handleVerifyPin(request, env) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const pin = String(body?.pin || '');
  if (!isValidDeviceId(deviceId)) return json(env, { success: false, error: 'device_id khong hop le' }, 400);

  const device = await getDeviceByDeviceId(env.DB, deviceId);
  if (!device) return json(env, { success: false, error: 'device chua dang ky - hay bat may va cho ket noi mang truoc' }, 404);
  const valid = await verifyDevicePin(env, device, pin);
  if (!valid) return json(env, { success: false, error: 'Sai mã PIN của thiết bị' }, 401);
  return json(env, { success: true, device_name: device.device_name || device.device_id });
}

// -------------------------- Endpoint: doi ten may --------------------------
async function handleRenameDevice(request, env) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const pin = String(body?.pin || '');
  const name = String(body?.name || '').trim().slice(0, 64);
  if (!isValidDeviceId(deviceId) || !name) {
    return json(env, { success: false, error: 'thieu device_id/pin/name hop le' }, 400);
  }

  const device = await getDeviceByDeviceId(env.DB, deviceId);
  if (!device) return json(env, { success: false, error: 'device chua dang ky' }, 404);
  const valid = await verifyDevicePin(env, device, pin);
  if (!valid) return json(env, { success: false, error: 'Sai mã PIN của thiết bị' }, 401);

  await renameDevice(env.DB, deviceId, name);
  return json(env, { success: true, device_name: name });
}

// -------------------------- Endpoint: doi PIN --------------------------
async function handleChangePin(request, env) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const oldPin = String(body?.old_pin || '');
  const newPin = String(body?.new_pin || '');
  if (!isValidDeviceId(deviceId)) return json(env, { success: false, error: 'device_id khong hop le' }, 400);
  if (!isValidPin(newPin)) {
    return json(env, { success: false, error: 'PIN mới phải là số, từ 4 đến 8 chữ số' }, 400);
  }

  const device = await getDeviceByDeviceId(env.DB, deviceId);
  if (!device) return json(env, { success: false, error: 'device chua dang ky' }, 404);
  const valid = await verifyDevicePin(env, device, oldPin);
  if (!valid) return json(env, { success: false, error: 'Sai mã PIN hiện tại' }, 401);

  const newHash = await hashDeviceKey(newPin, env.DEVICE_KEY_PEPPER);
  await setDevicePinHash(env.DB, deviceId, newHash);
  return json(env, { success: true });
}

// -------------------------- Endpoint: trang thai lien ket cua 1 thiet bi --------------------------
async function handleDeviceStatus(env, deviceId) {
  const device = await getDeviceByDeviceId(env.DB, deviceId);
  if (!device) return json(env, { success: true, exists: false });
  const subs = await getSubscriptionsForDevice(env.DB, deviceId);
  return json(env, {
    success: true,
    exists: true,
    device_id: device.device_id,
    device_name: device.device_name,
    status: device.status,
    last_seen: device.last_seen,
    linked_browsers: subs.length,
  });
}

// -------------------------- Cron: phat hien thiet bi mat ket noi --------------------------
// Chay DOC LAP voi ESP32 (xem wrangler.toml [triggers]) - vi khi mat dien
// chinh may ap cung chet theo nen KHONG THE tu goi API bao "toi vua mat
// dien". Worker phai tu phat hien qua khoang lang cua last_seen.
async function sendDeviceLifecycleAlarm(env, device, { state, message }) {
  const now = Date.now();
  const subscriptions = await getSubscriptionsForDevice(env.DB, device.device_id);
  const notification = buildNotificationPayload({
    deviceId: device.device_id,
    deviceName: device.device_name,
    alarmType: 'DEVICE_OFFLINE',
    severity: 'critical',
    state,
    message,
  });

  let notificationSent = 0;
  const results = await Promise.all(subscriptions.map((sub) => sendWebPush(env, sub, notification)));
  const staleEndpoints = [];
  results.forEach((result, i) => {
    if (result.ok) {
      notificationSent += 1;
    } else if (result.gone) {
      staleEndpoints.push(subscriptions[i].endpoint);
    }
  });
  await Promise.all(staleEndpoints.map((endpoint) => deleteSubscriptionByEndpoint(env.DB, endpoint)));

  await upsertAlarmState(env.DB, {
    deviceId: device.device_id,
    alarmType: 'DEVICE_OFFLINE',
    active: state === 'active',
    firstSentAt: now,
    lastSentAt: now,
    lastMessage: message,
  });
  await insertAlarmLog(env.DB, {
    deviceId: device.device_id,
    alarmType: 'DEVICE_OFFLINE',
    severity: 'critical',
    state,
    message,
    temperature: null,
    humidity: null,
    notificationSent: notificationSent > 0,
    now,
  });
}

async function checkDeviceConnectivity(env) {
  const staleBefore = Date.now() - DEVICE_OFFLINE_THRESHOLD_MS;

  const staleDevices = await getStaleOnlineDevices(env.DB, staleBefore);
  for (const device of staleDevices) {
    await setDeviceStatus(env.DB, device.device_id, 'offline');
    // Chi gui push khi device dang co me ap chay tai lan heartbeat GAN NHAT
    // (batch_running ghi kem moi heartbeat - xem touchDeviceHeartbeat trong
    // db.js) - khong co me nao dang chay thi mat mang/mat dien khong can bao,
    // theo yeu cau: chi quan tam khi dang ap that su.
    if (!device.batch_running) continue;
    await sendDeviceLifecycleAlarm(env, device, {
      state: 'active',
      message: 'Mất kết nối trên 75 giây - kiểm tra nguồn điện hoặc Wi-Fi ngay.',
    });
  }

  const recoveredDevices = await getRecoveredOfflineDevices(env.DB, staleBefore);
  for (const device of recoveredDevices) {
    // Noi ro may da song lai, khong bao chung chung. Chi tiet "me ap co tu
    // chay tiep hay dang cho xac nhan" do chinh ESP32 gui rieng ngay khi no
    // khoi dong lai (POWER_RESTORED trong cloud_alert_link.h) - o day Worker
    // chi biet den muc "da thay heartbeat tro lai".
    await sendDeviceLifecycleAlarm(env, device, {
      state: 'resolved',
      message: 'Đã hết: máy đã có điện/mạng trở lại.',
    });
  }
}

export default {
  async scheduled(event, env, ctx) {
    ctx.waitUntil(checkDeviceConnectivity(env));
  },
  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === 'OPTIONS') {
      return new Response(null, { status: 204, headers: corsHeaders(env) });
    }

    try {
      if (url.pathname === '/api/push/vapid-public-key' && request.method === 'GET') {
        return json(env, { publicKey: env.VAPID_PUBLIC_KEY });
      }
      if (url.pathname === '/api/device/register' && request.method === 'POST') {
        return await handleRegister(request, env);
      }
      if (url.pathname === '/api/device/heartbeat' && request.method === 'POST') {
        return await handleHeartbeat(request, env);
      }
      if (url.pathname === '/api/device/alarm' && request.method === 'POST') {
        return await handleAlarm(request, env);
      }
      if (url.pathname === '/api/device/verify-pin' && request.method === 'POST') {
        return await handleVerifyPin(request, env);
      }
      if (url.pathname === '/api/device/rename' && request.method === 'POST') {
        return await handleRenameDevice(request, env);
      }
      if (url.pathname === '/api/device/change-pin' && request.method === 'POST') {
        return await handleChangePin(request, env);
      }
      if (url.pathname === '/api/push/subscribe' && request.method === 'POST') {
        return await handleSubscribe(request, env);
      }
      if (url.pathname === '/api/push/subscribe' && request.method === 'DELETE') {
        return await handleUnsubscribe(request, env);
      }
      if (url.pathname === '/api/push/test' && request.method === 'POST') {
        return await handleTestPush(request, env);
      }
      const statusMatch = url.pathname.match(/^\/api\/device\/([A-Za-z0-9_-]{3,40})\/status$/);
      if (statusMatch && request.method === 'GET') {
        return await handleDeviceStatus(env, statusMatch[1]);
      }

      return json(env, { success: false, error: 'not found' }, 404);
    } catch (error) {
      return json(env, { success: false, error: 'internal error', detail: String(error && error.message ? error.message : error) }, 500);
    }
  },
};
