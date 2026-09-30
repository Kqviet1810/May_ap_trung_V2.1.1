// Speech event simulation only: no real microphone/audio or recognition service.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require(process.env.MAYAP_PLAYWRIGHT || 'playwright');
const { setup } = require('./test_web_experience.cjs');
const out = path.resolve(process.argv[2] || 'work/notes-qa');
fs.mkdirSync(out,{recursive:true});
const $=(p,id)=>p.locator(`#${id}`);
function mockSpeech() {
  window.__speech={instances:[],starts:0,aborts:0,stops:0};
  window.SpeechRecognition=class {
    constructor(){window.__speech.instances.push(this);}
    start(){window.__speech.starts++;this.onstart?.();}
    stop(){window.__speech.stops++;this.onend?.();}
    abort(){window.__speech.aborts++;this.onerror?.({error:'aborted'});this.onend?.();}
    results(entries,index=0){this.onresult?.({resultIndex:index,results:entries.map(([transcript,isFinal])=>({0:{transcript},isFinal}))});}
  };
}
async function main(){
  const browser=await chromium.launch({headless:true,executablePath:process.env.MAYAP_CHROME||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
  try {
    const {context,page,errors}=await setup(browser,{width:390,height:844,mobile:true,initScript:mockSpeech});
    await $(page,'notesFab').click();await $(page,'notesNew').click();
    assert.equal(await page.evaluate(()=>window.__speech.starts),0,'microphone never starts automatically');
    await $(page,'notesBody').fill('Đã soi');await $(page,'notesBody').evaluate(el=>el.setSelectionRange(el.value.length,el.value.length));
    await $(page,'notesVoice').click();
    assert.equal(await $(page,'notesVoice').getAttribute('aria-pressed'),'true');
    assert.equal(await $(page,'notesSave').isDisabled(),true,'save waits for final speech results');
    assert.equal(await page.evaluate(()=>window.__speech.instances[0].lang),'vi-VN');
    await page.evaluate(()=>window.__speech.instances[0].results([['trứng ngày bảy',false]]));
    assert.equal(await $(page,'notesBody').inputValue(),'Đã soi','interim text does not alter the draft');
    await page.evaluate(()=>window.__speech.instances[0].results([['trứng ngày bảy',true]]));
    assert.equal(await $(page,'notesBody').inputValue(),'Đã soi trứng ngày bảy');
    await page.evaluate(()=>window.__speech.instances[0].results([['trứng ngày bảy',true]]));
    assert.equal(await $(page,'notesBody').inputValue(),'Đã soi trứng ngày bảy','same final result is not duplicated');
    await page.evaluate(()=>window.__speech.instances[0].results([['trứng ngày bảy',true],['phôi phát triển tốt',true]],1));
    assert.equal(await $(page,'notesBody').inputValue(),'Đã soi trứng ngày bảy phôi phát triển tốt');
    await page.screenshot({path:path.join(out,'notes-voice-listening.png')});
    await $(page,'notesVoice').click();assert.equal(await $(page,'notesVoice').getAttribute('aria-pressed'),'false');
    assert.equal(await $(page,'notesSave').isEnabled(),true);await $(page,'notesSave').click();
    await $(page,'notesEditor').waitFor({state:'hidden'});
    assert.equal(await page.locator('#notesList .notes-card p').textContent(),'Đã soi trứng ngày bảy phôi phát triển tốt');
    await $(page,'notesNew').click();await $(page,'notesVoice').click();
    await page.evaluate(()=>window.__speech.instances.at(-1).onerror({error:'not-allowed'}));
    assert.match(await $(page,'notesVoiceStatus').textContent(),/Quyền micro bị từ chối/);
    assert.equal(await $(page,'notesSave').isEnabled(),true);
    await $(page,'notesVoice').click();
    await page.evaluate(()=>window.__speech.instances.at(-1).onerror({error:'network'}));
    assert.match(await $(page,'notesVoiceStatus').textContent(),/Internet/);
    await $(page,'notesBody').fill('a'.repeat(295));await $(page,'notesBody').evaluate(el=>el.setSelectionRange(295,295));
    await $(page,'notesVoice').click();await page.evaluate(()=>window.__speech.instances.at(-1).results([['kiểm tra nước và vệ sinh máy',true]]));
    assert.equal((await $(page,'notesBody').inputValue()).length,300);
    assert.equal(await $(page,'notesVoice').getAttribute('aria-pressed'),'false');assert.match(await $(page,'notesVoiceStatus').textContent(),/300/);
    await $(page,'notesBody').fill('Bản nháp');await $(page,'notesVoice').click();
    await $(page,'notesCancel').click();assert.ok(await page.evaluate(()=>window.__speech.aborts)>0);
    await $(page,'confirmCancel').click();assert.equal(await $(page,'notesVoice').getAttribute('aria-pressed'),'false');
    const before=await $(page,'notesBody').inputValue();
    await page.evaluate(()=>window.__speech.instances.at(-1).results([['kết quả muộn không được chèn',true]]));
    assert.equal(await $(page,'notesBody').inputValue(),before);
    await $(page,'notesVoice').click();
    await page.evaluate(()=>{const h=window.__qa;h.state.selectedId='';h.renderDevice();});
    assert.equal(await $(page,'notesVoice').getAttribute('aria-pressed'),'false','changing selected machine stops capture');
    assert.deepEqual(errors,[]);await context.close();
    const unsupported=await setup(browser,{width:1366,height:768,initScript:()=>{window.SpeechRecognition=undefined;window.webkitSpeechRecognition=undefined;}});
    await $(unsupported.page,'notesFab').click();await $(unsupported.page,'notesNew').click();
    assert.equal(await $(unsupported.page,'notesVoice').isDisabled(),true);
    assert.match(await $(unsupported.page,'notesVoiceStatus').textContent(),/chưa hỗ trợ/);
    await $(unsupported.page,'notesBody').fill('Nhập bằng bàn phím vẫn hoạt động.');await $(unsupported.page,'notesSave').click();
    await $(unsupported.page,'notesEditor').waitFor({state:'hidden'});assert.deepEqual(unsupported.errors,[]);await unsupported.context.close();
    fs.writeFileSync(path.join(out,'notes-voice-qa.json'),JSON.stringify({passed:true,method:'Mock SpeechRecognition events; no real audio captured',checks:['explicit start','vi-VN','interim/final','no duplicate finals','stop/save','permission/network errors','300-character limit','cancel/late results','device switch','unsupported browser keyboard fallback']},null,2));
    console.log('Voice Notes QA passed (simulated speech events).');
  } finally {await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
