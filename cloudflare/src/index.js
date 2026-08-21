import { hashDeviceKey, verifyDeviceKey, randomToken, isValidDeviceId } from './auth.js';
import {
  getDeviceByDeviceId,
  insertDevice,
  touchDevice,
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
  await touchDevice(env.DB, deviceId, 'online', now, deviceName || undefined);
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

  await touchDevice(env.DB, deviceId, 'online', Date.now());
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

    for (const sub of subscriptions) {
      const result = await sendWebPush(env, sub, notification);
      if (result.ok) {
        notificationSent += 1;
      } else if (result.gone) {
        // Subscription het han/bi thu hoi phia trinh duyet - don dep de lan
        // sau khong con thu gui vao mot endpoint da chet.
        await deleteSubscriptionByEndpoint(env.DB, sub.endpoint);
      }
    }

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

  const notification = {
    title: '🔔 Test thành công',
    body: 'Thiết bị của bạn đã kết nối thông báo.',
    icon: '/icons/icon-192.png',
    badge: '/icons/badge-72.png',
    data: { deviceId: sub.device_id, alarmType: 'TEST', severity: 'info', state: 'active', url: '/', ts: Date.now() },
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

export default {
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
