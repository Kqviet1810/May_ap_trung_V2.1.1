import { jwtVerify, importSPKI } from 'jose';

const reads=new Set(['presence','bootstrap','snapshot','ack','config/reported','reminders/reported','history/reported','log']);
const writes=new Set(['session','command','config/set','reminders/set','history/request']);
const match=/^mayap\/v1\/(MAP-[A-F0-9]{12})\/(.+)$/;
export function allowed(topic,claims,write=false) {
  if(typeof topic!=='string') return false;
  const m=topic.match(match);
  return !!m && (write ? writes : reads).has(m[2]) && (write ? claims.write : claims.read).includes(m[1]);
}
export function clientPacketAllowed(packet,claims) {
  if(packet.cmd==='subscribe') return packet.subscriptions.length>0 && packet.subscriptions.length<=32 &&
    packet.subscriptions.every(s=>s.qos<=1 && allowed(s.topic,claims));
  if(packet.cmd==='unsubscribe') return packet.unsubscriptions.length<=32 && packet.unsubscriptions.every(t=>allowed(t,claims));
  if(packet.cmd==='publish') {
    const sessionMatch=packet.topic.match(/^mayap\/v1\/(MAP-[A-F0-9]{12})\/session$/);
    if(packet.retain || packet.qos>1 || packet.payload.length>=1536 ||
       !(sessionMatch ? claims.read.includes(sessionMatch[1]) : allowed(packet.topic,claims,true))) return false;
    if(packet.topic.endsWith('/session')) {
      try { const data=JSON.parse(packet.payload.toString()); return data.clientId===claims.cid &&
        typeof data.active==='boolean' && Number.isFinite(data.ttlMs) && data.ttlMs>0 && data.ttlMs<=60000; } catch {return false;}
    }
    try {return JSON.parse(packet.payload.toString()).v===2;} catch {return false;}
  }
  return ['pingreq','disconnect','puback'].includes(packet.cmd);
}
export async function verifyTicket(ticket,config,keyOverride) {
  const key=keyOverride || await importSPKI(config.publicKey,'EdDSA');
  const {payload}=await jwtVerify(ticket,key,{algorithms:['EdDSA'],issuer:config.issuer,audience:'mayap-mqtt-gateway',
    requiredClaims:['sub','sid','cid','iat','exp'], maxTokenAge:'15m',clockTolerance:0});
  if(!Array.isArray(payload.read) || !Array.isArray(payload.write) || payload.read.length>100 ||
    !payload.read.every(id=>/^MAP-[A-F0-9]{12}$/.test(id)) || !payload.write.every(id=>payload.read.includes(id)) ||
    !/^[A-Za-z0-9_-]{8,40}$/.test(payload.cid) || typeof payload.sid!=='string' || typeof payload.sub!=='string' ||
    payload.exp-payload.iat>900) throw new Error('Invalid ticket');
  return payload;
}
