import worker from './index.js';
import { hashDeviceKey, verifyDeviceKey, randomToken } from './auth.js';
import { sendWebPush } from './push.js';

const DEVICE_ID_RE = /^MAP-[A-F0-9]{12}$/;
const DEVICE_KEY_RE = /^[A-Fa-f0-9]{64}$/;
const CLIENT_ID_RE = /^[A-Za-z0-9_-]{16,80}$/;
const SESSION_TOKEN_RE = /^[a-f0-9]{64}$/;

const PIN_GLOBAL_WINDOW_MS = 60 * 60 * 1000;
const PIN_GLOBAL_BLOCK_MS = 60 * 60 * 1000;
const PIN_GLOBAL_MAX_FAILURES = 20;

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
    return await request.clone().json();
  } catch (_) {
    return null;
  }
}

function requestWithJson(request, body) {
  const headers = new Headers(request.headers);
  headers.set('Content-Type', 'application/json');
  return new Request(request.url, {
    method: request.method,
    headers,
    body: JSON.stringify(body),
    redirect: request.redirect,
  });
}

async function delegate(request, env, ctx) {
  const response = await worker.fetch(request, env, ctx);
  if (response.status < 500) return response;
  return json(env, { success: false, error: 'internal error' }, 500);
}

function validClientId(value) {
  return CLIENT_ID_RE.test(String(value || ''));
}

function validSessionToken(value) {
  return SESSION_TOKEN_RE.test(String(value || ''));
}

function configuredBrowserLimit(env) {
  const parsed = Number.parseInt(String(env.MAX_PAIRED_BROWSERS || '3'), 10);
  return Number.isFinite(parsed) ? Math.min(10, Math.max(1, parsed)) : 3;
}

function configuredSessionDays(env) {
  const parsed = Number.parseInt(String(env.BROWSER_SESSION_DAYS || '90'), 10);
  return Number.isFinite(parsed) ? Math.min(365, Math.max(1, parsed)) : 90;
}

async function browserLimitForDevice(env, deviceId) {
  const row = await env.DB.prepare(
    'SELECT browser_limit FROM device_inventory WHERE device_id = ?1 AND enabled = 1'
  ).bind(deviceId).first();
  const override = Number.parseInt(String(row?.browser_limit ?? ''), 10);
  if (Number.isFinite(override) && override >= 1 && override <= 10) return override;
  return configuredBrowserLimit(env);
}

async function revokeClient(env, deviceId, clientId, now) {
  await env.DB.batch([
    env.DB.prepare(
      'UPDATE device_clients SET revoked_at = ?3 WHERE device_id = ?1 AND client_id = ?2 AND revoked_at IS NULL'
    ).bind(deviceId, clientId, now),
    env.DB.prepare(
      'DELETE FROM push_subscriptions WHERE device_id = ?1 AND client_id = ?2'
    ).bind(deviceId, clientId),
  ]);
}

async function revokeAllClients(env, deviceId, now) {
  await env.DB.batch([
    env.DB.prepare(
      'UPDATE device_clients SET revoked_at = ?2 WHERE device_id = ?1 AND revoked_at IS NULL'
    ).bind(deviceId, now),
    env.DB.prepare('DELETE FROM push_subscriptions WHERE device_id = ?1').bind(deviceId),
  ]);
}

