'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { MemoryNotesStore, insertDictation } = require('../notes.js');
const owner = { deviceId: 'A', batchRunning: true, batchLabel: 'Mẻ tháng 9' };
const input = { kind: 'batch', title: 'Soi trứng', body: 'Ngày 7: phát triển tốt.' };
function store() {
  let id = 0, clock = 1000;
  return new MemoryNotesStore({ makeId: () => String(++id), now: () => ++clock });
}
test('notes are isolated by device, newest first, and returned as copies', async () => {
  const service = store();
  const first = await service.createNote(input, owner);
  first.body = 'modified externally';
  const last = await service.createNote({ ...input, kind: 'machine' }, owner);
  await service.createNote(input, { ...owner, deviceId: 'B' });
  const listed = await service.listNotes(owner);
  assert.deepEqual(listed.map(n => n.id), [last.id, first.id]);
  assert.equal(listed[1].body, input.body);
  listed[0].body = 'another mutation';
  assert.equal((await service.listNotes(owner))[0].body, input.body);
});
test('edit preserves ID and creation time and historical batch label', async () => {
  const service = store(), first = await service.createNote(input, owner);
  const edited = await service.updateNote(first.id, { ...input, body: 'Đã vệ sinh.' },
    { ...owner, batchRunning: false, batchLabel: 'Mẻ khác' });
  assert.equal(edited.id, first.id); assert.equal(edited.createdAt, first.createdAt);
  assert.equal(edited.batchLabel, owner.batchLabel); assert.ok(edited.updatedAt > edited.createdAt);
  await assert.rejects(service.updateNote(first.id, input, { ...owner, deviceId: 'B' }));
  await assert.rejects(service.deleteNote(first.id, { deviceId: 'B' }));
  await service.deleteNote(first.id, owner);
  assert.deepEqual(await service.listNotes(owner), []);
});
test('validates blank/length/type and requires a running batch for new association', async () => {
  const service = store(), stopped = { ...owner, batchRunning: false };
  for (const invalid of [{ body: ' \n\t ' }, { title: 'x'.repeat(61) }, { body: 'x'.repeat(301) }, { kind: 'bad' }])
    await assert.rejects(service.createNote({ ...input, ...invalid }, owner));
  await assert.rejects(service.createNote(input, stopped));
  const machine = await service.createNote({ kind: 'machine', title: '  Bảo trì  ', body: '  < > " \' &  ' }, stopped);
  assert.equal(machine.title, 'Bảo trì'); assert.equal(machine.body, '< > " \' &');
  await assert.rejects(service.updateNote(machine.id, input, stopped));
  const batch = await service.updateNote(machine.id, input, owner);
  assert.equal(batch.batchLabel, owner.batchLabel);
  const reverted = await service.updateNote(machine.id, { ...input, kind: 'machine' }, stopped);
  assert.equal(reverted.batchLabel, '');
});
test('dictation inserts at cursor, replaces selection, preserves typing and stays inside 300 characters', () => {
  assert.deepEqual(insertDictation('Đã soi', 6, 6, '  trứng\nngày 7  '),
    { value: 'Đã soi trứng ngày 7', caret: 'Đã soi trứng ngày 7'.length, truncated: false });
  assert.equal(insertDictation('Đã soi sai hôm nay', 7, 10, 'trứng').value, 'Đã soi trứng hôm nay');
  const limited = insertDictation('a'.repeat(295), 295, 295, 'kiểm tra nước');
  assert.equal(limited.value.length, 300); assert.equal(limited.truncated, true);
  const emoji = insertDictation('a'.repeat(298), 298, 298, '🐣');
  assert.equal(emoji.value, 'a'.repeat(298) + ' '); assert.equal(emoji.truncated, true);
  assert.equal(insertDictation('Giữ nguyên', 3, 8, ' \n ').value, 'Giữ nguyên');
});
