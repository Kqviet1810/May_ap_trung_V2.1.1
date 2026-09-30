'use strict';
(function (global) {
  const POSITION_KEY = 'mayap.notes.position.v1';
  const RECENT_LIMIT = 4;
  const clamp = (value, min, max) => Math.max(min, Math.min(value, Math.max(min, max)));
  const text = (tag, value, className = '') => {
    const element = document.createElement(tag);
    element.textContent = value;
    if (className) element.className = className;
    return element;
  };
  const fold = value => String(value).normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/đ/g, 'd').replace(/Đ/g, 'D').toLowerCase();

  function validateInput(input) {
    const kind = input.kind;
    const title = String(input.title || '').trim();
    const body = String(input.body || '').trim();
    if (!['batch', 'machine'].includes(kind)) throw new Error('Hãy chọn loại ghi chú.');
    if (!body) throw new Error('Hãy nhập nội dung ghi chú.');
    if (String(input.title || '').length > 60 || String(input.body || '').length > 300)
      throw new Error('Tiêu đề tối đa 60 ký tự, nội dung tối đa 300 ký tự.');
    return { kind, title, body };
  }

  function insertDictation(value, start, end, transcript, limit = 300) {
    const words = String(transcript).replace(/\s+/g, ' ').trim();
    start = clamp(start, 0, value.length); end = clamp(end, start, value.length);
    if (!words) return { value, caret: end, truncated: false };
    const before = value.slice(0, start), after = value.slice(end);
    const phrase = `${before && !/\s$/.test(before) ? ' ' : ''}${words}${after && !/^\s/.test(after) ? ' ' : ''}`;
    const capacity = Math.max(0, limit - before.length - after.length);
    let added = phrase.slice(0, capacity);
    // Avoid cutting a surrogate pair at the character limit.
    if (/[\uD800-\uDBFF]$/.test(added)) added = added.slice(0, -1);
    return { value: before + added + after, caret: before.length + added.length, truncated: added.length < phrase.length };
  }

  // Replace this adapter to connect a future storage backend. No note contents
  // are written to browser storage, MQTT, NVS or EEPROM in this UI prototype.
  class MemoryNotesStore {
    constructor({ now = () => Date.now(), makeId = () => global.crypto.randomUUID() } = {}) {
      this.notes = new Map(); this.now = now; this.makeId = makeId;
    }
    async listNotes(context) {
      return [...this.notes.values()].filter(note => note.deviceId === context.deviceId)
        .sort((a, b) => b.createdAt - a.createdAt || b.id.localeCompare(a.id)).map(note => ({ ...note }));
    }
    async createNote(input, context) {
      const value = validateInput(input);
      if (value.kind === 'batch' && !context.batchRunning) throw new Error('Không có mẻ đang chạy để gắn ghi chú.');
      const note = { ...value, id: this.makeId(), deviceId: context.deviceId,
        batchLabel: value.kind === 'batch' ? context.batchLabel : '', createdAt: this.now(), updatedAt: null };
      this.notes.set(note.id, note); return { ...note };
    }
    async updateNote(id, input, context) {
      const original = this.notes.get(id);
      if (!original || original.deviceId !== context.deviceId) throw new Error('Không tìm thấy ghi chú của máy này.');
      const value = validateInput(input);
      if (value.kind === 'batch' && original.kind !== 'batch' && !context.batchRunning)
        throw new Error('Không có mẻ đang chạy để gắn ghi chú.');
      const note = { ...original, ...value, updatedAt: this.now(), batchLabel: value.kind === 'machine'
        ? '' : original.kind === 'batch' ? original.batchLabel : context.batchLabel };
      this.notes.set(id, note); return { ...note };
    }
    async deleteNote(id, context) {
      const note = this.notes.get(id);
      if (!note || note.deviceId !== context.deviceId) throw new Error('Không tìm thấy ghi chú của máy này.');
      this.notes.delete(id);
    }
  }

  function icon(name) {
    const paths = { edit: ['m16 3 5 5-12 12-6 1 1-6Z', 'm14 5 5 5'],
      delete: ['M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7M14 10v7'],
      note: ['M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9Z', 'M14 3v6h6M8 13h8M8 17h5'] };
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    for (const [key, value] of Object.entries({ viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor',
      'stroke-width': '2', 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'aria-hidden': 'true' })) svg.setAttribute(key, value);
    for (const d of paths[name]) {
      const path = document.createElementNS('http://www.w3.org/2000/svg', 'path'); path.setAttribute('d', d); svg.append(path);
    }
    return svg;
  }

  let active = null;
  function mount({ getContext, confirmAction, toast, service = new MemoryNotesStore() }) {
    if (active) return active;
    const $ = id => document.getElementById(id);
    const root = $('notesRoot'), fab = $('notesFab'), quick = $('notesQuick'), surface = $('notesSurface'), dialog = $('notesDialog');
    if (!root || !fab || !surface || !dialog) return null;
    const list = $('notesList'), form = $('notesEditor'), kind = $('notesKind'), title = $('notesTitle'), body = $('notesBody');
    const voiceButton = $('notesVoice'), voiceStatus = $('notesVoiceStatus');
    const Recognition = global.SpeechRecognition || global.webkitSpeechRecognition;
    const voiceSupported = Boolean(Recognition && global.isSecureContext);
    let voice = null, voiceStopping = false;
    const voiceDefault = voiceSupported ? 'Tiếng Việt · có thể cần Internet. Âm thanh do dịch vụ của trình duyệt xử lý.'
      : 'Trình duyệt này chưa hỗ trợ nhận giọng nói hoặc trang chưa dùng HTTPS. Bạn vẫn có thể nhập bằng bàn phím.';
    let notes = [], loaded = false, loadError = false, loading = false, request = 0;
    let open = false, full = false, editor = null, saving = false, confirming = false;
    let context = getContext(), contextKey = '', filter = 'all', query = '';
    const contextSignature = value => JSON.stringify([value.deviceId, value.deviceName, value.batchRunning, value.batchLabel]);
    let position = { side: 'right', ratio: 1 }, fabAt = { x: 0, y: 0 }, drag = null, frame = 0, suppressClickUntil = 0;
    let positionStorage = null, storageWarningShown = false;
    try {
      positionStorage = global.localStorage;
      const stored = JSON.parse(positionStorage.getItem(POSITION_KEY) || 'null');
      if (stored && ['left', 'right'].includes(stored.side) && Number.isFinite(stored.ratio))
        position = { side: stored.side, ratio: clamp(stored.ratio, 0, 1) };
    } catch (_) { /* Position storage is optional; the Notes service is separate. */ }

    function input() { return { kind: kind.value, title: title.value, body: body.value }; }
    function dirty() { return Boolean(editor && JSON.stringify(input()) !== editor.initial); }
    function editorError(message = '') {
      $('notesEditorError').textContent = message; $('notesEditorError').hidden = !message;
      $('notesEditorError').classList.toggle('show', Boolean(message));
    }
    function voiceUi(message) {
      voiceButton.disabled = !voiceSupported || saving || voiceStopping;
      voiceButton.setAttribute('aria-pressed', String(Boolean(voice)));
      $('notesVoiceLabel').textContent = voice ? 'Dừng micro' : 'Nhập bằng giọng nói';
      $('notesSave').disabled = saving || Boolean(voice);
      if (message !== undefined) voiceStatus.textContent = message;
    }
    function stopVoice(message = voiceDefault) {
      const previous = voice; voice = null; voiceStopping = false;
      if (previous) { try { previous.abort(); } catch (_) { /* Already ended. */ } }
      voiceUi(message);
    }
    function toggleVoice() {
      if (!editor || saving || confirming || !voiceSupported) return;
      if (voice) {
        voiceStopping = true; voiceUi('Đang hoàn tất nội dung vừa nói…');
        try { voice.stop(); } catch (_) { stopVoice('Đã dừng micro. Bạn có thể sửa nội dung trước khi lưu.'); }
        return;
      }
      if (body.value.length >= 300 && body.selectionStart === body.selectionEnd)
        return voiceUi('Nội dung đã đủ 300 ký tự. Hãy xóa bớt hoặc chọn đoạn cần thay trước khi nói.');
      try {
        const recognition = new Recognition(); voice = recognition;
        recognition.lang = 'vi-VN'; recognition.continuous = true; recognition.interimResults = true;
        recognition.maxAlternatives = 1;
        const finalIndexes = new Set();
        recognition.onstart = () => { if (voice === recognition) voiceUi('Đang nghe tiếng Việt… Bấm Dừng micro khi nói xong.'); };
        recognition.onresult = event => {
          if (voice !== recognition || !editor) return;
          syncContext(); if (voice !== recognition) return;
          let interim = '';
          for (let i = event.resultIndex; i < event.results.length; i++) {
            const result = event.results[i], transcript = result[0]?.transcript || '';
            if (!result.isFinal) { interim += transcript; continue; }
            if (finalIndexes.has(i)) continue; finalIndexes.add(i);
            const next = insertDictation(body.value, body.selectionStart, body.selectionEnd, transcript);
            body.value = next.value; body.setSelectionRange(next.caret, next.caret);
            body.dispatchEvent(new Event('input', { bubbles: true }));
            if (next.truncated || next.value.length >= 300) {
              stopVoice('Đã chạm giới hạn 300 ký tự; micro đã dừng. Hãy kiểm tra nội dung trước khi lưu.'); return;
            }
          }
          voiceUi(interim ? `Đang nghe: ${interim.slice(0, 180)}` : 'Đã nhận nội dung. Bạn có thể nói tiếp hoặc bấm Dừng micro.');
        };
        recognition.onerror = event => {
          if (voice !== recognition) return;
          const errors = {
            'not-allowed': 'Quyền micro bị từ chối. Hãy cho phép micro trong cài đặt trình duyệt rồi thử lại.',
            'service-not-allowed': 'Trình duyệt không cho phép dịch vụ nhận giọng nói. Bạn có thể nhập bằng bàn phím.',
            'audio-capture': 'Không tìm thấy micro hoạt động. Hãy kiểm tra micro rồi thử lại.',
            'no-speech': 'Chưa nghe được giọng nói. Bấm micro để thử lại.',
            network: 'Không kết nối được dịch vụ nhận giọng nói. Hãy kiểm tra Internet rồi thử lại.',
            'language-not-supported': 'Trình duyệt chưa hỗ trợ nhận giọng nói tiếng Việt.'
          };
          stopVoice(errors[event.error] || 'Nhận giọng nói đã dừng. Nội dung đã nhận vẫn được giữ để bạn sửa.');
        };
        recognition.onend = () => {
          if (voice === recognition) {
            voice = null; voiceStopping = false; voiceUi('Micro đã dừng. Hãy kiểm tra nội dung trước khi lưu.');
          }
        };
        voiceUi('Đang mở micro…'); recognition.start();
      } catch (_) { stopVoice('Không thể mở micro. Hãy kiểm tra quyền micro của trình duyệt rồi thử lại.'); }
    }
    function badge() {
      $('notesBadge').textContent = notes.length > 99 ? '99+' : String(notes.length);
      $('notesBadge').hidden = !notes.length;
      fab.setAttribute('aria-label', `Mở ghi chú${notes.length ? `, ${notes.length} ghi chú` : ''}`);
      $('notesCount').textContent = loaded ? `(${notes.length})` : '';
    }
    function header() {
      $('notesHeading').firstChild.textContent = full ? 'Tất cả ghi chú ' : 'Ghi chú ';
      $('notesOwner').textContent = (editor?.context || context).deviceName || 'Nhật ký của bạn';
      surface.setAttribute('aria-label', full ? 'Tất cả ghi chú' : 'Ghi chú');
      $('notesToolbar').hidden = Boolean(editor);
      $('notesSearchTools').hidden = !full || Boolean(editor);
      $('notesViewAll').hidden = Boolean(editor) || full || notes.length <= RECENT_LIMIT || loading || loadError;
      $('notesNew').disabled = loading || saving || loadError;
      badge();
    }
    function status(message, hint, action, callback) {
      const box = text('div', '', 'notes-empty');
      const mark = text('span', '', 'notes-mark'); mark.setAttribute('aria-hidden', 'true'); mark.append(icon('note'));
      box.append(mark, text('strong', message));
      if (hint) box.append(text('p', hint));
      if (action) {
        const button = text('button', action, 'ghost'); button.type = 'button'; button.addEventListener('click', callback); box.append(button);
      }
      return box;
    }
    function card(note) {
      const article = text('article', '', 'notes-card'); article.dataset.noteId = note.id;
      const top = text('div', '', 'notes-card-top'), meta = text('div', '');
      const date = new Date(note.createdAt), time = text('time',
        `${date.toLocaleDateString('vi-VN')} · ${date.toLocaleTimeString('vi-VN', { hour: '2-digit', minute: '2-digit' })}`);
      time.dateTime = date.toISOString();
      if (note.updatedAt) time.title = `Đã sửa ${new Date(note.updatedAt).toLocaleString('vi-VN')}`;
      const type = note.kind === 'batch' ? `Mẻ ấp${note.batchLabel ? ` · ${note.batchLabel}` : ''}` : 'Máy / bảo trì';
      meta.append(time, text('span', type, 'notes-kind-label'));
      const actions = text('div', '', 'notes-card-actions');
      for (const [name, label, action] of [['edit', 'Sửa ghi chú', () => showEditor(note)], ['delete', 'Xóa ghi chú', () => remove(note)]]) {
        const button = text('button', '', 'iconButton'); button.type = 'button'; button.title = label;
        button.setAttribute('aria-label', label); button.append(icon(name)); button.addEventListener('click', action); actions.append(button);
      }
      top.append(meta, actions); article.append(top);
      if (note.title) article.append(text('h3', note.title));
      article.append(text('p', note.body)); return article;
    }
    function renderList() {
      list.replaceChildren(); list.hidden = Boolean(editor); form.hidden = !editor;
      header();
      if (editor) return;
      if (loading) {
        const skeleton = text('div', '', 'notes-loading'); skeleton.setAttribute('role', 'status');
        skeleton.append(text('span', 'Đang tải ghi chú…', 'srOnly'));
        for (let i = 0; i < 2; i++) { const line = text('div', '', 'notes-skeleton'); line.setAttribute('aria-hidden', 'true'); skeleton.append(line); }
        list.append(skeleton);
      } else if (loadError) {
        list.append(status('Không thể tải ghi chú.', 'Hãy thử lại để tiếp tục.', 'Thử lại', reload));
      } else {
        const visible = notes.filter(note => (!full || filter === 'all' || note.kind === filter) &&
          (!full || !query || fold(`${note.title}\n${note.body}\n${note.batchLabel}`).includes(fold(query))));
        if (!notes.length) list.append(status('Chưa có ghi chú', 'Ghi lại việc đã làm và những điều cần theo dõi.', 'Tạo ghi chú đầu tiên', () => showEditor()));
        else if (!visible.length) list.append(status('Không tìm thấy ghi chú', 'Thử từ khóa khác hoặc đổi bộ lọc.'));
        else {
          const fragment = document.createDocumentFragment();
          for (const note of full ? visible : visible.slice(0, RECENT_LIMIT)) fragment.append(card(note));
          list.append(fragment);
        }
      }
      if (open) layout();
    }
    async function reload() {
      const ticket = ++request, owner = { ...context };
      loading = true; loadError = false; renderList();
      try {
        const records = await service.listNotes(owner);
        if (ticket !== request) return;
        notes = records; loaded = true;
      } catch (_) {
        if (ticket !== request) return;
        loadError = true; loaded = false; notes = [];
      } finally {
        if (ticket === request) { loading = false; renderList(); }
      }
    }
    function syncContext() {
      const next = getContext(), key = contextSignature(next);
      if (key === contextKey) return;
      const changedOwner = next.deviceId !== context.deviceId;
      contextKey = key; context = next;
      if (changedOwner) stopVoice('Máy đang chọn đã thay đổi; micro đã dừng.');
      if (editor) {
        kind.options[0].disabled = !context.batchRunning && editor.original?.kind !== 'batch';
        if (context.deviceId !== editor.context.deviceId)
          editorError('Máy đang chọn đã thay đổi. Hãy hủy hoặc quay lại máy trước khi lưu.');
        else if (kind.value === 'batch' && !context.batchRunning && !editor.original)
          editorError('Mẻ đã dừng. Hãy chọn Máy / bảo trì hoặc hủy ghi chú.');
        else editorError();
        header();
      }
      else header();
      if (changedOwner && !editor) { notes = []; loaded = false; reload(); }
    }
    function showEditor(note = null) {
      if (saving || loading || confirming) return;
      syncContext();
      stopVoice();
      editor = { original: note, context: { ...context }, initial: '' };
      kind.options[0].disabled = !context.batchRunning && note?.kind !== 'batch';
      kind.options[0].textContent = note?.kind === 'batch' && !context.batchRunning ? 'Mẻ ấp đã ghi' : 'Mẻ hiện tại';
      kind.value = note?.kind || (context.batchRunning ? 'batch' : 'machine');
      title.value = note?.title || ''; body.value = note?.body || '';
      editor.initial = JSON.stringify(input());
      $('notesEditorTitle').textContent = note ? 'Sửa ghi chú' : 'Ghi chú mới';
      $('notesSave').textContent = note ? 'Lưu thay đổi' : 'Lưu';
      $('notesCounter').textContent = `${body.value.length} / 300`;
      editorError(); renderList(); layout(); title.focus({ preventScroll: true });
    }
    async function discardAllowed() {
      if (saving || confirming) return false;
      stopVoice();
      if (!dirty()) return true;
      confirming = true;
      try { return await confirmAction({ title: 'Bỏ thay đổi chưa lưu?', message: 'Nội dung bạn vừa nhập sẽ không được lưu.', accept: 'Bỏ thay đổi', danger: true }); }
      finally { confirming = false; }
    }
    async function cancelEditor() {
      if (!await discardAllowed()) return;
      editor = null; syncContext(); await reload(); $('notesNew').focus({ preventScroll: true });
    }
    async function close() {
      if (!open || !await discardAllowed()) return;
      editor = null; open = false; full = false;
      if (dialog.open) dialog.close();
      quick.append(surface); quick.hidden = true;
      fab.setAttribute('aria-expanded', 'false'); fab.focus({ preventScroll: true });
    }
    function show() {
      if (document.querySelector('dialog[open]')) return;
      syncContext(); open = true; quick.hidden = false; fab.setAttribute('aria-expanded', 'true');
      renderList(); layout(); surface.focus({ preventScroll: true }); reload();
    }
    function showAll() {
      if (saving || confirming || editor) return;
      full = true; quick.hidden = true; dialog.append(surface); dialog.showModal();
      renderList(); layout(); $('notesSearch').focus({ preventScroll: true });
    }
    async function save(event) {
      event.preventDefault(); if (saving || !editor) return;
      if (voice) return voiceUi('Bấm Dừng micro để hoàn tất nhận giọng nói trước khi lưu.');
      syncContext(); editorError();
      if (context.deviceId !== editor.context.deviceId) return editorError('Máy đang chọn đã thay đổi. Hãy quay lại máy trước khi lưu.');
      try { validateInput(input()); } catch (error) { editorError(error.message); body.focus(); return; }
      saving = true; $('notesSave').disabled = $('notesCancel').disabled = true;
      try {
        const value = input(), owner = { ...context };
        if (editor.original) await service.updateNote(editor.original.id, value, owner);
        else await service.createNote(value, owner);
        editor = null; await reload(); toast('Đã lưu ghi chú trong phiên thử nghiệm');
        $('notesNew').focus({ preventScroll: true });
      } catch (error) { editorError(error.message || 'Không thể lưu ghi chú. Hãy thử lại.'); }
      finally {
        saving = false; $('notesSave').disabled = $('notesCancel').disabled = false; header();
        voiceUi();
        if (!editor) $('notesNew').focus({ preventScroll: true });
      }
    }
    async function remove(note) {
      if (confirming || saving || document.querySelector('dialog[open]:not(#notesDialog)')) return;
      confirming = true;
      try {
        const accepted = await confirmAction({ title: 'Xóa ghi chú này?', message: 'Hành động này không thể hoàn tác.', accept: 'Xóa ghi chú', danger: true });
        if (!accepted) return;
        await service.deleteNote(note.id, { deviceId: note.deviceId });
        await reload(); toast('Đã xóa ghi chú'); $('notesNew').focus({ preventScroll: true });
      } catch (_) { toast('Không thể xóa ghi chú. Hãy thử lại.'); }
      finally { confirming = false; }
    }

    function bounds() {
      const v = global.visualViewport;
      const x = v?.offsetLeft || 0, y = v?.offsetTop || 0, w = v?.width || global.innerWidth, h = v?.height || global.innerHeight;
      const safe = getComputedStyle(root);
      let left = x + 12, bottom = y + h - (parseFloat(safe.paddingBottom) || 0) - 12;
      const sidebar = document.querySelector('.sidebar')?.getBoundingClientRect();
      if (sidebar && sidebar.width > w * .8 && sidebar.top >= y && sidebar.top < bottom) bottom = sidebar.top - 12;
      else if (sidebar && sidebar.width < w * .5 && sidebar.height > h * .7 && sidebar.right > left) left = sidebar.right + 12;
      const top = y + (parseFloat(safe.paddingTop) || 0) + 12;
      return { left, top, right: x + w - 12, bottom: Math.max(top + fab.offsetHeight, bottom), width: w, height: h, x, y };
    }
    function avoidControls(x, y, b) {
      const size = fab.offsetWidth, min = b.top, max = b.bottom - size;
      const obstacles = [...document.querySelectorAll('#batchAction, #quickForm button[type="submit"], #batchForm .batchActions, #outputSirenBtn, #outputLightBtn, #addDeviceBtn, .headerReadings')]
        .filter(el => el.getClientRects().length && !el.closest('.page:not(.active)')).map(el => el.getBoundingClientRect());
      const overlaps = at => obstacles.some(r => x < r.right + 6 && x + size > r.left - 6 && at < r.bottom + 6 && at + size > r.top - 6);
      const candidates = [clamp(y, min, max), min, max, ...obstacles.flatMap(r => [r.top - size - 8, r.bottom + 8])]
        .map(at => clamp(at, min, max)).sort((a, c) => Math.abs(a - y) - Math.abs(c - y));
      return candidates.find(at => !overlaps(at)) ?? clamp(y, min, max);
    }
    function moveFab(x, y) { fabAt = { x, y }; fab.style.transform = `translate3d(${x}px, ${y}px, 0)`; }
    function placeFab() {
      if (drag) return;
      const b = bounds(), size = fab.offsetWidth;
      const x = position.side === 'left' ? b.left : b.right - size;
      const y = b.top + position.ratio * Math.max(0, b.bottom - b.top - size);
      moveFab(x, avoidControls(x, y, b));
    }
    function layout() {
      placeFab(); if (!open) return;
      const b = bounds(), available = Math.max(120, b.bottom - b.top);
      if (full) {
        const width = Math.min(720, b.width - 24), height = Math.min(720, available);
        dialog.style.cssText = `width:${width}px;height:${height}px;max-height:${height}px;left:${b.x + (b.width - width) / 2}px;top:${b.top + (available - height) / 2}px;margin:0`;
      } else {
        const width = Math.min(b.width <= 600 ? b.width - 24 : 360, b.right - b.left);
        quick.style.width = `${width}px`; quick.style.maxHeight = `${Math.min(560, available)}px`;
        const height = surface.getBoundingClientRect().height;
        const x = position.side === 'right' ? fabAt.x - width - 10 : fabAt.x + fab.offsetWidth + 10;
        quick.style.left = `${clamp(x, b.left, b.right - width)}px`;
        quick.style.top = `${clamp(fabAt.y + fab.offsetHeight - height, b.top, b.bottom - height)}px`;
      }
      const focused = document.activeElement, fields = form.querySelector('.notes-editor-fields');
      if (editor && fields.contains(focused)) {
        const r = focused.getBoundingClientRect(), parent = fields.getBoundingClientRect();
        if (r.bottom > parent.bottom) fields.scrollTop += r.bottom - parent.bottom;
        else if (r.top < parent.top) fields.scrollTop -= parent.top - r.top;
      }
    }
    function persistPosition() {
      try { positionStorage?.setItem(POSITION_KEY, JSON.stringify(position)); }
      catch (_) {
        if (!storageWarningShown) { storageWarningShown = true; toast('Trình duyệt không lưu được vị trí nút ghi chú.'); }
      }
    }
    fab.addEventListener('pointerdown', event => {
      if (event.button !== 0 || saving || confirming) return;
      suppressClickUntil = 0;
      drag = { id: event.pointerId, startX: event.clientX, startY: event.clientY, x: fabAt.x, y: fabAt.y,
        latestX: event.clientX, latestY: event.clientY, moved: false, bounds: bounds() };
      fab.setPointerCapture(event.pointerId);
    });
    fab.addEventListener('pointermove', event => {
      if (!drag || event.pointerId !== drag.id) return;
      drag.latestX = event.clientX; drag.latestY = event.clientY;
      if (!drag.moved && Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY) < 7) return;
      drag.moved = true; fab.classList.add('is-dragging');
      if (!frame) frame = requestAnimationFrame(() => {
        frame = 0; if (!drag) return;
        const b = drag.bounds;
        moveFab(clamp(drag.x + drag.latestX - drag.startX, b.left, b.right - fab.offsetWidth),
          clamp(drag.y + drag.latestY - drag.startY, b.top, b.bottom - fab.offsetHeight));
      });
    });
    function endDrag(event) {
      if (!drag || event.pointerId !== drag.id) return;
      const state = drag; drag = null;
      cancelAnimationFrame(frame); frame = 0; fab.classList.remove('is-dragging');
      if (fab.hasPointerCapture(event.pointerId)) fab.releasePointerCapture(event.pointerId);
      if (state.moved) {
        const b = bounds(), x = clamp(state.x + state.latestX - state.startX, b.left, b.right - fab.offsetWidth);
        const y = clamp(state.y + state.latestY - state.startY, b.top, b.bottom - fab.offsetHeight);
        position = { side: x + fab.offsetWidth / 2 < (b.left + b.right) / 2 ? 'left' : 'right',
          ratio: clamp((y - b.top) / Math.max(1, b.bottom - b.top - fab.offsetHeight), 0, 1) };
        suppressClickUntil = performance.now() + 350; persistPosition(); layout();
      }
    }
    fab.addEventListener('pointerup', endDrag); fab.addEventListener('pointercancel', endDrag);
    fab.addEventListener('lostpointercapture', endDrag);
    fab.addEventListener('click', event => {
      if (event.detail && performance.now() < suppressClickUntil) { event.preventDefault(); return; }
      if (open) close(); else show();
    });
    $('notesClose').addEventListener('click', close); $('notesNew').addEventListener('click', () => showEditor());
    $('notesCancel').addEventListener('click', cancelEditor); $('notesViewAll').addEventListener('click', showAll);
    form.addEventListener('submit', save);
    voiceButton.addEventListener('click', toggleVoice); voiceUi(voiceDefault);
    body.addEventListener('input', () => { $('notesCounter').textContent = `${body.value.length} / 300`; editorError(); });
    $('notesSearch').addEventListener('input', event => { query = event.target.value; renderList(); });
    for (const button of document.querySelectorAll('[data-notes-filter]')) button.addEventListener('click', () => {
      filter = button.dataset.notesFilter;
      for (const item of document.querySelectorAll('[data-notes-filter]')) item.setAttribute('aria-pressed', String(item === button));
      renderList();
    });
    document.addEventListener('pointerdown', event => {
      if (!open || full || editor && dirty() || saving || confirming || root.contains(event.target) || document.querySelector('dialog[open]')) return;
      close();
    });
    document.addEventListener('keydown', event => {
      if (!open || document.querySelector('dialog[open]:not(#notesDialog)')) return;
      if (event.key === 'Escape' && !full) { event.preventDefault(); close(); }
      if (event.key === 'Tab' && surface.contains(document.activeElement)) {
        const controls = [...surface.querySelectorAll('button, input, select, textarea')].filter(el => !el.disabled && el.getClientRects().length);
        const first = controls[0], last = controls.at(-1);
        if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
        else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
      }
    });
    dialog.addEventListener('cancel', event => { event.preventDefault(); close(); });
    dialog.addEventListener('click', event => { if (event.target === dialog && !dirty()) close(); });
    dialog.addEventListener('wheel', event => { if (event.target === dialog) event.preventDefault(); }, { passive: false });
    global.addEventListener('resize', layout);
    global.addEventListener('pagehide', () => stopVoice());
    document.addEventListener('visibilitychange', () => { if (document.hidden) stopVoice('Micro đã dừng khi rời trang.'); });
    document.addEventListener('scroll', event => {
      if (!surface.contains(event.target)) layout();
    }, { capture: true, passive: true });
    const pageObserver = new MutationObserver(layout);
    pageObserver.observe(document.body, { attributes: true, attributeFilter: ['data-page'] });
    global.visualViewport?.addEventListener('resize', layout); global.visualViewport?.addEventListener('scroll', layout);
    const observer = new ResizeObserver(() => { if (open) layout(); }); observer.observe(surface);
    root.hidden = false; contextKey = contextSignature(context);
    layout(); reload();
    active = { syncContext }; return active;
  }
  const api = { MemoryNotesStore, insertDictation, mount, syncContext: () => active?.syncContext() };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  global.MayapNotes = api;
})(typeof window !== 'undefined' ? window : globalThis);
