const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require(process.env.MAYAP_PLAYWRIGHT || 'playwright');
const out=path.resolve(process.argv[2] || '../../outputs/account-browser');fs.mkdirSync(out,{recursive:true});
async function main(){
  const browser=await chromium.launch({executablePath:process.env.MAYAP_CHROME || 'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
  const results=[];
  try {
    for(const width of [390,768,1440]){
      const context=await browser.newContext({viewport:{width,height:900},serviceWorkers:'block'}),page=await context.newPage(),errors=[];
      page.on('pageerror',e=>errors.push(e.message));
      await context.route('**/api/**',route=>route.fulfill({status:401,contentType:'application/json',body:'{"success":false,"error":"ACCOUNT_LOGIN_REQUIRED"}'}));
      await page.goto('http://127.0.0.1:8765',{waitUntil:'networkidle'});
      assert.equal(await page.locator('.appShell').isVisible(),false);
      assert.equal(await page.locator('.landing').isVisible(),true);
      assert.equal(await page.locator('.googleLogin').count(),2);
      assert.equal(await page.locator('.googleLogin').first().getAttribute('href'),'/auth/google/start');
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
      await page.screenshot({path:path.join(out,`landing-${width}.png`),fullPage:true});
      assert.deepEqual(errors,[]);results.push({width,guestHome:true,noDashboard:true,noOverflow:true,errors});await context.close();
    }
    const context=await browser.newContext({viewport:{width:390,height:844},serviceWorkers:'block'}),page=await context.newPage();
    let release,loggedOut=false;const gate=new Promise(r=>{release=r;});
    await context.route('**/api/**',async route=>{
      const pathname=new URL(route.request().url()).pathname;
      if(pathname==='/api/account/session') {
        await gate;
        return route.fulfill({status:loggedOut?401:200,contentType:'application/json',body:JSON.stringify(loggedOut?{success:false}:{
          success:true,user:{sub:'returning-sub',name:'Khách quay lại'},csrf:'test-csrf',devices:[],expiresAt:Date.now()+86400000})});
      }
      if(pathname==='/api/account/logout'){assert.equal(route.request().headers()['x-mayap-csrf'],'test-csrf');loggedOut=true;}
      return route.fulfill({status:200,contentType:'application/json',body:'{"success":true}'});
    });
    await page.addInitScript(()=>{
      window.authStates=[];new MutationObserver(()=>window.authStates.push(document.documentElement?.dataset.auth))
        .observe(document,{subtree:true,attributes:true,attributeFilter:['data-auth']});
    });
    await page.goto('http://127.0.0.1:8765',{waitUntil:'domcontentloaded'});
    assert.equal(await page.locator('.landing').isVisible(),false);assert.equal(await page.locator('.appShell').isVisible(),false);
    release();await page.waitForFunction(()=>document.documentElement.dataset.auth==='ready');
    assert.ok(!(await page.evaluate(()=>window.authStates)).includes('guest'));
    await page.locator('[data-page="settings"]').click();await page.locator('#logoutBtn').click();
    await page.waitForFunction(()=>document.documentElement.dataset.auth==='guest');
    assert.equal(await page.locator('.landing').isVisible(),true);assert.equal(await page.locator('.appShell').isVisible(),false);
    results.push({returningLoginNoLandingFlash:true,logoutReturnsHome:true,csrfHeader:true});await context.close();
    fs.writeFileSync(path.join(out,'account-browser-qa.json'),JSON.stringify({passed:true,results},null,2));
    console.log(JSON.stringify(results));
  }finally{await browser.close();}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