async function createBrowserSession(request, env, deviceId, clientId, clientName = '') {
  if (!env.DEVICE_KEY_PEPPER || !validClientId(clientId)) return null;
  const now = Date.now();
  const expiresAt = now + configuredSessionDays(env) * 24 * 60 * 60 * 1000;
  const limit = await browserLimitForDevice(env, deviceId);

  await env.DB.prepare(
    'UPDATE device_clients SET revoked_at = ?2 WHERE device_id = ?1 AND revoked_at IS NULL AND expires_at <= ?2'
  ).bind(deviceId, now).run();

  const existing = await env.DB.prepare(
    `SELECT client_id FROM device_clients
     WHERE device_id = ?1 AND client_id = ?2 AND revoked_at IS NULL AND expires_at > ?3`
  ).bind(deviceId, clientId, now).first();

  let evicted = null;
  if (!existing) {
    const countRow = await env.DB.prepare(
      `SELECT COUNT(*) AS n FROM device_clients
       WHERE device_id = ?1 AND revoked_at IS NULL AND expires_at > ?2`
    ).bind(deviceId, now).first();
    if (Number(countRow?.n || 0) >= limit) {
      const oldest = await env.DB.prepare(
        `SELECT client_id, client_name FROM device_clients
         WHERE device_id = ?1 AND revoked_at IS NULL AND expires_at > ?2
         ORDER BY issued_at ASC, client_id ASC LIMIT 1`
      ).bind(deviceId, now).first();
      if (oldest?.client_id) {
        await revokeClient(env, deviceId, oldest.client_id, now);
        evicted = { client_id: oldest.client_id, client_name: oldest.client_name || '' };
      }
    }
  }

  const token = randomToken(32);
  const tokenHash = await hashDeviceKey(token, env.DEVICE_KEY_PEPPER);
  const userAgent = String(request.headers.get('User-Agent') || '').slice(0, 240);
  await env.DB.prepare(
    `INSERT INTO device_clients
      (device_id, client_id, token_hash, client_name, user_agent, issued_at, last_seen_at, expires_at, revoked_at)
     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6, ?7, NULL)
     ON CONFLICT(device_id, client_id) DO UPDATE SET
       token_hash = excluded.token_hash,
       client_name = excluded.client_name,
       user_agent = excluded.user_agent,
       issued_at = excluded.issued_at,
       last_seen_at = excluded.last_seen_at,
       expires_at = excluded.expires_at,
       revoked_at = NULL`
  ).bind(deviceId, clientId, tokenHash, String(clientName || '').slice(0, 80), userAgent, now, expiresAt).run();

  return { token, expiresAt, limit, evicted };
}

async function verifyBrowserSession(env, deviceId, clientId, token, touch = true) {
  if (!DEVICE_ID_RE.test(String(deviceId || '')) || !validClientId(clientId) || !validSessionToken(token)) {
    return null;
  }
  const now = Date.now();
  const row = await env.DB.prepare(
    `SELECT * FROM device_clients
     WHERE device_id = ?1 AND client_id = ?2 AND revoked_at IS NULL`
  ).bind(deviceId, clientId).first();
  if (!row || Number(row.expires_at || 0) <= now) return null;
  const valid = await verifyDeviceKey(token, env.DEVICE_KEY_PEPPER, row.token_hash);
  if (!valid) return null;
  if (touch) {
    await env.DB.prepare(
      'UPDATE device_clients SET last_seen_at = ?3 WHERE device_id = ?1 AND client_id = ?2'
    ).bind(deviceId, clientId, now).run();
  }
  return row;
}

async function legacyPairingToken(env, deviceId) {
  const row = await env.DB.prepare('SELECT pairing_token FROM devices WHERE device_id = ?1').bind(deviceId).first();
  return String(row?.pairing_token || '');
}

async function rewriteForLegacyWorker(request, env, body) {
  const token = await legacyPairingToken(env, body.device_id);
  if (!token) return null;
  return requestWithJson(request, { ...body, pairing_token: token });
}

function globalRateKey(deviceId) {
  return `pin-global:${deviceId}`;
}

async function globalPinAllowed(env, deviceId, now) {
  const key = globalRateKey(deviceId);
  const row = await env.DB.prepare(
    'SELECT attempts, window_started_at, blocked_until FROM auth_rate_limits WHERE rate_key = ?1'
  ).bind(key).first();
  if (!row) return true;
  if (Number(row.blocked_until || 0) > now) return false;
  if (now - Number(row.window_started_at || 0) >= PIN_GLOBAL_WINDOW_MS) {
    await env.DB.prepare('DELETE FROM auth_rate_limits WHERE rate_key = ?1').bind(key).run();
  }
  return true;
}

