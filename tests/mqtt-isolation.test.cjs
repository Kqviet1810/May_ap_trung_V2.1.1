const test=require('node:test'),assert=require('node:assert/strict');
const {pathToFileURL}=require('node:url'),{EventEmitter,once}=require('node:events');
const ws=require('../mqtt-gateway/node_modules/ws'),packet=require('../mqtt-gateway/node_modules/mqtt-packet');
const modules=Promise.all([import('../mqtt-gateway/acl.js'),import('../mqtt-gateway/server.js'),
  import(pathToFileURL(require.resolve('../mqtt-gateway/node_modules/jose')).href)]);
const A='MAP-000000000001',B='MAP-000000000002',issuer='https://account.example';
const topic=(id,suffix)=>`mayap/v1/${id}/${suffix}`;
test('MQTT ACL rejects wildcards, foreign devices, retained writes, V1 and viewer control',async()=>{
  const [acl]=await modules,claims={read:[A],write:[A],cid:'w-client0000'};
  for(const suffix of ['presence','bootstrap','snapshot','ack','config/reported','history/reported','reminders/reported','log']){
    assert.equal(acl.allowed(topic(A,suffix),claims),true);assert.equal(acl.allowed(topic(B,suffix),claims),false);
  }
  for(const bad of ['#','mayap/#','mayap/v1/+/snapshot','$share/x/'+topic(A,'snapshot'),'$SYS/#',topic(A,'snapshot/#')])
    assert.equal(acl.allowed(bad,claims),false);
  const command={cmd:'publish',topic:topic(A,'command'),qos:1,retain:false,payload:Buffer.from('{"v":2}')};
  assert.equal(acl.clientPacketAllowed(command,claims),true);
  for(const extra of [{topic:topic(B,'command')},{retain:true},{qos:2},{payload:Buffer.from('{"v":1}')},{payload:Buffer.alloc(1536)}])
    assert.equal(acl.clientPacketAllowed({...command,...extra},claims),false);
  assert.equal(acl.clientPacketAllowed(command,{...claims,write:[]}),false);
  const session={...command,topic:topic(A,'session'),payload:Buffer.from(JSON.stringify({clientId:claims.cid,active:true,ttlMs:45000,foreground:false}))};
  assert.equal(acl.clientPacketAllowed(session,{...claims,write:[]}),true);
  assert.equal(acl.clientPacketAllowed({...session,payload:Buffer.from('{"clientId":"other","active":true,"ttlMs":45000}')},claims),false);
});
async function gateway(){
  const [acl,server,jose]=await modules, keys=await jose.generateKeyPair('EdDSA',{extractable:true}),upstreams=[],received=[];
  let active=true;
  const mint=async(extra={})=>new jose.SignJWT({sid:'session-A',cid:'w-client0000',read:[A],write:[A],...extra})
    .setProtectedHeader({alg:'EdDSA'}).setIssuer(issuer).setAudience('mayap-mqtt-gateway').setSubject('user-A')
    .setIssuedAt().setExpirationTime(extra.exp || '15m').sign(keys.privateKey);
  const upstream=()=>{
    const u=new EventEmitter();u.readyState=ws.WebSocket.OPEN;u.bufferedAmount=0;u.close=()=>{u.readyState=ws.WebSocket.CLOSED;};
    const parser=packet.parser({protocolVersion:4});u.send=data=>parser.parse(data);
    parser.on('packet',p=>{received.push(p);if(p.cmd==='connect')u.deliver({cmd:'connack',returnCode:0,sessionPresent:false});
      if(p.cmd==='subscribe'){
        u.deliver({cmd:'suback',messageId:p.messageId,granted:p.subscriptions.map(s=>s.qos)});
        for(const s of p.subscriptions)u.deliver({cmd:'publish',qos:0,retain:true,topic:s.topic,payload:Buffer.from('{"bootId":123}')});
      }});
    u.deliver=p=>u.emit('message',packet.generate(p,{protocolVersion:4}));upstreams.push(u);setImmediate(()=>u.emit('open'));return u;
  };
  const g=server.createGateway({port:0,issuer,publicKey:await jose.exportSPKI(keys.publicKey),username:'server-only',password:'server-secret',
    upstream:'wss://broker.example/mqtt',checkSecret:'check-secret',createUpstream:upstream,
    fetch:async()=>new Response(JSON.stringify({success:true,sessions:active?[{sid:'session-A',sub:'user-A',read:[A],write:[A]}]:[]}))});
  await once(g.server,'listening');
  async function connect(ticket=undefined){
    const socket=new ws.WebSocket('ws://127.0.0.1:'+g.server.address().port,'mqtt',{origin:issuer}), messages=[];
    const parse=packet.parser({protocolVersion:4});parse.on('packet',p=>messages.push(p));socket.on('message',data=>parse.parse(data));
    await once(socket,'open');socket.send(packet.generate({cmd:'connect',protocolId:'MQTT',protocolVersion:4,clean:true,keepalive:30,
      clientId:'w-client0000',username:'w-client0000',password:Buffer.from(ticket || await mint())}));
    const wait=async(fn)=>{for(let i=0;i<100;i++){if(fn())return;await new Promise(r=>setTimeout(r,5));}assert.fail('Packet timeout');};
    await wait(()=>messages.some(p=>p.cmd==='connack') || socket.readyState===ws.WebSocket.CLOSED);
    return {socket,messages,wait,send:p=>socket.send(packet.generate(p))};
  }
  return {g,mint,connect,upstreams,received,revoke:()=>{active=false;},acl,keys};
}
test('actual WSS-side MQTT parser delivers retained bootstrap and renews ticket without a new broker socket',async()=>{
  const h=await gateway();try{
    const c=await h.connect();assert.equal(c.messages[0].cmd,'connack');
    assert.equal(h.received[0].username,'server-only');assert.equal(h.received[0].password.toString(),'server-secret');
    assert.ok(!c.messages.some(p=>p.password));
    c.send({cmd:'subscribe',messageId:1,subscriptions:[{topic:topic(A,'bootstrap'),qos:1}]});
    await c.wait(()=>c.messages.some(p=>p.cmd==='publish'));
    assert.equal(c.messages.find(p=>p.cmd==='publish').retain,true);
    const renewed=await h.mint();
    c.send({cmd:'publish',topic:'mayap/auth/renew',qos:1,retain:false,messageId:2,payload:Buffer.from(renewed)});
    // Traffic queued during async renewal is forwarded after validation.
    c.send({cmd:'pingreq'});
    await c.wait(()=>c.messages.some(p=>p.cmd==='puback' && p.messageId===2));
    assert.equal(h.upstreams.length,1);assert.ok(!h.received.some(p=>p.topic==='mayap/auth/renew'));
    await c.wait(()=>h.received.some(p=>p.cmd==='pingreq'));
    const close=once(c.socket,'close');c.send({cmd:'subscribe',messageId:3,subscriptions:[{topic:topic(B,'snapshot'),qos:0}]});await close;
    assert.ok(!h.received.some(p=>p.subscriptions?.some(s=>s.topic===topic(B,'snapshot'))));
  }finally{await h.g.close();}
});
test('gateway drops cross-tenant upstream packets and revokes sessions on batch validation',async()=>{
  const h=await gateway();try{
    const c=await h.connect(),closed=once(c.socket,'close');
    h.upstreams[0].deliver({cmd:'publish',qos:0,retain:false,topic:topic(B,'snapshot'),payload:Buffer.from('private-B')});await closed;
    assert.ok(!c.messages.some(p=>p.topic===topic(B,'snapshot')));
    const c2=await h.connect(),closed2=once(c2.socket,'close');h.revoke();await h.g.refresh();await closed2;
  }finally{await h.g.close();}
});
test('invalid/expired MQTT ticket is rejected before opening upstream',async()=>{
  const h=await gateway();try{
    const c=await h.connect('invalid');assert.equal(c.socket.readyState,ws.WebSocket.CLOSED);assert.equal(h.upstreams.length,0);
    const ticket=await h.mint({read:[B],write:[B]});const c2=await h.connect(ticket);
    assert.equal(c2.socket.readyState,ws.WebSocket.CLOSED);assert.equal(h.upstreams.length,0);
    const expired=await h.connect(await h.mint({exp:Math.floor(Date.now()/1000)-1}));
    assert.equal(expired.socket.readyState,ws.WebSocket.CLOSED);assert.equal(h.upstreams.length,0);
  }finally{await h.g.close();}
});
