const test=require('node:test'), assert=require('node:assert/strict'), fs=require('node:fs');
const { DatabaseSync }=require('node:sqlite');
const { pathToFileURL }=require('node:url');
const base='https://account.example';
const modules=Promise.all([import('../cloudflare/src/account-worker.js'),import('../cloudflare/src/account-auth.js'),
  import(pathToFileURL(require.resolve('../cloudflare/node_modules/jose')).href)]);
function database(){
  const sql=new DatabaseSync(':memory:');
  sql.exec(fs.readFileSync('cloudflare/schema.sql','utf8'));
  sql.exec(fs.readFileSync('cloudflare/migrations/0003_telemetry_history.sql','utf8'));
  sql.exec(fs.readFileSync('cloudflare/migrations/0004_accounts.sql','utf8'));
  const DB={prepare(source){const statement=sql.prepare(source);let args=[];
    return {bind(...a){args=a;return this;},async first(){return statement.get(...args) || null;},
      async all(){return {results:statement.all(...args)};},async run(){
        if(statement.columns().length)return {results:statement.all(...args)};
        return {meta:statement.run(...args)};}};},
    async batch(statements){sql.exec('BEGIN');try{const result=[];for(const s of statements)result.push(await s.run());sql.exec('COMMIT');return result;}
      catch(e){sql.exec('ROLLBACK');throw e;}}};
  return {DB,sql};
}
async function setup(){
  const [worker,auth,jose]=await modules, {DB,sql}=database();
  const pair=await jose.generateKeyPair('EdDSA',{extractable:true});
  const env={DB,APP_ORIGIN:base,MAYAP_SESSION_PEPPER:'random-session-test-only',DEVICE_KEY_PEPPER:'random-device-test-only',
    MQTT_ISOLATION_READY:'1',MQTT_GATEWAY_URL:'wss://gateway.example/mqtt',MQTT_TICKET_PRIVATE_KEY:await jose.exportPKCS8(pair.privateKey),MQTT_GATEWAY_CHECK_SECRET:'gateway-test-only'};
  async function login(sub){return auth.createSession(env,{sub,email:sub+'@example.test',name:sub});}
  async function device(n){const id='MAP-'+n.toString(16).toUpperCase().padStart(12,'0');
    sql.prepare('INSERT INTO devices(device_id,device_key_hash,created_at,web_pin_hash) VALUES(?,?,?,?)')
      .run(id,'device-key-test',Date.now(),await auth.hash('123456',{MAYAP_SESSION_PEPPER:env.DEVICE_KEY_PEPPER}));return id;}
  async function call(path,sess,data,method=data?'POST':'GET',extra={}){
    return worker.default.fetch(new Request(base+path,{method,headers:{Cookie:sess?`${auth.cookieName}=${sess.token}; ${auth.csrfCookie}=${sess.csrfToken}`:'',
      Origin:base,'Content-Type':'application/json','X-Mayap-CSRF':sess?.csrfToken || '',...extra},body:data?JSON.stringify(data):undefined}),env,{waitUntil(){}});
  }
  return {env,sql,auth,jose,pair,login,device,call};
}
test('claim checks PIN, refuses takeover, and ownership protects status/history/config/grant',async()=>{
  const h=await setup(), A=await h.login('user-A'), B=await h.login('user-B'), id=await h.device(1);
  assert.equal((await h.call('/api/account/devices/claim',B,{device_id:id,pin:'000000'})).status,403);
  assert.equal((await h.call('/api/account/devices/claim',A,{device_id:id,pin:'123456'})).status,200);
  assert.equal((await h.call('/api/account/devices/claim',B,{device_id:id,pin:'123456'})).status,409);
  for(const route of ['status','history','config'])assert.equal((await h.call(`/api/device/${id}/${route}`,B)).status,403);
  assert.equal((await h.call('/api/device/mqtt-session',B,{device_id:id,control_client_id:'w-browserB123'})).status,403);
  assert.equal((await h.call(`/api/device/${id}/status`,A)).status,200);
  assert.equal((await h.call(`/api/device/${id}/status`,null)).status,401);
  assert.equal((await h.call(`/api/device/${id}/status`,null,undefined,'GET',{Cookie:'pairing_token=legacy-known-token'})).status,401);
  assert.equal((await h.call('/api/device/sign-mqtt',A,{device_id:id})).status,410);
  assert.equal((await h.call('/api/device/session-check',A,{device_id:id})).status,410);
});
test('PIN attempts are reserved atomically and bounded across accounts/IPs',async()=>{
  const h=await setup(), A=await h.login('A'), id=await h.device(2);
  const requests=await Promise.all(Array.from({length:6},()=>h.call('/api/account/devices/claim',A,{device_id:id,pin:'000000'})));
  assert.equal(requests.filter(r=>r.status===429).length,1);
  assert.equal(h.sql.prepare('SELECT attempts FROM auth_rate_limits WHERE rate_key=?').get('claim:device:'+id).attempts,6);
});
test('account session uses Google sub, rejects CSRF/cross origin, logout/expiry/revoke end access',async()=>{
  const h=await setup(), A=await h.login('stable-google-sub'), A2=await h.login('stable-google-sub'), B=await h.login('B');
  const data=await (await h.call('/api/account/session',A)).json();assert.equal(data.user.sub,'stable-google-sub');assert.equal(data.csrf,A.csrfToken);
  assert.equal((await h.call('/api/account/logout',A,{},'POST',{'X-Mayap-CSRF':'wrong'})).status,403);
  assert.equal((await h.call('/api/account/logout',A,{},'POST',{Origin:'https://attacker.example'})).status,403);
  await h.call('/api/account/sessions/revoke',B,{session_id:A2.id});assert.equal((await h.call('/api/account/session',A2)).status,200);
  await h.call('/api/account/sessions/revoke',A,{session_id:A2.id});assert.equal((await h.call('/api/account/session',A2)).status,401);
  const res=await h.call('/api/account/logout',A,{});assert.equal(res.status,200);assert.match(res.headers.get('Set-Cookie'),/HttpOnly; Secure; SameSite=Lax; Max-Age=0/);
  assert.equal((await h.call('/api/account/session',A)).status,401);
  h.sql.prepare('UPDATE user_sessions SET expires_at=? WHERE id=?').run(Date.now()-1,B.id);
  assert.equal((await h.call('/api/account/session',B)).status,401);
});
test('10 customers x 3 devices issue only their own MQTT topics and exact existing V2 grants',async()=>{
  const h=await setup(), accounts=[],ids=[];
  for(let user=0;user<10;user++){
    const s=await h.login('google-'+user);accounts.push(s);ids[user]=[];
    for(let d=0;d<3;d++){
      const id=await h.device(user*3+d+100);ids[user].push(id);
      assert.equal((await h.call('/api/account/devices/claim',s,{device_id:id,pin:'123456'},'POST',{'CF-Connecting-IP':'192.0.2.'+user})).status,200);
    }
    const rows=await (await h.call('/api/account/session',s)).json();assert.equal(rows.devices.length,3);
    const result=await (await h.call('/api/device/mqtt-session',s,{device_id:ids[user][0],control_client_id:'w-browser'+user+'000'})).json();
    assert.equal(result.success,true);
    const {payload}=await h.jose.jwtVerify(result.mqtt.password,h.pair.publicKey,{issuer:base,audience:'mayap-mqtt-gateway'});
    assert.deepEqual(payload.read,ids[user]);assert.deepEqual(payload.write,ids[user]);assert.equal(payload.sub,'google-'+user);
    const keyHex=await h.auth.hmacHex(h.env.DEVICE_KEY_PEPPER,'mayap-command-key:v1:'+ids[user][0]), bytes=Buffer.from(keyHex,'hex');
    assert.equal(result.control.grantSig,await h.auth.hmacHex(bytes,`mayap-control-grant:v2\n${ids[user][0]}\n${result.control.grant}`));
    assert.equal(result.control.sessionKey,await h.auth.hmacHex(bytes,`mayap-control-session:v2\n${ids[user][0]}\n${result.control.grant}`));
  }
  for(let i=0;i<10;i++)for(let j=0;j<10;j++)if(i!==j){
    assert.equal((await h.call(`/api/device/${ids[j][0]}/status`,accounts[i])).status,403);
    assert.equal((await h.call('/api/device/mqtt-session',accounts[i],{device_id:ids[j][1],control_client_id:'w-cross12345'})).status,403);
  }
  assert.equal(h.sql.prepare('SELECT COUNT(*) AS n FROM user_devices').get().n,30);
});
test('MQTT isolation readiness gate cannot leak shared broker credentials',async()=>{
  const h=await setup(), A=await h.login('A'),id=await h.device(8);await h.call('/api/account/devices/claim',A,{device_id:id,pin:'123456'});
  h.env.MQTT_ISOLATION_READY='0';h.env.WEB_MQTT_PASSWORD='shared-old-password';
  const response=await h.call('/api/device/mqtt-session',A,{device_id:id,control_client_id:'w-gated00000'});
  assert.equal(response.status,503);assert.ok(!(await response.text()).includes('shared-old-password'));
});
test('Google RS256 identity validates signature, audience, issuer, expiry, nonce, azp and sub',async()=>{
  const [,auth,jose]=await modules, pair=await jose.generateKeyPair('RS256');
  const mint=(claims={},key=pair.privateKey)=>new jose.SignJWT({nonce:'expected',...claims}).setProtectedHeader({alg:'RS256'})
    .setIssuer(claims.iss || 'https://accounts.google.com').setAudience(claims.aud || 'google-client')
    .setSubject(claims.sub || 'google-sub').setIssuedAt().setExpirationTime(claims.exp || '5m').sign(key);
  const good=await mint({email:'can-change@example.test'});assert.equal((await auth.verifyGoogle(good,'google-client','expected',pair.publicKey)).sub,'google-sub');
  for(const claims of [{aud:'wrong'},{iss:'https://attacker.example'},{exp:Math.floor(Date.now()/1000)-60},{nonce:'wrong'},{azp:'wrong'}])
    await assert.rejects(auth.verifyGoogle(await mint(claims),'google-client','expected',pair.publicKey));
  const other=await jose.generateKeyPair('RS256');await assert.rejects(auth.verifyGoogle(await mint({},other.privateKey),'google-client','expected',pair.publicKey));
  await assert.rejects(auth.verifyGoogle('invalid','google-client','expected',pair.publicKey));
});
test('OIDC start uses state/nonce/PKCE, callback rejects forged/replayed state',async()=>{
  const h=await setup();h.env.GOOGLE_CLIENT_ID='client';h.env.GOOGLE_CLIENT_SECRET='test-only';
  const response=await h.call('/auth/google/start');assert.equal(response.status,302);
  const url=new URL(response.headers.get('Location'));assert.equal(url.searchParams.get('response_type'),'code');
  assert.equal(url.searchParams.get('code_challenge_method'),'S256');assert.equal(url.searchParams.get('redirect_uri'),base+'/auth/google/callback');
  assert.match(response.headers.get('Set-Cookie'),/HttpOnly; Secure; SameSite=Lax/);
  assert.equal(h.sql.prepare('SELECT COUNT(*) AS n FROM oauth_transactions').get().n,1);
  const state=url.searchParams.get('state');
  const noCookie=await h.call('/auth/google/callback?state='+state+'&code=invalid');assert.match(noCookie.headers.get('Location'),/login_error/);
  assert.equal(h.sql.prepare('SELECT COUNT(*) AS n FROM user_sessions').get().n,0);
});
test('Google code callback verifies remote JWKS, creates HttpOnly MAYAP session and consumes state once',async()=>{
  const h=await setup();h.env.GOOGLE_CLIENT_ID='callback-client';h.env.GOOGLE_CLIENT_SECRET='callback-test-only';
  const start=await h.call('/auth/google/start'), state=new URL(start.headers.get('Location')).searchParams.get('state');
  const tx=h.sql.prepare('SELECT * FROM oauth_transactions').get(), keys=await h.jose.generateKeyPair('RS256',{extractable:true});
  const jwk=await h.jose.exportJWK(keys.publicKey);jwk.kid='test-google-key';jwk.alg='RS256';jwk.use='sig';
  const token=await new h.jose.SignJWT({nonce:tx.nonce,email:'display@example.test',name:'Google user'})
    .setProtectedHeader({alg:'RS256',kid:jwk.kid}).setIssuer('https://accounts.google.com')
    .setAudience(h.env.GOOGLE_CLIENT_ID).setSubject('actual-google-sub').setIssuedAt().setExpirationTime('5m').sign(keys.privateKey);
  const originalFetch=global.fetch;let exchanges=0;
  global.fetch=async(url,init)=>{
    if(String(url)==='https://oauth2.googleapis.com/token'){
      exchanges++;const form=init.body;assert.equal(form.get('code_verifier'),tx.verifier);
      assert.equal(form.get('redirect_uri'),base+'/auth/google/callback');
      return new Response(JSON.stringify({id_token:token,access_token:'not-persisted'}));
    }
    if(String(url)==='https://www.googleapis.com/oauth2/v3/certs')return new Response(JSON.stringify({keys:[jwk]}));
    throw new Error('Unexpected remote request');
  };
  try {
    const response=await h.call('/auth/google/callback?state='+state+'&code=test',null,undefined,'GET',{Cookie:'__Host-mayap_oauth='+state});
    assert.equal(response.status,303);assert.equal(response.headers.get('Location'),base+'/');
    assert.match(response.headers.get('Set-Cookie'),/__Host-mayap_session=[a-f0-9]{64}; Path=\/; HttpOnly; Secure/);
    assert.equal(h.sql.prepare('SELECT user_sub FROM user_sessions').get().user_sub,'actual-google-sub');
    assert.equal(h.sql.prepare('SELECT COUNT(*) AS n FROM oauth_transactions').get().n,0);
    const replay=await h.call('/auth/google/callback?state='+state+'&code=test',null,undefined,'GET',{Cookie:'__Host-mayap_oauth='+state});
    assert.match(replay.headers.get('Location'),/login_error/);assert.equal(exchanges,1);
  }finally{global.fetch=originalFetch;}
});
test('viewer can receive read MQTT ticket but cannot obtain control grant or change ownership/config',async()=>{
  const h=await setup(),owner=await h.login('owner'),viewer=await h.login('viewer'),id=await h.device(50);
  await h.call('/api/account/devices/claim',owner,{device_id:id,pin:'123456'});
  h.sql.prepare("INSERT INTO user_devices(user_sub,device_id,role,created_at) VALUES(?,?,'viewer',?)").run('viewer',id,Date.now());
  const data=await (await h.call('/api/device/mqtt-session',viewer,{device_id:id,control_client_id:'w-viewer000'})).json();
  assert.equal(data.control,null);
  const {payload}=await h.jose.jwtVerify(data.mqtt.password,h.pair.publicKey);assert.deepEqual(payload.read,[id]);assert.deepEqual(payload.write,[]);
  assert.equal((await h.call('/api/device/rename',viewer,{device_id:id,name:'bad'})).status,403);
  assert.equal((await h.call('/api/device/change-pin',viewer,{device_id:id,old_pin:'123456',new_pin:'999999'})).status,403);
});
test('fleet heartbeat/account/hidden snapshot budget and V1 retirement are explicit',()=>{
  const config=fs.readFileSync('MAYAP_INDUSTRIAL_v4_0_0/config.h','utf8'),app=fs.readFileSync('app.js','utf8'),
    account=fs.readFileSync('account.js','utf8'),realtime=fs.readFileSync('MAYAP_INDUSTRIAL_v4_0_0/realtime_link.h','utf8');
  assert.match(config,/CLOUD_HEARTBEAT_INTERVAL_MS = 60000UL/);assert.equal(30*86400000/60000,43200);
  assert.match(config,/WEB_SNAPSHOT_WARM_INTERVAL_MS = 3000UL/);assert.match(config,/WEB_SNAPSHOT_ACTIVE_INTERVAL_MS = 400UL/);
  assert.match(account,/Date.now\(\)-lastCheck>=300000/);assert.match(app,/WARM_BACKGROUND_MS = 300000/);
  assert.match(realtime,/if \(!v2\) return/);assert.ok(!fs.readFileSync('config.js','utf8').includes('session-check'));
});
