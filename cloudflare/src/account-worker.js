import physicalWorker from './reliability-wrapper.js';
import legacy from './index.js';
import { randomToken, verifyDeviceKey, timingSafeEqual } from './auth.js';
import { cookieName, csrfCookie, cookie, cookies, hash, json, session, csrf,
  deviceList, permission, createSession, verifyGoogle, mqttTicket, controlGrant, hmacHex } from './account-auth.js';

const idRe = /^MAP-[A-F0-9]{12}$/;
const physical = new Set(['/api/device/register','/api/device/heartbeat','/api/device/reset-pin',
  '/api/device/rotate-key','/api/device/alarm','/api/firmware/check']);
const deny = (status=403) => json({ success:false, error:status===401?'ACCOUNT_LOGIN_REQUIRED':'ACCESS_DENIED' },status);
async function body(request) {
  if (Number(request.headers.get('Content-Length') || 0)>8192) throw new Error('BODY_TOO_LARGE');
  const text=await boundedText(request,8192);
  const value=JSON.parse(text);
  if (!value || typeof value!=='object' || Array.isArray(value)) throw new Error('INVALID_BODY');
  return value;
}
async function boundedText(request,limit) {
  if(!request.body)return '';
  const reader=request.body.getReader(), chunks=[];let size=0;
  try {
    for(;;){const {value,done}=await reader.read();if(done)break;size+=value.byteLength;
      if(size>limit){await reader.cancel();throw new Error('BODY_TOO_LARGE');}chunks.push(value);}
  }finally{reader.releaseLock();}
  const bytes=new Uint8Array(size);let offset=0;for(const chunk of chunks){bytes.set(chunk,offset);offset+=chunk.byteLength;}
  return new TextDecoder().decode(bytes);
}
function origin(env) {
  const url=new URL(env.APP_ORIGIN);
  if (url.protocol!=='https:' || url.origin!==env.APP_ORIGIN) throw new Error('APP_ORIGIN_INVALID');
  return url.origin;
}
async function loginStart(request, env) {
  const base=origin(env);
  if (!env.GOOGLE_CLIENT_ID || !env.GOOGLE_CLIENT_SECRET || !env.MAYAP_SESSION_PEPPER)
    return json({success:false,error:'GOOGLE_LOGIN_NOT_CONFIGURED'},503);
  const state=randomToken(32), nonce=randomToken(32), verifier=randomToken(32);
  const digest=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(verifier));
  const challenge=btoa(String.fromCharCode(...new Uint8Array(digest))).replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,'');
  await env.DB.prepare('INSERT INTO oauth_transactions(state_hash,nonce,verifier,expires_at) VALUES(?,?,?,?)')
    .bind(await hash(state,env),nonce,verifier,Date.now()+600000).run();
  const url=new URL('https://accounts.google.com/o/oauth2/v2/auth');
  url.search=new URLSearchParams({client_id:env.GOOGLE_CLIENT_ID,redirect_uri:base+'/auth/google/callback',
    response_type:'code',scope:'openid email profile',state,nonce,code_challenge:challenge,code_challenge_method:'S256'});
  return new Response(null,{status:302,headers:{Location:url.href,'Cache-Control':'no-store',
    'Set-Cookie':cookie('__Host-mayap_oauth',state,600)}});
}
async function loginCallback(request,env) {
  const url=new URL(request.url), state=url.searchParams.get('state') || '';
  const fail=()=>new Response(null,{status:303,headers:{Location:origin(env)+'/?login_error=1',
    'Cache-Control':'no-store','Set-Cookie':cookie('__Host-mayap_oauth','',0)}});
  if (!/^[a-f0-9]{64}$/.test(state) || !timingSafeEqual(state,cookies(request)['__Host-mayap_oauth'] || '')) return fail();
  const tx=await env.DB.prepare('DELETE FROM oauth_transactions WHERE state_hash=? AND expires_at>? RETURNING *')
    .bind(await hash(state,env),Date.now()).first();
  if (!tx || !url.searchParams.get('code')) return fail();
  try {
    const res=await fetch('https://oauth2.googleapis.com/token',{method:'POST',signal:AbortSignal.timeout(8000),
      body:new URLSearchParams({client_id:env.GOOGLE_CLIENT_ID,client_secret:env.GOOGLE_CLIENT_SECRET,
        code:url.searchParams.get('code'),grant_type:'authorization_code',redirect_uri:origin(env)+'/auth/google/callback',code_verifier:tx.verifier})});
    if (!res.ok) return fail();
    const tokens=await res.json(), identity=await verifyGoogle(tokens.id_token,env.GOOGLE_CLIENT_ID,tx.nonce);
    // Rotate an existing session on login; no Google access/refresh token is persisted.
    const old=await session(request,env);
    if(old) await revoke(env,old.id,old.user_sub);
    const own=await createSession(env,identity,request.headers.get('User-Agent') || '');
    const headers=new Headers({Location:origin(env)+'/', 'Cache-Control':'no-store'});
    headers.append('Set-Cookie',cookie(cookieName,own.token,604800));
    headers.append('Set-Cookie',cookie(csrfCookie,own.csrfToken,604800));
    headers.append('Set-Cookie',cookie('__Host-mayap_oauth','',0));
    return new Response(null,{status:303,headers});
  } catch { return fail(); }
}
async function revoke(env,id,sub) {
  await env.DB.batch([
    env.DB.prepare('UPDATE user_sessions SET revoked_at=? WHERE id=? AND user_sub=?').bind(Date.now(),id,sub),
    env.DB.prepare('DELETE FROM push_subscriptions WHERE user_session_id=? AND user_sub=?').bind(id,sub),
  ]);
}
async function claim(request, env, auth, data) {
  const id=String(data.device_id || '').toUpperCase(), pin=String(data.pin || '');
  if (!idRe.test(id) || !/^[0-9]{4,8}$/.test(pin)) return json({success:false,error:'INVALID_CLAIM'},400);
  const now=Date.now(), ip=request.headers.get('CF-Connecting-IP') || 'unknown';
  // Reserve attempts atomically BEFORE verifying. Parallel requests cannot skip the limit.
  const keys=[`claim:device:${id}`,`claim:ip:${ip}`];
  const rows=await env.DB.batch(keys.map(key=>env.DB.prepare(`INSERT INTO auth_rate_limits
    (rate_key,attempts,window_started_at,blocked_until,updated_at) VALUES(?,1,?,0,?)
    ON CONFLICT(rate_key) DO UPDATE SET
    attempts=CASE WHEN window_started_at<? THEN 1 ELSE attempts+1 END,
    window_started_at=CASE WHEN window_started_at<? THEN excluded.window_started_at ELSE window_started_at END,
    updated_at=excluded.updated_at RETURNING attempts`).bind(key,now,now,now-900000,now-900000)));
  if (rows[0].results[0].attempts>5 || rows[1].results[0].attempts>30)
    return json({success:false,error:'Thử quá nhiều lần. Vui lòng chờ 15 phút.'},429);
  const device=await env.DB.prepare(`SELECT d.* FROM devices d LEFT JOIN device_inventory i ON i.device_id=d.device_id
    WHERE d.device_id=? AND COALESCE(i.enabled,1)=1`).bind(id).first();
  if (!device || !env.DEVICE_KEY_PEPPER || !await verifyDeviceKey(pin,env.DEVICE_KEY_PEPPER,device.web_pin_hash))
    return json({success:false,error:'Device ID hoặc PIN không đúng.'},403);
  await env.DB.prepare(`INSERT OR IGNORE INTO user_devices(user_sub,device_id,role,created_at)
    SELECT ?,?,'owner',? WHERE NOT EXISTS(SELECT 1 FROM user_devices WHERE device_id=? AND role='owner')`)
    .bind(auth.user_sub,id,now,id).run();
  const owner=await env.DB.prepare("SELECT user_sub FROM user_devices WHERE device_id=? AND role='owner'").bind(id).first();
  if (owner?.user_sub!==auth.user_sub) return json({success:false,error:'Thiết bị đã thuộc một tài khoản khác.'},409);
  return json({success:true,device_id:id,device_name:device.device_name || id});
}
async function gatewayCheck(request,env) {
  const raw=await boundedText(request,16384), at=Number(request.headers.get('X-Mayap-Time'));
  if (raw.length>16384 || !env.MQTT_GATEWAY_CHECK_SECRET || !Number.isFinite(at) || Math.abs(Date.now()-at)>30000 ||
      !timingSafeEqual(await hmacHex(env.MQTT_GATEWAY_CHECK_SECRET,`${at}\n${raw}`),request.headers.get('X-Mayap-Signature') || '')) return deny();
  const data=JSON.parse(raw);
  if (!Array.isArray(data.sessions) || data.sessions.length>100) return json({success:false},400);
  const result=[];
  for (const id of new Set(data.sessions)) {
    if (typeof id!=='string' || id.length>64) continue;
    const auth=await env.DB.prepare(`SELECT s.* FROM user_sessions s JOIN users u ON s.user_sub=u.google_sub
      WHERE s.id=? AND s.revoked_at IS NULL AND s.expires_at>? AND u.disabled=0`).bind(id,Date.now()).first();
    if (!auth) continue;
    const devices=await deviceList(env,auth.user_sub);
    result.push({sid:id,sub:auth.user_sub,read:devices.map(d=>d.device_id),write:devices.filter(d=>d.role!=='viewer').map(d=>d.device_id)});
  }
  return json({success:true,sessions:result});
}
async function fetchAccount(request, env, ctx) {
  const url=new URL(request.url), path=url.pathname, method=request.method;
  // Existing physical-device authentication and task architecture stay unchanged.
  if ((method==='POST' && physical.has(path)) || (method==='GET' && /^\/api\/firmware\/download\//.test(path)))
    return physicalWorker.fetch(request,env,ctx);
  if (method==='POST' && path==='/api/gateway/sessions/check') return gatewayCheck(request,env);
  if (url.origin!==origin(env)) return deny();
  if (method==='GET' && path==='/auth/google/start') return loginStart(request,env);
  if (method==='GET' && path==='/auth/google/callback') return loginCallback(request,env);
  if (!path.startsWith('/api/')) {
    if (!['GET','HEAD'].includes(method) || !env.ASSETS) return json({success:false},404);
    const res=await env.ASSETS.fetch(request), headers=new Headers(res.headers);
    headers.set('X-Content-Type-Options','nosniff');
    headers.set('Referrer-Policy','same-origin');
    headers.set('Cache-Control',path==='/' || path.endsWith('.html') ? 'no-cache' : 'public, max-age=300');
    headers.set('Content-Security-Policy',`default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self' ${env.MQTT_GATEWAY_URL || "'self'"}; frame-ancestors 'none'; base-uri 'self'; object-src 'none'; form-action 'self'`);
    return new Response(res.body,{status:res.status,headers});
  }
  // No permissive CORS or legacy browser credential fallback.
  if (path==='/api/device/sign-mqtt' || path==='/api/device/verify-pin' || path==='/api/device/session-check')
    return json({success:false,error:'ACCOUNT_UPGRADE_REQUIRED'},410);
  const auth=await session(request,env);
  if (!auth) return deny(401);
  if (!['GET','HEAD'].includes(method) && !await csrf(request,env,auth)) return deny();
  if (path==='/api/account/session' && method==='GET') return json({success:true,
    user:{sub:auth.user_sub,name:auth.name,email:auth.email},expiresAt:auth.expires_at,
    csrf:cookies(request)[csrfCookie],devices:await deviceList(env,auth.user_sub)});
  const data=method==='GET' ? {} : await body(request);
  if (path==='/api/account/logout' && method==='POST') {
    await revoke(env,auth.id,auth.user_sub);
    const headers=new Headers({'Content-Type':'application/json','Cache-Control':'no-store'});
    headers.append('Set-Cookie',cookie(cookieName,'',0)); headers.append('Set-Cookie',cookie(csrfCookie,'',0));
    return new Response('{"success":true}',{headers});
  }
  if (path==='/api/account/sessions' && method==='GET') {
    const {results}=await env.DB.prepare('SELECT id,created_at,expires_at,user_agent FROM user_sessions WHERE user_sub=? AND revoked_at IS NULL AND expires_at>?')
      .bind(auth.user_sub,Date.now()).all(); return json({success:true,sessions:results});
  }
  if (path==='/api/account/sessions/revoke' && method==='POST') {
    await revoke(env,String(data.session_id || ''),auth.user_sub); return json({success:true});
  }
  if (path==='/api/account/devices/claim' && method==='POST') return claim(request,env,auth,data);
  if (path==='/api/push/vapid-public-key' && method==='GET') return legacy.fetch(request,env,ctx);
  if (path==='/api/firmware/latest' && method==='GET') return legacy.fetch(request,env,ctx);
  const match=path.match(/^\/api\/device\/(MAP-[A-F0-9]{12})\/(status|history|config)$/);
  const id=match?.[1] || String(data.device_id || '');
  if (match && method==='GET') {
    const device=await permission(env,auth.user_sub,id);
    if (!device) return deny();
    if (match[2]==='config') return json({success:false,error:'LOAD_CONFIG_OVER_AUTHORIZED_MQTT'},409);
    if (match[2]==='history') {
      const {results}=await env.DB.prepare('SELECT recorded_at,temperature,humidity,batch_running FROM telemetry_history WHERE device_id=? ORDER BY recorded_at DESC LIMIT 500').bind(id).all();
      return json({success:true,points:results});
    }
    const subs=await env.DB.prepare('SELECT COUNT(*) AS n FROM push_subscriptions WHERE device_id=? AND user_sub=?').bind(id,auth.user_sub).first();
    return json({success:true,exists:true,device_id:id,device_name:device.device_name,status:device.status,last_seen:device.last_seen,
      batch_running:!!device.batch_running,subscription_count:subs.n});
  }
  if (path==='/api/device/mqtt-session' && method==='POST') {
    const device=await permission(env,auth.user_sub,id), cid=String(data.control_client_id || '');
    if (!device) return deny();
    if (!/^[A-Za-z0-9_-]{8,40}$/.test(cid)) return json({success:false,error:'INVALID_CLIENT'},400);
    try { return json({success:true,mqtt:await mqttTicket(env,auth,cid),
      control:device.role==='viewer' ? null : await controlGrant(env,id,cid)}); }
    catch { return json({success:false,error:'MQTT_ISOLATION_NOT_READY'},503); }
  }
  if (path==='/api/device/rename' && method==='POST') {
    const device=await permission(env,auth.user_sub,id,true);
    if (!device || device.role!=='owner') return deny();
    const name=String(data.device_name || data.name || '').trim().slice(0,64);
    if (!name) return json({success:false},400);
    await env.DB.prepare('UPDATE devices SET device_name=? WHERE device_id=?').bind(name,id).run();
    return json({success:true,device_name:name});
  }
  if (path==='/api/device/change-pin' && method==='POST') {
    const device=await permission(env,auth.user_sub,id,true);
    if (!device || device.role!=='owner') return deny();
    // Legacy handler still verifies old PIN; never returns the legacy pairing secret to Web.
    const response=await legacy.fetch(new Request(request.url,{method:'POST',headers:request.headers,
      body:JSON.stringify(data)}),env,ctx), value=await response.json(); delete value.pairing_token;
    return json(value,response.status);
  }
  if (path==='/api/push/subscribe' && method==='POST') {
    const device=await permission(env,auth.user_sub,id), sub=data.subscription;
    if (!device) return deny();
    if (!sub?.endpoint?.startsWith('https://') || !sub.keys?.auth || !sub.keys?.p256dh) return json({success:false},400);
    const existing=await env.DB.prepare('SELECT user_sub FROM push_subscriptions WHERE endpoint=? LIMIT 1').bind(sub.endpoint).first();
    if (existing && existing.user_sub!==auth.user_sub) return deny();
    await env.DB.prepare(`INSERT INTO push_subscriptions(device_id,endpoint,p256dh,auth,user_agent,created_at,updated_at,user_sub,user_session_id)
      VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(device_id,endpoint) DO UPDATE SET p256dh=excluded.p256dh,
      auth=excluded.auth,updated_at=excluded.updated_at,user_session_id=excluded.user_session_id`)
      .bind(id,sub.endpoint,sub.keys.p256dh,sub.keys.auth,request.headers.get('User-Agent') || '',Date.now(),Date.now(),auth.user_sub,auth.id).run();
    return json({success:true});
  }
  if ((path==='/api/push/subscribe' && method==='DELETE') || (path==='/api/push/test' && method==='POST')) {
    const sub=await env.DB.prepare(`SELECT ps.* FROM push_subscriptions ps JOIN user_devices ud
      ON ud.device_id=ps.device_id AND ud.user_sub=ps.user_sub WHERE ps.endpoint=? AND ps.user_sub=? LIMIT 1`)
      .bind(String(data.endpoint || ''),auth.user_sub).first();
    if(!sub) return deny();
    if(method==='DELETE') {
      await env.DB.prepare('DELETE FROM push_subscriptions WHERE endpoint=? AND user_sub=?').bind(data.endpoint,auth.user_sub).run();
      return json({success:true});
    }
    return legacy.fetch(new Request(request.url,{method,headers:request.headers,body:JSON.stringify(data)}),env,ctx);
  }
  return json({success:false,error:'NOT_FOUND'},404);
}
export default {
  async fetch(request,env,ctx) { try {return await fetchAccount(request,env,ctx);} catch(error) {
    const bad=['INVALID_BODY','BODY_TOO_LARGE'].includes(error.message) || error instanceof SyntaxError;
    return json({success:false,error:bad?'INVALID_REQUEST':'SERVICE_UNAVAILABLE'},bad?400:503);
  }},
  async scheduled(event,env,ctx) {
    await env.DB.batch([
      env.DB.prepare('DELETE FROM oauth_transactions WHERE expires_at<?').bind(Date.now()),
      env.DB.prepare('DELETE FROM push_subscriptions WHERE user_session_id IN (SELECT id FROM user_sessions WHERE revoked_at IS NOT NULL OR expires_at<?)').bind(Date.now()),
    ]);
    return physicalWorker.scheduled(event,env,ctx);
  },
};