async function recordGlobalPinFailure(env, deviceId, now) {
  const key = globalRateKey(deviceId);
  const row = await env.DB.prepare(
    'SELECT attempts, window_started_at FROM auth_rate_limits WHERE rate_key = ?1'
  ).bind(key).first();
  const fresh = !row || now - Number(row.window_started_at || 0) >= PIN_GLOBAL_WINDOW_MS;
  const attempts = fresh ? 1 : Number(row.attempts || 0) + 1;
  const started = fresh ? now : Number(row.window_started_at || now);
  const blockedUntil = attempts >= PIN_GLOBAL_MAX_FAILURES ? now + PIN_GLOBAL_BLOCK_MS : 0;
  await env.DB.prepare(
    `INSERT INTO auth_rate_limits (rate_key, attempts, window_started_at, blocked_until, updated_at)
     VALUES (?1, ?2, ?3, ?4, ?5)
     ON CONFLICT(rate_key) DO UPDATE SET attempts = excluded.attempts,
       window_started_at = excluded.window_started_at,
       blocked_until = excluded.blocked_until,
       updated_at = excluded.updated_at`
  ).bind(key, attempts, started, blockedUntil, now).run();
}

async function clearGlobalPinFailures(env, deviceId) {
  await env.DB.prepare('DELETE FROM auth_rate_limits WHERE rate_key = ?1').bind(globalRateKey(deviceId)).run();
}

async function guardPinCall(request, env, ctx, body, afterSuccess = null) {
  const deviceId = String(body?.device_id || '').trim();
  if (!DEVICE_ID_RE.test(deviceId)) return json(env, { success: false, error: 'device_id khong hop le' }, 400);
  const now = Date.now();
  if (!await globalPinAllowed(env, deviceId, now)) {
    return json(env, { success: false, error: 'Thiết bị đang tạm khóa xác thực do có quá nhiều lần nhập sai PIN' }, 429);
  }
  const response = await delegate(request, env, ctx);
  if (response.status === 401) {
    await recordGlobalPinFailure(env, deviceId, now);
    return response;
  }
  if (response.ok) {
    await clearGlobalPinFailures(env, deviceId);
    if (afterSuccess) return afterSuccess(response, deviceId);
  }
  return response;
}

async function handleSecureRegister(request, env, ctx) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const deviceKey = String(body?.device_key || '');
  if (!DEVICE_ID_RE.test(deviceId) || !DEVICE_KEY_RE.test(deviceKey)) {
    return json(env, { success: false, error: 'device_id/device_key khong hop le' }, 400);
  }
  const existing = await env.DB.prepare('SELECT 1 AS ok FROM devices WHERE device_id = ?1').bind(deviceId).first();
  if (!existing) {
    const inventory = await env.DB.prepare(
      'SELECT enabled FROM device_inventory WHERE device_id = ?1'
    ).bind(deviceId).first();
    if (Number(inventory?.enabled || 0) !== 1) {
      return json(env, { success: false, error: 'Thiết bị chưa được cấp phép xuất xưởng' }, 403);
    }
  }
  return delegate(request, env, ctx);
}

async function handleVerifyPin(request, env, ctx) {
  const body = await readJson(request);
  const clientId = String(body?.client_id || '');
  if (!validClientId(clientId)) {
    return json(env, { success: false, error: 'Trình duyệt chưa có định danh bảo mật hợp lệ' }, 400);
  }
  return guardPinCall(request, env, ctx, body, async (response, deviceId) => {
    const data = await response.json().catch(() => null);
    if (!data?.success) return json(env, data || { success: false, error: 'xac thuc that bai' }, response.status);
    const session = await createBrowserSession(request, env, deviceId, clientId, body?.client_name || '');
    if (!session) return json(env, { success: false, error: 'Máy chủ chưa tạo được phiên trình duyệt' }, 503);
    return json(env, {
      ...data,
      pairing_token: session.token,
      session_expires_at: session.expiresAt,
      browser_limit: session.limit,
      evicted_browser: session.evicted,
    });
  });
}

async function handleChangePin(request, env, ctx) {
  const body = await readJson(request);
  const newPin = String(body?.new_pin || '');
  if (!/^[0-9]{6,8}$/.test(newPin)) {
    return json(env, { success: false, error: 'PIN mới phải gồm 6 đến 8 chữ số' }, 400);
  }
  const clientId = String(body?.client_id || '');
  if (!validClientId(clientId)) return json(env, { success: false, error: 'Trình duyệt chưa có định danh bảo mật hợp lệ' }, 400);
  return guardPinCall(request, env, ctx, body, async (response, deviceId) => {
    const data = await response.json().catch(() => null);
    if (!data?.success) return json(env, data || { success: false, error: 'doi PIN that bai' }, response.status);
    const now = Date.now();
    await revokeAllClients(env, deviceId, now);
    const session = await createBrowserSession(request, env, deviceId, clientId, body?.client_name || '');
    if (!session) return json(env, { success: false, error: 'Đã đổi PIN nhưng chưa tạo lại được phiên trình duyệt' }, 503);
    return json(env, {
      ...data,
      pairing_token: session.token,
      session_expires_at: session.expiresAt,
      browser_limit: session.limit,
    });
  });
}

