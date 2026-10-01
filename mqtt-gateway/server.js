import { WebSocket, WebSocketServer } from 'ws';
import mqttPacket from 'mqtt-packet';
import { createHmac, createHash } from 'node:crypto';
import { pathToFileURL } from 'node:url';
import { allowed, clientPacketAllowed, verifyTicket } from './acl.js';

// TLS is terminated by the adjacent reverse proxy. HiveMQ upstream always uses WSS/TLS.
export function createGateway(config) {
  if(!/^wss:\/\//.test(config.upstream) || !/^https:\/\//.test(config.issuer) ||
     !config.checkSecret || !config.publicKey || !config.username || !config.password) throw new Error('Incomplete secure gateway config');
  const server=new WebSocketServer({port:config.port ?? 8080,host:config.host || '127.0.0.1',
    maxPayload:8192,perMessageDeflate:false,handleProtocols:protocols=>protocols.has('mqtt')?'mqtt':false});
  const clients=new Set();
  let checking=false;
  const check=async(sids)=>{
    const raw=JSON.stringify({sessions:[...new Set(sids)]}), at=Date.now();
    const res=await (config.fetch || fetch)(config.issuer+'/api/gateway/sessions/check',{method:'POST',
      signal:AbortSignal.timeout(5000),headers:{'Content-Type':'application/json','X-Mayap-Time':String(at),
        'X-Mayap-Signature':createHmac('sha256',config.checkSecret).update(`${at}\n${raw}`).digest('hex')},body:raw});
    if(!res.ok) throw new Error('Introspection failed');
    const data=await res.json(); if(!data.success || !Array.isArray(data.sessions)) throw new Error('Invalid introspection');
    return new Map(data.sessions.map(s=>[s.sid,s]));
  };
  const refresh=async()=>{
    if(checking || !clients.size) return; checking=true;
    try {
      const sessions=await check([...clients].filter(c=>c.claims).map(c=>c.claims.sid));
      for(const c of clients) if(c.claims) {
        const active=sessions.get(c.claims.sid);
        if(!active || active.sub!==c.claims.sub) c.close();
        else {
          // Never extend beyond the signed ticket, and never re-add rights lost since issuance.
          c.claims.read=c.claims.read.filter(id=>active.read.includes(id));
          c.claims.write=c.claims.write.filter(id=>active.write.includes(id)); c.checkedAt=Date.now();
        }
      }
    } catch { for(const c of clients) if(Date.now()-c.checkedAt>90000) c.close(); }
    finally {checking=false;}
  };
  const timer=setInterval(refresh,60000);timer.unref();
  server.on('connection',(socket,request)=>{
    if(request.headers.origin!==config.issuer || clients.size>=100) {socket.close(1008);return;}
    const c={socket,upstream:null,claims:null,checkedAt:Date.now(),busy:false,ready:false,
      close:()=>{socket.close(1008,'Session ended');c.upstream?.close();}};
    const queued=[];
    let queuedBytes=0;
    clients.add(c);
    const authDeadline=setTimeout(c.close,10000), guard=setInterval(()=>{
      if(c.claims && (Date.now()>=c.claims.exp*1000 || Date.now()-c.checkedAt>90000)) c.close();
    },1000); guard.unref();
    socket.on('close',()=>{clearTimeout(authDeadline);clearInterval(guard);clients.delete(c);c.upstream?.close();});
    socket.on('error',c.close);
    const send=(target,packet)=>{
      if(target?.readyState!==WebSocket.OPEN || target.bufferedAmount>65536) {c.close();return;}
      target.send(mqttPacket.generate(packet,{protocolVersion:4}));
    };
    const parser=mqttPacket.parser({protocolVersion:4});
    parser.on('error',c.close);
    // Bound incomplete MQTT packets too: WebSocket maxPayload alone is insufficient.
    let pendingBytes=0;
    socket.on('message',(data,isBinary)=>{
      if(!isBinary) {c.close();return;}
      pendingBytes+=data.length;
      if(pendingBytes>8192) {c.close();return;}
      parser.parse(data);
    });
    const handle=packet=>{
      pendingBytes=0;
      if(c.busy && c.ready) {
        queuedBytes+=mqttPacket.generate(packet,{protocolVersion:4}).length;
        if(queued.length>=32 || queuedBytes>32768) {c.close();return;}
        queued.push(packet);return;
      }
      if(!c.claims) {
        if(c.busy || packet.cmd!=='connect' || packet.protocolVersion!==4 || !packet.clean || packet.will ||
          packet.clientId!==packet.username || !packet.password || packet.password.length>7000) {c.close();return;}
        c.busy=true;
        (async()=>{
          try {
            const claims=await verifyTicket(packet.password.toString(),config);
            if(claims.cid!==packet.clientId) throw new Error('Client mismatch');
            const active=(await check([claims.sid])).get(claims.sid);
            if(!active || active.sub!==claims.sub || !claims.read.every(id=>active.read.includes(id)) ||
               !claims.write.every(id=>active.write.includes(id))) throw new Error('Revoked');
            if(socket.readyState!==WebSocket.OPEN) return;
            c.claims=claims;c.checkedAt=Date.now();
            const upstream=config.createUpstream ? config.createUpstream() :
              new WebSocket(config.upstream,'mqtt',{maxPayload:8192,perMessageDeflate:false});
            c.upstream=upstream;
            upstream.on('error',c.close);upstream.on('close',c.close);
            upstream.on('open',()=>{
              const clientId='mayap-gw-'+createHash('sha256').update(claims.sid+':'+claims.cid).digest('hex').slice(0,24);
              send(upstream,{...packet,clientId,username:config.username,password:Buffer.from(config.password),keepalive:30});
            });
            const incoming=mqttPacket.parser({protocolVersion:4});let upstreamPending=0;
            incoming.on('error',c.close);
            upstream.on('message',data=>{upstreamPending+=data.length;if(upstreamPending>8192) c.close();else incoming.parse(data);});
            incoming.on('packet',p=>{
              upstreamPending=0;
              if(p.cmd==='connack') {c.ready=true;c.busy=false;clearTimeout(authDeadline);}
              if(p.cmd==='publish' && !allowed(p.topic,c.claims)) {c.close();return;}
              send(socket,p);
            });
          } catch {c.close();}
        })();
        return;
      }
      if(!c.ready || c.busy || Date.now()>=c.claims.exp*1000) {c.close();return;}
      if(packet.cmd==='publish' && packet.topic==='mayap/auth/renew') {
        if(packet.retain || packet.qos!==1 || packet.payload.length>7000) {c.close();return;}
        c.busy=true;
        (async()=>{
          try {
            const next=await verifyTicket(packet.payload.toString(),config);
            if(next.sid!==c.claims.sid || next.sub!==c.claims.sub || next.cid!==c.claims.cid) throw new Error('Renew identity mismatch');
            // Worker signed a fresh ownership list. Periodic introspection still bounds revocation.
            c.claims=next;c.busy=false;send(socket,{cmd:'puback',messageId:packet.messageId});
            queuedBytes=0;
            while(queued.length && !c.busy) handle(queued.shift());
          } catch {c.close();}
        })();return;
      }
      if(!clientPacketAllowed(packet,c.claims)) {c.close();return;}
      send(c.upstream,packet);
    };
    parser.on('packet',handle);
  });
  return {server,clients,refresh,close:()=>{clearInterval(timer);for(const c of clients)c.close();return new Promise(resolve=>server.close(resolve));}};
}
if(process.argv[1] && import.meta.url===pathToFileURL(process.argv[1]).href) {
  createGateway({port:Number(process.env.PORT || 8080),host:process.env.HOST || '127.0.0.1',
    issuer:process.env.APP_ORIGIN,publicKey:process.env.MQTT_TICKET_PUBLIC_KEY?.replace(/\\n/g,'\n'),
    checkSecret:process.env.MQTT_GATEWAY_CHECK_SECRET,upstream:process.env.HIVEMQ_WSS_URL,
    username:process.env.HIVEMQ_GATEWAY_USERNAME,password:process.env.HIVEMQ_GATEWAY_PASSWORD});
}
