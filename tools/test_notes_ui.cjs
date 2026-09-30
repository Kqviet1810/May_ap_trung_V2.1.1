// Isolated UI QA; the shared fixture mocks all machine/network traffic.
// Start a local server on 127.0.0.1:8765, then run with MAYAP_PLAYWRIGHT set.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require(process.env.MAYAP_PLAYWRIGHT || 'playwright');
const { setup } = require('./test_web_experience.cjs');
const out = path.resolve(process.argv[2] || 'work/notes-qa');
fs.mkdirSync(out, { recursive: true });
const $ = (page, id) => page.locator(`#${id}`);
async function settled(page) {
  await page.waitForFunction(() => !document.getElementById('notesFab').getAnimations().some(a => a.playState === 'running'));
}
async function open(page) {
  await $(page, 'notesFab').click();
  await $(page, 'notesNew').waitFor({ state: 'visible' });
  await page.waitForFunction(() => !document.getElementById('notesNew').disabled);
}
async function add(page, title, body, kind = 'batch') {
  await $(page, 'notesNew').click();
  await $(page, 'notesKind').selectOption(kind);
  await $(page, 'notesTitle').fill(title); await $(page, 'notesBody').fill(body);
  await $(page, 'notesSave').click();
  await $(page, 'notesEditor').waitFor({ state: 'hidden' });
  await page.waitForFunction(() => !document.getElementById('notesNew').disabled);
}
async function inspect(page) {
  return page.evaluate(() => {
    const box = id => { const r = document.getElementById(id).getBoundingClientRect(); return { x:r.x,y:r.y,width:r.width,height:r.height,bottom:r.bottom,right:r.right }; };
    const v = visualViewport, fab=box('notesFab'), surface=box('notesSurface'), save=box('notesSave');
    const nav = document.querySelector('.sidebar').getBoundingClientRect();
    const list=document.getElementById('notesList'), fields=document.querySelector('.notes-editor-fields');
    return { width:innerWidth,height:innerHeight,viewport:{x:v.offsetLeft,y:v.offsetTop,width:v.width,height:v.height},
      fab,surface,save,nav:{x:nav.x,y:nav.y,width:nav.width,height:nav.height},
      overflow:document.documentElement.scrollWidth>innerWidth,
      listScroll:list.scrollHeight>list.clientHeight,fieldsScroll:fields.scrollHeight>fields.clientHeight,
      active:document.activeElement.id };
  });
}
function within(box, r) {
  assert.ok(box.x >= r.viewport.x - 1 && box.y >= r.viewport.y - 1, `top/left in viewport ${JSON.stringify(r)}`);
  assert.ok(box.right <= r.viewport.x+r.viewport.width+1 && box.bottom <= r.viewport.y+r.viewport.height+1, `bottom/right in viewport ${JSON.stringify(r)}`);
}
async function drag(page, touch) {
  const box = await $(page, 'notesFab').boundingBox(), x=box.x+28,y=box.y+28;
  const target = { x:page.viewportSize().width < 800 ? 35 : 300, y:160 };
  if (touch) {
    const cdp=await page.context().newCDPSession(page);
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x,y}]});
    for(let i=1;i<=6;i++) await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:x+(target.x-x)*i/6,y:y+(target.y-y)*i/6}]});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]}); await cdp.detach();
  } else { await page.mouse.move(x,y);await page.mouse.down();await page.mouse.move(target.x,target.y,{steps:8});await page.mouse.up(); }
  await settled(page);
  assert.equal(await $(page,'notesFab').getAttribute('aria-expanded'),'false','drag must not open Notes');
  const saved = await page.evaluate(()=>JSON.parse(localStorage.getItem('mayap.notes.position.v1')));
  assert.equal(saved.side,'left'); assert.ok(saved.ratio>=0 && saved.ratio<=1);
  const before=await $(page,'notesFab').boundingBox();
  await page.reload({waitUntil:'networkidle'});await settled(page);
  const after=await $(page,'notesFab').boundingBox();
  assert.ok(Math.abs(after.x-before.x)<1 && Math.abs(after.y-before.y)<1,'saved position restored');
}
async function main() {
  const browser = await chromium.launch({ headless:true, executablePath:process.env.MAYAP_CHROME || 'C:/Program Files/Google/Chrome/Application/chrome.exe' });
  const results=[];
  try {
    for(const [width,height,theme='light'] of [[1920,1080],[1366,768],[768,1024],[390,844],[390,420],[844,390],[320,568],[1366,768,'dark'],[390,844,'dark']]) {
      const {context,page,errors}=await setup(browser,{width,height,mobile:width<=800||height<500,theme});
      await settled(page);await open(page);
      let r=await inspect(page);within(r.surface,r);within(r.fab,r);assert.equal(r.overflow,false);
      await $(page,'notesNew').click();await $(page,'notesBody').fill('Ghi chú tiếng Việt '.repeat(16));
      r=await inspect(page);within(r.surface,r);within(r.save,r);assert.ok(r.save.height>=44);
      await page.screenshot({path:path.join(out,`notes-editor-${width}x${height}-${theme}.png`)});
      await $(page,'notesCancel').click();await $(page,'confirmAccept').click();
      await $(page,'notesClose').click();
      assert.equal(await page.evaluate(()=>document.activeElement.id),'notesFab');
      await drag(page,width<=800);
      await open(page);
      for(let i=0;i<6;i++) await add(page,`Nhật ký ${i}`,`Soi trứng ngày ${i+1}: phôi phát triển tốt. Đã kiểm tra nước và vệ sinh máy.`,i%2?'machine':'batch');
      assert.equal(await page.locator('#notesList .notes-card').count(),4);
      assert.equal(await $(page,'notesBadge').textContent(),'6');
      await page.screenshot({path:path.join(out,`notes-quick-${width}x${height}-${theme}.png`)});
      await $(page,'notesViewAll').click();
      assert.equal(await page.locator('#notesList .notes-card').count(),6);
      r=await inspect(page);within(r.surface,r);
      await $(page,'notesSearch').fill('soi trung');assert.equal(await page.locator('#notesList .notes-card').count(),6);
      await page.locator('[data-notes-filter="machine"]').click();assert.equal(await page.locator('#notesList .notes-card').count(),3);
      await $(page,'notesSearch').fill('không tồn tại');assert.equal(await page.locator('#notesList .notes-card').count(),0);
      await $(page,'notesSearch').fill('');await page.locator('[data-notes-filter="all"]').click();
      await page.screenshot({path:path.join(out,`notes-all-${width}x${height}-${theme}.png`)});
      await $(page,'notesClose').click();assert.equal(await page.evaluate(()=>document.activeElement.id),'notesFab');
      assert.deepEqual(errors,[]);results.push({width,height,theme,responsive:r,drag:width<=800?'touch':'mouse',passed:true});
      await context.close();
    }
    const {context,page,errors}=await setup(browser,{width:390,height:844,mobile:true});
    await settled(page);await open(page);
    const literal=`<img src=x onerror="window.__xss=1"> < > " ' &`, long='Trứng phát triển khỏe; kiểm tra độ ẩm và bổ sung nước. '.repeat(6).slice(0,300);
    await add(page,literal,long);
    assert.equal(await page.locator('#notesList .notes-card h3').textContent(),literal);
    assert.equal(await page.locator('#notesList img, #notesList script').count(),0);
    assert.equal(await page.evaluate(()=>window.__xss),undefined);
    const originalId=await page.locator('#notesList .notes-card').getAttribute('data-note-id');
    await page.locator('[aria-label="Sửa ghi chú"]').click();
    await $(page,'notesBody').fill('Đã sửa nội dung.');
    await page.keyboard.press('Escape');await $(page,'confirmDialog').waitFor({state:'visible'});
    await page.keyboard.press('Escape');assert.equal(await $(page,'notesBody').inputValue(),'Đã sửa nội dung.');
    await page.mouse.click(15,20);assert.equal(await $(page,'notesEditor').isVisible(),true,'outside click retains dirty draft');
    // Simulate the smaller visual area when a mobile keyboard is visible.
    await page.setViewportSize({width:390,height:420});await $(page,'notesBody').focus();
    const keyboard=await inspect(page);within(keyboard.save,keyboard);assert.ok(keyboard.fieldsScroll);
    await $(page,'notesSave').click();await $(page,'notesEditor').waitFor({state:'hidden'});
    assert.equal(await page.locator('#notesList .notes-card').count(),1);assert.equal(await page.locator('#notesList .notes-card p').textContent(),'Đã sửa nội dung.');
    assert.equal(await page.locator('#notesList .notes-card').getAttribute('data-note-id'),originalId);
    await page.setViewportSize({width:390,height:844});
    await page.locator('[aria-label="Xóa ghi chú"]').click();await $(page,'confirmCancel').click();
    assert.equal(await page.locator('#notesList .notes-card').count(),1);
    await page.locator('[aria-label="Xóa ghi chú"]').click();await $(page,'confirmAccept').click();
    await page.waitForFunction(()=>document.getElementById('notesBadge').hidden);
    // Delayed/rejected adapter responses: static loading, retry, then success.
    await $(page,'notesClose').click();
    await page.evaluate(()=>{
      const proto=MayapNotes.MemoryNotesStore.prototype, original=proto.listNotes;
      window.__notesMode='wait';proto.listNotes=async function(ctx){
        if(window.__notesMode==='wait') await new Promise(resolve=>window.__releaseNotes=resolve);
        if(window.__notesMode==='error') throw new Error('QA load failed');return original.call(this,ctx);
      };
    });
    await $(page,'notesFab').click();await page.locator('.notes-loading').waitFor({state:'visible'});
    await page.evaluate(()=>{window.__notesMode='error';window.__releaseNotes();});
    await page.getByRole('button',{name:'Thử lại',exact:true}).waitFor({state:'visible'});
    await page.evaluate(()=>window.__notesMode='ok');await page.getByRole('button',{name:'Thử lại',exact:true}).click();
    await page.getByText('Chưa có ghi chú',{exact:true}).waitFor({state:'visible'});
    // Batch association reads confirmed runtime; ending the batch blocks a new batch note.
    await $(page,'notesNew').click();assert.equal(await $(page,'notesKind').inputValue(),'batch');await $(page,'notesBody').fill('Mẻ vừa dừng.');
    await page.evaluate(()=>{window.__deliver=()=>{};const d=window.__qa.state.devices[0];d.snapshot.runtime.batchRunning=false;window.__qa.renderDevice();});
    assert.equal(await $(page,'notesKind').evaluate(el=>el.options[0].disabled),true);
    await $(page,'notesSave').click();assert.equal(await $(page,'notesEditorError').isVisible(),true);
    await $(page,'notesKind').selectOption('machine');await $(page,'notesSave').click();await $(page,'notesEditor').waitFor({state:'hidden'});
    await $(page,'notesNew').click();assert.equal(await $(page,'notesKind').inputValue(),'machine');
    assert.equal(await $(page,'notesKind').evaluate(el=>el.options[0].disabled),true);await $(page,'notesCancel').click();
    // Changing the selected device while editing must not misattribute a note.
    await $(page,'notesNew').click();await $(page,'notesBody').fill('Bản nháp của máy A.');
    await page.evaluate(()=>{
      const h=window.__qa;h.state.devices.push(h.createDevice({id:'MAP-111111111111',name:'Máy B',pairingToken:'qa-token'}));
      h.state.selectedId='MAP-111111111111';h.renderDevice();
    });
    await $(page,'notesSave').click();assert.match(await $(page,'notesEditorError').textContent(),/Máy đang chọn đã thay đổi/);
    await $(page,'notesCancel').click();await $(page,'confirmAccept').click();
    await page.getByText('Chưa có ghi chú',{exact:true}).waitFor({state:'visible'});
    await page.evaluate(()=>{window.__qa.state.selectedId='MAP-1234567890AB';window.__qa.renderDevice();});
    await page.waitForFunction(()=>document.querySelectorAll('#notesList .notes-card').length===1);
    // The larger native dialog also preserves dirty edits through nested confirmation.
    for(let i=0;i<4;i++) await add(page,`Bảo trì ${i}`,'Đã kiểm tra máy.','machine');
    await $(page,'notesViewAll').click();await page.locator('[aria-label="Sửa ghi chú"]').first().click();
    await $(page,'notesBody').fill('Chưa lưu trong xem tất cả.');await page.keyboard.press('Escape');
    await $(page,'confirmDialog').waitFor({state:'visible'});await $(page,'confirmCancel').click();
    assert.equal(await $(page,'notesBody').inputValue(),'Chưa lưu trong xem tất cả.');
    await $(page,'notesSave').click();await $(page,'notesEditor').waitFor({state:'hidden'});
    await $(page,'notesClose').click();await page.reload({waitUntil:'networkidle'});await open(page);
    assert.equal(await page.locator('#notesList .notes-card').count(),0,'memory notes are not persisted');
    assert.deepEqual(errors,[]);results.push({interactions:'XSS, Vietnamese 300 chars, edit/delete, dirty confirmation, outside click, keyboard area, adapter loading/error/retry, stopped batch, non-persistence',keyboard,passed:true});
    await context.close();
    fs.writeFileSync(path.join(out,'notes-browser-qa.json'),JSON.stringify({passed:true,results},null,2));
    console.log(`Notes browser QA passed: ${results.length} scenarios.`);
  } finally { await browser.close(); }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