async function handleResetPin(request, env, ctx) {
  const body = await readJson(request);
  const response = await delegate(request, env, ctx);
  if (!response.ok) return response;
  const data = await response.json().catch(() => null);
  if (data?.success && DEVICE_ID_RE.test(String(body?.device_id || ''))) {
    await revokeAllClients(env, String(body.device_id), Date.now());
  }
  return json(env, data || { success: true }, response.status);
}

async function handleBrowserAuthorizedDelegate(request, env, ctx) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const clientId = String(body?.client_id || '');
  const token = String(body?.pairing_token || '');
  if (!await verifyBrowserSession(env, deviceId, clientId, token)) {
    return json(env, { success: false, error: 'Phiên trình duyệt đã hết hạn hoặc đã bị đăng xuất' }, 401);
  }
  const rewritten = await rewriteForLegacyWorker(request, env, body);
  if (!rewritten) return json(env, { success: false, error: 'Thiết bị chưa có phiên máy chủ hợp lệ' }, 503);
  const response = await delegate(rewritten, env, ctx);
  if (response.ok && new URL(request.url).pathname === '/api/push/subscribe') {
    const endpoint = String(body?.subscription?.endpoint || '');
    if (endpoint) {
      await env.DB.prepare(
        'UPDATE push_subscriptions SET client_id = ?3 WHERE device_id = ?1 AND endpoint = ?2'
      ).bind(deviceId, endpoint, clientId).run();
    }
  }
  return response;
}

async function handleSessionCheck(request, env) {
  const body = await readJson(request);
  const deviceId = String(body?.device_id || '').trim();
  const clientId = String(body?.client_id || '');
  const token = String(body?.pairing_token || '');
  const row = await verifyBrowserSession(env, deviceId, clientId, token);
  if (!row) return json(env, { success: false, error: 'Phiên trình duyệt không còn hiệu lực' }, 401);
  return json(env, { success: true, expires_at: Number(row.expires_at || 0) });
}

async function handleStatus(request, env, ctx, deviceId) {
  const url = new URL(request.url);
  const clientId = String(url.searchParams.get('client_id') || '');
  const token = String(url.searchParams.get('pairing_token') || '');
  if (!await verifyBrowserSession(env, deviceId, clientId, token)) {
    return json(env, { success: false, error: 'Phiên trình duyệt không hợp lệ' }, 401);
  }
  return delegate(request, env, ctx);
}

function browserSessionsFromBody(body) {
  return Array.isArray(body?.browser_sessions) ? body.browser_sessions : [];
}

async function validSessionForSubscription(env, sessions, subscriptionRow) {
  const deviceId = String(subscriptionRow?.device_id || '');
  const subscriptionClientId = String(subscriptionRow?.client_id || '');
  const candidate = sessions.find((item) =>
    String(item?.device_id || '') === deviceId &&
    String(item?.client_id || '') === subscriptionClientId
  );
  if (!candidate) return null;
  return verifyBrowserSession(
    env,
    deviceId,
    String(candidate.client_id || ''),
    String(candidate.pairing_token || '')
  );
}

async function handlePushDelete(request, env) {
  const body = await readJson(request);
  const endpoint = String(body?.endpoint || '');
  if (!endpoint) return json(env, { success: false, error: 'thieu endpoint' }, 400);
  const sessions = browserSessionsFromBody(body);
  const { results } = await env.DB.prepare(
    'SELECT device_id, client_id FROM push_subscriptions WHERE endpoint = ?1'
  ).bind(endpoint).all();
  let removed = 0;
  for (const sub of (results || [])) {
    const candidate = sessions.find((item) =>
      String(item?.device_id || '') === String(sub.device_id || '') &&
      String(item?.client_id || '') === String(sub.client_id || '')
    );
    if (!candidate) continue;
    const valid = await verifyBrowserSession(
      env,
      String(sub.device_id || ''),
      String(candidate.client_id || ''),
      String(candidate.pairing_token || '')
    );
    if (!valid) continue;
    await env.DB.prepare(
      'DELETE FROM push_subscriptions WHERE endpoint = ?1 AND device_id = ?2 AND client_id = ?3'
    ).bind(endpoint, sub.device_id, sub.client_id).run();
    removed += 1;
  }
  if (!removed) return json(env, { success: false, error: 'Không có phiên trình duyệt hợp lệ để hủy thông báo' }, 401);
  return json(env, { success: true, removed });
}

