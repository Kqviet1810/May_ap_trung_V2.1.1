(() => {
  'use strict';
  let account=null, csrf='', pending=null, lastCheck=0;
  const nativeFetch=window.fetch.bind(window);
  const channel=typeof BroadcastChannel==='function' ? new BroadcastChannel('mayap-account') : null;
  const api=async(path,init={})=>{
    const headers=new Headers(init.headers);
    if(init.method && init.method!=='GET') headers.set('X-Mayap-CSRF',csrf);
    const response=await nativeFetch(path,{...init,headers,credentials:'same-origin',cache:'no-store'});
    if(response.status===401 && account) expire();
    return response;
  };
  window.fetch=async(input,init)=>{
    const url=new URL(input instanceof Request ? input.url : String(input),location.href);
    if(url.origin!==location.origin || !url.pathname.startsWith('/api/')) return nativeFetch(input,init);
    const next={...init};
    if(typeof next.body==='string') {
      try {const data=JSON.parse(next.body);delete data.pairing_token;delete data.browser_sessions;next.body=JSON.stringify(data);} catch(_){}
    }
    return api(input,next);
  };
  function expire() {
    if(account) {
      const prefix=`mayap.account.${account.user.sub}.`;
      for(let i=localStorage.length-1;i>=0;i--) {
        const key=localStorage.key(i);if(key?.startsWith(prefix))localStorage.removeItem(key);
      }
    }
    csrf='';account=null;localStorage.removeItem('mayap.web.v10.mqtt.private');
    localStorage.removeItem('mayap.push.v1');
    window.dispatchEvent(new Event('mayap-logout'));
    document.documentElement.dataset.auth='guest';
    // Drop device DOM/RAM and pending command keys before another user signs in.
    location.replace('/');
  }
  async function refresh() {
    if(pending)return pending;
    pending=(async()=>{
      try {
        const response=await api('/api/account/session');
        if(response.status===401) {document.documentElement.dataset.auth='guest';return null;}
        if(!response.ok)throw new Error('Phiên đăng nhập chưa kiểm tra được.');
        const data=await response.json();
        if(!data.success || !data.user?.sub || !Array.isArray(data.devices))throw new Error('Phản hồi đăng nhập không hợp lệ.');
        if(account && account.user.sub!==data.user.sub){expire();return null;}
        account=data;csrf=data.csrf;lastCheck=Date.now();
        const label=document.getElementById('accountName');if(label)label.textContent=data.user.name || data.user.email || 'Tài khoản MAYAP';
        // App renders validated account cache before revealing the dashboard.
        window.dispatchEvent(new CustomEvent('mayap-account-devices',{detail:data.devices}));
        return data;
      } catch(_) {
        const message=document.getElementById('authMessage');
        if(message)message.textContent='Chưa kiểm tra được phiên đăng nhập. Hãy thử lại khi có mạng.';
        document.getElementById('authRetry')?.removeAttribute('hidden');
        return account;
      } finally {pending=null;}
    })();return pending;
  }
  async function logout() {
    try {
      const res=await api('/api/account/logout',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});
      if(!res.ok)throw new Error('Không đăng xuất được.');
      channel?.postMessage('logout');expire();
    } catch(_) {document.getElementById('accountNotice').textContent='Chưa đăng xuất được. Kiểm tra kết nối rồi thử lại.';}
  }
  channel?.addEventListener('message',event=>{if(event.data==='logout')expire();});
  window.MayapAccount={ready:null,refresh,api,get current(){return account;}};
  document.getElementById('logoutBtn')?.addEventListener('click',logout);
  document.getElementById('authRetry')?.addEventListener('click',async()=>{const data=await refresh();if(data)location.reload();});
  document.getElementById('revokeOtherSessions')?.addEventListener('click',async()=>{
    const res=await api('/api/account/sessions'), data=await res.json();
    if(!res.ok)return;
    // Selecting a session explicitly avoids accidentally revoking this page.
    const root=document.getElementById('accountSessions');root.replaceChildren();
    for(const session of data.sessions){
      const button=document.createElement('button');button.className='ghost';
      button.textContent=`Thu hồi: ${session.user_agent.slice(0,55)} · ${new Date(session.created_at).toLocaleDateString('vi-VN')}`;
      button.onclick=async()=>{await api('/api/account/sessions/revoke',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({session_id:session.id})});await refresh();button.remove();};
      root.append(button);
    }
  });
  if(new URLSearchParams(location.search).has('login_error'))document.getElementById('loginError').textContent='Đăng nhập chưa hoàn tất. Vui lòng thử lại.';
  setInterval(()=>{if(account && !document.hidden && Date.now()-lastCheck>=300000)refresh();},300000);
  document.addEventListener('visibilitychange',()=>{if(!document.hidden && Date.now()-lastCheck>=300000)refresh();});
  window.MayapAccount.ready=refresh();
})();
