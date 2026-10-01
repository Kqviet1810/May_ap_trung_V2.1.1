import { createRemoteJWKSet, jwtVerify, SignJWT, importPKCS8 } from 'jose';
import { randomToken, hashDeviceKey, timingSafeEqual } from './auth.js';

const googleKeys = createRemoteJWKSet(new URL('https://www.googleapis.com/oauth2/v3/certs'),
  { timeoutDuration: 5000, cooldownDuration: 30000, cacheMaxAge: 3600000 });
export const cookieName = '__Host-mayap_session';
export const csrfCookie = '__Host-mayap_csrf';
export const hash = (value, env) => hashDeviceKey(value, env.MAYAP_SESSION_PEPPER);
export const cookie = (name, value, age) => `${name}=${value}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${age}`;
export function cookies(request) {
  return Object.fromEntries((request.headers.get('Cookie') || '').split(';').map(s => s.trim().split('=')));
}
export function json(data, status = 200, extra = {}) {
  return new Response(JSON.stringify(data), { status, headers: {
    'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff', ...extra,
  }});
}
export async function verifyGoogle(token, clientId, nonce, keys = googleKeys) {
  const { payload } = await jwtVerify(token, keys, {
    algorithms: ['RS256'], issuer: ['https://accounts.google.com', 'accounts.google.com'],
    audience: clientId, clockTolerance: 5, maxTokenAge: '1h',
    requiredClaims: ['sub', 'exp', 'iat', 'nonce'],
  });
  if (payload.nonce !== nonce || typeof payload.sub !== 'string' || !payload.sub || payload.sub.length > 255 ||
      (payload.azp && payload.azp !== clientId) || (Array.isArray(payload.aud) && payload.aud.length > 1 && payload.azp !== clientId))
    throw new Error('Invalid Google identity');
  return payload;
}
export async function session(request, env) {
  if (!env.MAYAP_SESSION_PEPPER) return null;
  const token = cookies(request)[cookieName] || '';
  if (!/^[a-f0-9]{64}$/.test(token)) return null;
  return env.DB.prepare(`SELECT s.*, u.email, u.name FROM user_sessions s JOIN users u
    ON u.google_sub=s.user_sub WHERE s.token_hash=? AND s.revoked_at IS NULL
    AND s.expires_at>? AND u.disabled=0`).bind(await hash(token, env), Date.now()).first();
}
export async function csrf(request, env, auth) {
  return request.headers.get('Origin') === new URL(request.url).origin &&
    timingSafeEqual(await hash(request.headers.get('X-Mayap-CSRF') || '', env), auth.csrf_hash);
}
export async function deviceList(env, sub) {
  const { results } = await env.DB.prepare(`SELECT d.device_id, d.device_name, d.status, d.last_seen,
    d.batch_running, ud.role, (SELECT COUNT(*) FROM push_subscriptions ps
      JOIN user_sessions s ON s.id=ps.user_session_id AND s.revoked_at IS NULL AND s.expires_at>unixepoch()*1000
      WHERE ps.device_id=d.device_id AND ps.user_sub=ud.user_sub) AS linked_browsers
    FROM user_devices ud JOIN devices d ON d.device_id=ud.device_id
    LEFT JOIN device_inventory i ON i.device_id=d.device_id
    WHERE ud.user_sub=? AND COALESCE(i.enabled,1)=1 ORDER BY ud.created_at, d.device_id`).bind(sub).all();
  return results;
}
export async function permission(env, sub, id, write = false) {
  return env.DB.prepare(`SELECT d.*, ud.role FROM user_devices ud JOIN devices d ON d.device_id=ud.device_id
    LEFT JOIN device_inventory i ON i.device_id=d.device_id
    WHERE ud.user_sub=? AND ud.device_id=? AND COALESCE(i.enabled,1)=1
    ${write ? "AND ud.role IN ('owner','operator')" : ''}`).bind(sub, id).first();
}
export async function createSession(env, identity, agent = '') {
  const now = Date.now(), token = randomToken(32), csrfToken = randomToken(32), id = randomToken(24);
  const expiry = now + 7 * 86400000;
  await env.DB.prepare(`INSERT INTO users(google_sub,email,name,created_at,last_login_at) VALUES(?,?,?,?,?)
    ON CONFLICT(google_sub) DO UPDATE SET email=excluded.email,name=excluded.name,last_login_at=excluded.last_login_at`)
    .bind(identity.sub, String(identity.email || '').slice(0,254), String(identity.name || '').slice(0,100), now, now).run();
  const enabled = await env.DB.prepare('SELECT disabled FROM users WHERE google_sub=?').bind(identity.sub).first();
  if (enabled.disabled) throw new Error('Account disabled');
  await env.DB.prepare(`INSERT INTO user_sessions(id,user_sub,token_hash,csrf_hash,created_at,expires_at,user_agent)
    VALUES(?,?,?,?,?,?,?)`).bind(id, identity.sub, await hash(token,env), await hash(csrfToken,env), now, expiry, agent.slice(0,200)).run();
  return { token, csrfToken, id, expiry };
}
export async function mqttTicket(env, auth, clientId) {
  if (env.MQTT_ISOLATION_READY !== '1' || !/^wss:\/\//.test(env.MQTT_GATEWAY_URL || '') || !env.MQTT_TICKET_PRIVATE_KEY)
    throw new Error('MQTT_ISOLATION_NOT_READY');
  const devices = await deviceList(env, auth.user_sub);
  const key = await importPKCS8(env.MQTT_TICKET_PRIVATE_KEY, 'EdDSA');
  const exp = Math.min(Math.floor(auth.expires_at/1000), Math.floor(Date.now()/1000) + 900);
  const ticket = await new SignJWT({ sid: auth.id, cid: clientId,
    read: devices.map(d=>d.device_id), write: devices.filter(d=>d.role!=='viewer').map(d=>d.device_id) })
    .setProtectedHeader({ alg: 'EdDSA', typ: 'JWT' }).setIssuer(env.APP_ORIGIN)
    .setAudience('mayap-mqtt-gateway').setSubject(auth.user_sub).setIssuedAt().setExpirationTime(exp).sign(key);
  return { url: env.MQTT_GATEWAY_URL, username: clientId, password: ticket, expiresAt: exp };
}
export async function hmacHex(secret, value) {
  const key = await crypto.subtle.importKey('raw', typeof secret === 'string' ? new TextEncoder().encode(secret) : secret,
    { name:'HMAC', hash:'SHA-256' }, false, ['sign']);
  return Array.from(new Uint8Array(await crypto.subtle.sign('HMAC',key,new TextEncoder().encode(value))), b=>b.toString(16).padStart(2,'0')).join('');
}
export async function controlGrant(env, id, cid) {
  if (!env.DEVICE_KEY_PEPPER) throw new Error('Missing command pepper');
  const commandKey = await hmacHex(env.DEVICE_KEY_PEPPER, 'mayap-command-key:v1:'+id);
  const bytes = new Uint8Array(commandKey.match(/../g).map(v=>parseInt(v,16)));
  const expiresAt = Math.floor(Date.now()/1000)+300, grant=`${cid}|${expiresAt}|${randomToken(12)}`;
  return { grant, expiresAt, grantSig:await hmacHex(bytes,`mayap-control-grant:v2\n${id}\n${grant}`),
    sessionKey:await hmacHex(bytes,`mayap-control-session:v2\n${id}\n${grant}`) };
}