async function handlePushTest(request, env) {
  const body = await readJson(request);
  const endpoint = String(body?.endpoint || '');
  if (!endpoint) return json(env, { success: false, error: 'thieu endpoint' }, 400);
  const sessions = browserSessionsFromBody(body);
  const { results } = await env.DB.prepare(
    `SELECT p.*, d.device_name FROM push_subscriptions p
     LEFT JOIN devices d ON d.device_id = p.device_id
     WHERE p.endpoint = ?1`
  ).bind(endpoint).all();
  let sub = null;
  for (const row of (results || [])) {
    if (await validSessionForSubscription(env, sessions, row)) {
      sub = row;
      break;
    }
  }
  if (!sub) return json(env, { success: false, error: 'Phiên trình duyệt không hợp lệ' }, 401);
  const notification = {
    title: `🔔 Test thành công - ${sub.device_name || sub.device_id}`,
    body: 'Thiết bị này đã kết nối thông báo thành công.',
    icon: './icons/icon-192.png',
    badge: './icons/badge-72.png',
    data: {
      deviceId: sub.device_id,
      alarmType: 'TEST',
      severity: 'info',
      state: 'active',
      url: `./?device=${encodeURIComponent(sub.device_id || '')}`,
      ts: Date.now(),
    },
  };
  const result = await sendWebPush(env, sub, notification);
  if (!result.ok && result.gone) {
    await env.DB.prepare(
      'DELETE FROM push_subscriptions WHERE endpoint = ?1 AND device_id = ?2'
    ).bind(endpoint, sub.device_id).run();
  }
  return json(env, {
    success: result.ok,
    notification_sent: result.ok ? 1 : 0,
    ...(result.ok ? {} : { push_status: result.status, push_error: result.error || '' }),
  });
}

export default {
  async scheduled(event, env, ctx) {
    if (worker.scheduled) return worker.scheduled(event, env, ctx);
  },

  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname;

    if (request.method === 'OPTIONS') return delegate(request, env, ctx);

    try {
      if (path === '/api/device/register' && request.method === 'POST') {
        return handleSecureRegister(request, env, ctx);
      }
      if (path === '/api/device/verify-pin' && request.method === 'POST') {
        return handleVerifyPin(request, env, ctx);
      }
      if (path === '/api/device/change-pin' && request.method === 'POST') {
        return handleChangePin(request, env, ctx);
      }
      if (path === '/api/device/rename' && request.method === 'POST') {
        const body = await readJson(request);
        return guardPinCall(request, env, ctx, body);
      }
      if (path === '/api/device/reset-pin' && request.method === 'POST') {
        return handleResetPin(request, env, ctx);
      }
      if (path === '/api/device/mqtt-session' && request.method === 'POST') {
        return handleBrowserAuthorizedDelegate(request, env, ctx);
      }
      if (path === '/api/device/session-check' && request.method === 'POST') {
        return handleSessionCheck(request, env);
      }
      if (path === '/api/push/subscribe' && request.method === 'POST') {
        return handleBrowserAuthorizedDelegate(request, env, ctx);
      }
      if (path === '/api/push/subscribe' && request.method === 'DELETE') {
        return handlePushDelete(request, env);
      }
      if (path === '/api/push/test' && request.method === 'POST') {
        return handlePushTest(request, env);
      }
      const statusMatch = path.match(/^\/api\/device\/(MAP-[A-F0-9]{12})\/status$/);
      if (statusMatch && request.method === 'GET') {
        return handleStatus(request, env, ctx, statusMatch[1]);
      }
      return delegate(request, env, ctx);
    } catch (error) {
      console.error('[security-wrapper]', String(error?.stack || error?.message || error));
      return json(env, { success: false, error: 'internal error' }, 500);
    }
  },
};
