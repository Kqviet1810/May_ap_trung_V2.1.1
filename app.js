(() => {
  'use strict';

  const $ = (id) => document.getElementById(id);
  const WEB = Object.freeze({
    mqttUrl: '',
    mqttUsername: '',
    mqttPassword: '',
    topicRoot: 'mayap/v1',
    reconnectPeriodMs: 3000,
    connectTimeoutMs: 8000,
    keepaliveSeconds: 60,
    sessionTtlMs: 15000,
    sessionRefreshMs: 9000,
    staleAfterMs: 90000,
    commandTimeoutMs: 10000,
    configTimeoutMs: 15000,
    ...window.MAYAP_WEB_CONFIG
  });

  const STORAGE = 'mayap.web.v9';
  const PROTOCOL_VERSION = 1;
  const DEVICE_ID_RE = /^MAP-[A-F0-9]{12}$/;
  const CONFIG_KEYS = Object.freeze([
    'targetTemp', 'tempHysteresis', 'lowTempAlarm', 'highTempAlarm',
    'emergencyTemp', 'kp', 'ki', 'kd', 'lowHumidityAlarm', 'ventOnTemp',
    'ventOffTemp', 'tempOffset', 'humidityOffset', 'pidCycleSec',
    'humidityAlarmDelaySec', 'turnIntervalMin', 'turnMaxRunSec',
    'powerRestoreDelaySec', 'sensorTimeoutSec', 'maxHeaterPower',
    'totalIncubationDays', 'circulationFanEnabled', 'turningEnabled',
    'autoResumeAfterPower', 'allowHeatWithoutBatch', 'alarmEnabled',
    'controlMode', 'nextDirection'
  ]);

  const DEFAULT_BATCH_META = Object.freeze({
    name: 'Mẻ ấp 01',
    startDate: new Date().toISOString().slice(0, 10),
    targetHumidity: 58
  });

  const pageMeta = {
    device: ['Thiết bị', 'Theo dõi máy đang chọn và chỉnh nhanh thông số chính.'],
    batch: ['Mẻ ấp', 'Cấu hình mẻ trực tiếp và bắt đầu khi đã sẵn sàng.'],
    settings: ['Cài đặt', 'Chỉ mở nhóm cần thay đổi, các thông số còn lại được giữ gọn.']
  };

  const state = {
    devices: loadDevices(),
    selectedId: localStorage.getItem(`${STORAGE}.selected`) || '',
    mqtt: null,
    mqttConnected: false,
    mqttMessage: 'Chưa kết nối MQTT',
    subscriptions: new Set(),
    sessionTimer: 0,
    staleTimer: 0,
    formFlags: new Map(),
    pending: new Map(),
    confirmResolver: null,
    currentActivityStartedAt: 0,
    lastResumePromptBootId: 0
  };

  function createDevice(id, name) {
    return {
      id,
      name,
      presence: null,
      presenceAt: 0,
      snapshot: null,
      snapshotAt: 0,
      config: null,
      configAt: 0,
      revision: 0,
      bootId: 0,
      commandSequence: Math.max(1, Number(localStorage.getItem(`${STORAGE}.seq.${id}`) || 0)),
      logs: loadJson(`${STORAGE}.logs.${id}`, []),
      batchMeta: {
        ...DEFAULT_BATCH_META,
        ...loadJson(`${STORAGE}.batch.${id}`, {})
      }
    };
  }

  function loadDevices() {
    const stored = loadJson(`${STORAGE}.devices`, []);
    if (!Array.isArray(stored)) return [];
    return stored
      .filter((item) => item && DEVICE_ID_RE.test(String(item.id || '').toUpperCase()))
      .map((item) => createDevice(String(item.id).toUpperCase(), String(item.name || 'Máy ấp')));
  }

  function loadJson(key, fallback) {
    try {
      const parsed = JSON.parse(localStorage.getItem(key) || 'null');
      return parsed ?? fallback;
    } catch (_) {
      return fallback;
    }
  }

  function saveDevices() {
    localStorage.setItem(`${STORAGE}.devices`, JSON.stringify(
      state.devices.map(({ id, name }) => ({ id, name }))
    ));
    localStorage.setItem(`${STORAGE}.selected`, state.selectedId || '');
  }

  function saveDeviceRuntime(device) {
    if (!device) return;
    localStorage.setItem(`${STORAGE}.batch.${device.id}`, JSON.stringify(device.batchMeta));
    localStorage.setItem(`${STORAGE}.logs.${device.id}`, JSON.stringify(device.logs.slice(0, 100)));
    localStorage.setItem(`${STORAGE}.seq.${device.id}`, String(device.commandSequence));
  }

  function currentDevice() {
    return state.devices.find((device) => device.id === state.selectedId) || null;
  }

  function normalizeDeviceId(value) {
    return String(value || '').trim().toUpperCase().replace(/\s+/g, '');
  }

  function escapeHtml(value) {
    return String(value).replace(/[&<>'"]/g, (char) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;'
    }[char]));
  }

  function numberVi(value, digits = 1) {
    const number = Number(value);
    return Number.isFinite(number) ? number.toFixed(digits).replace('.', ',') : '—';
  }

  function bool(value) {
    return value === true;
  }

  // Firmware WEB_REQUEST_ID_CAPACITY = 40 byte KE CA null terminator (xem
  // config.h + mayap_web_adapter.h::readString trong firmware RC2), nen chuoi
  // request Id toi da dung duoc la 39 ky tu. crypto.randomUUID() co dau gach
  // ngang dai 36 ky tu; kem tien to "cfg-"/"cmd-" (4 ky tu) la du 40 ky tu va
  // BI FIRMWARE TU CHOI TOAN BO GOI (readString tra ve false -> "invalid").
  // Bo dau gach ngang de con 32 ky tu hex, luon nam duoi gioi han an toan.
  const REQUEST_ID_MAX = 36;
  function requestId(prefix = 'req') {
    const raw = crypto.randomUUID
      ? crypto.randomUUID().replace(/-/g, '')
      : `${Date.now().toString(36)}${Math.random().toString(36).slice(2)}${Math.random().toString(36).slice(2)}`;
    return `${prefix}-${raw}`.slice(0, REQUEST_ID_MAX);
  }

  function topicBase(deviceId) {
    return `${String(WEB.topicRoot || 'mayap/v1').replace(/\/+$/, '')}/${deviceId}`;
  }

  function topics(deviceId) {
    const base = topicBase(deviceId);
    return {
      presence: `${base}/presence`,
      snapshot: `${base}/snapshot`,
      report: `${base}/config/reported`,
      ack: `${base}/ack`,
      log: `${base}/log`,
      config: `${base}/config/set`,
      command: `${base}/command`,
      session: `${base}/session`
    };
  }

  function parseTopic(topic) {
    const root = String(WEB.topicRoot || 'mayap/v1').replace(/^\/+|\/+$/g, '');
    const escaped = root.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const match = String(topic).match(new RegExp(`^${escaped}/(MAP-[A-F0-9]{12})/(presence|snapshot|config/reported|ack|log)$`));
    return match ? { deviceId: match[1], channel: match[2] } : null;
  }

  function toast(message, timeout = 2600) {
    const element = $('toast');
    if (!element) return;
    clearTimeout(toast.timer);
    element.textContent = message;
    element.classList.add('show');
    toast.timer = setTimeout(() => element.classList.remove('show'), timeout);
  }

  function timeText(epochOrDate) {
    const date = epochOrDate instanceof Date
      ? epochOrDate
      : new Date(Number(epochOrDate) > 1000000000000 ? Number(epochOrDate) : Number(epochOrDate) * 1000);
    if (Number.isNaN(date.getTime())) return '--:--';
    return date.toLocaleTimeString('vi-VN', { hour: '2-digit', minute: '2-digit' });
  }

  function confirmAction({ title, message, accept = 'Xác nhận', danger = false }) {
    $('confirmTitle').textContent = title;
    $('confirmMessage').textContent = message;
    $('confirmAccept').textContent = accept;
    $('confirmAccept').className = danger ? 'dangerButton' : 'primary dark';
    $('confirmDialog').showModal();
    return new Promise((resolve) => { state.confirmResolver = resolve; });
  }

  function finishConfirm(value) {
    if ($('confirmDialog').open) $('confirmDialog').close();
    const resolve = state.confirmResolver;
    state.confirmResolver = null;
    if (resolve) resolve(value);
  }

  function ensureFormState(formId) {
    const form = $(formId);
    if (!form) return null;
    let element = form.querySelector('.formState');
    if (!element) {
      // Giữ trạng thái cho logic và trình đọc màn hình nhưng không chiếm chỗ giao diện.
      element = document.createElement('output');
      element.className = 'formState';
      element.hidden = true;
      element.setAttribute('aria-live', 'polite');
      form.append(element);
    }
    return element;
  }

  function updateSubmitState(formId, mode) {
    const submit = $(formId)?.querySelector('button[type="submit"]');
    if (!submit) return;
    if (!submit.dataset.defaultLabel) submit.dataset.defaultLabel = submit.textContent.trim();
    submit.classList.toggle('attention', mode === 'dirty');
    submit.disabled = mode === 'pending';
    submit.textContent = mode === 'pending' ? 'Đang lưu…' : submit.dataset.defaultLabel;
  }

  function setFormState(formId, mode, text) {
    const element = ensureFormState(formId);
    if (!element) return;
    element.dataset.mode = mode;
    element.textContent = text;
    const old = state.formFlags.get(formId) || {};
    state.formFlags.set(formId, { ...old, dirty: mode === 'dirty', mode, text });
    updateSubmitState(formId, mode);
  }

  function setFormError(formId, message = '') {
    const form = $(formId);
    if (!form) return;
    let error = form.querySelector('.formError');
    if (!error) {
      error = document.createElement('div');
      error.className = 'formError';
      const submit = form.querySelector('button[type="submit"]');
      form.insertBefore(error, submit || null);
    }
    error.textContent = message;
    error.classList.toggle('show', Boolean(message));
  }

  function clearInvalid(formId) {
    $(formId)?.querySelectorAll('.invalid').forEach((element) => element.classList.remove('invalid'));
    setFormError(formId, '');
  }

  function invalidate(formId, id, message) {
    const element = $(id);
    element?.classList.add('invalid');
    element?.closest('.compactTile')?.classList.add('invalid');
    setFormError(formId, message);
    element?.focus();
    return false;
  }

  function markDirty(formId) {
    setFormState(formId, 'dirty', 'Có thay đổi chưa lưu');
  }

  function registerDirty(formId) {
    const form = $(formId);
    if (!form) return;
    ensureFormState(formId);
    form.querySelectorAll('input,select').forEach((element) => {
      element.addEventListener('input', () => markDirty(formId));
      element.addEventListener('change', () => markDirty(formId));
    });
  }

  function showPage(name) {
    document.body.dataset.page = name;
    document.querySelectorAll('.page').forEach((element) => {
      element.classList.toggle('active', element.id === `page-${name}`);
    });
    document.querySelectorAll('.nav button').forEach((element) => {
      element.classList.toggle('active', element.dataset.page === name);
    });
    $('pageTitle').textContent = pageMeta[name][0];
    $('pageSubtitle').textContent = pageMeta[name][1];
    if (name === 'batch') renderBatchLogs();
    window.scrollTo({ top: 0, left: 0, behavior: 'smooth' });
  }

  function connectionStatus(device) {
    if (!device) return 'none';
    if (!state.mqttConnected) return 'connecting';
    if (!device.presence) return 'connecting';
    if (!device.presence.online) return 'offline';
    const lastAt = Math.max(device.presenceAt || 0, device.snapshotAt || 0, device.configAt || 0);
    if (!lastAt || Date.now() - lastAt > WEB.staleAfterMs) return 'offline';
    return 'online';
  }

  function isDeviceOnline(device = currentDevice()) {
    return connectionStatus(device) === 'online';
  }

  function renderSelector() {
    const select = $('deviceSelector');
    select.replaceChildren();
    if (!state.devices.length) {
      select.add(new Option('Chưa có thiết bị', ''));
      select.disabled = true;
      state.selectedId = '';
    } else {
      select.disabled = false;
      state.devices.forEach((device) => select.add(new Option(device.name, device.id)));
      if (!state.devices.some((device) => device.id === state.selectedId)) {
        state.selectedId = state.devices[0].id;
      }
      select.value = state.selectedId;
    }
    saveDevices();
    renderDevice();
    renderDeviceList();
    syncSelectedDevice(true);
  }

  function renderDevice() {
    const device = currentDevice();
    const connection = connectionStatus(device);
    const pill = $('onlinePill');

    $('deviceNameView').textContent = device?.name || 'Chưa chọn thiết bị';
    $('deviceIdView').textContent = device?.id || 'Nhấn + để thêm thiết bị';
    $('sideDevice').textContent = device?.name || 'Chưa có thiết bị';

    if (connection === 'online') {
      pill.textContent = 'ONLINE';
      pill.className = 'pill online';
      $('wifiConnectionText').textContent = 'Wi‑Fi đã kết nối';
      $('sideStatus').textContent = 'Đang trực tuyến';
    } else if (connection === 'connecting') {
      pill.textContent = 'ĐANG KẾT NỐI';
      pill.className = 'pill soft';
      $('wifiConnectionText').textContent = state.mqttMessage;
      $('sideStatus').textContent = 'Đang kết nối';
    } else {
      pill.textContent = 'OFFLINE';
      pill.className = 'pill offline';
      $('wifiConnectionText').textContent = 'Chưa nhận dữ liệu từ máy';
      $('sideStatus').textContent = 'Đang ngoại tuyến';
    }

    const ssid = device?.presence?.ssid || '';
    $('wifiSettingSummary').textContent = connection === 'online'
      ? (ssid ? `Đang kết nối: ${ssid}` : 'Wi‑Fi đã kết nối')
      : 'Đổi bằng trang cài đặt ESP32';

    applySnapshotToUi(device);
    renderBatchLogs();
  }

  function renderDeviceList() {
    const root = $('deviceList');
    root.replaceChildren();
    state.devices.forEach((device) => {
      const row = document.createElement('div');
      row.className = 'deviceListItem';
      const text = document.createElement('div');
      text.innerHTML = `<strong>${escapeHtml(device.name)}</strong><small>${escapeHtml(device.id)}</small>`;
      const remove = document.createElement('button');
      remove.type = 'button';
      remove.textContent = 'Xóa';
      remove.addEventListener('click', async () => {
        const ok = await confirmAction({
          title: 'Xóa thiết bị?',
          message: `${device.name} chỉ bị xóa khỏi danh sách trên trình duyệt. ESP32 không bị xóa cấu hình.`,
          accept: 'Xóa thiết bị',
          danger: true
        });
        if (!ok) return;
        deactivateSession(device.id);
        unsubscribeDevice(device.id);
        state.devices = state.devices.filter((item) => item.id !== device.id);
        if (state.selectedId === device.id) state.selectedId = state.devices[0]?.id || '';
        renderSelector();
        toast('Đã xóa thiết bị khỏi website');
      });
      row.append(text, remove);
      root.append(row);
    });
  }

  function updateOutput(id, on, onText = 'ON', offText = 'OFF') {
    const element = $(id);
    if (!element) return;
    element.textContent = on ? onText : offText;
    element.classList.toggle('on', Boolean(on));
  }

  function clearBatchActionPending(device) {
    if (!device) return;
    device.batchUiPendingTarget = '';
    device.batchUiPendingUntil = 0;
  }

  function renderBatchAction(device, runtime = device?.snapshot?.runtime) {
    const button = $('batchAction');
    if (!button) return;

    const running = Boolean(runtime?.batchRunning);
    const pending = Boolean(
      device?.batchUiPendingTarget &&
      Number(device.batchUiPendingUntil || 0) > Date.now()
    );

    if (pending) {
      const starting = device.batchUiPendingTarget === 'running';
      button.disabled = true;
      button.textContent = starting ? 'Đang bắt đầu…' : 'Đang kết thúc…';
      button.className = starting ? 'primary dark' : 'dangerButton';
      button.setAttribute('aria-label', button.textContent);
      return;
    }

    clearBatchActionPending(device);
    button.disabled = false;
    button.textContent = running ? 'Kết thúc' : 'Bắt đầu';
    button.className = running ? 'dangerButton' : 'primary dark';
    button.setAttribute('aria-label', running ? 'Kết thúc mẻ ấp' : 'Bắt đầu mẻ ấp');
  }

  function beginBatchActionPending(device, target) {
    if (!device) return;
    device.batchUiPendingTarget = target;
    device.batchUiPendingUntil = Date.now() + Math.max(10000, Number(WEB.commandTimeoutMs || 0) + 2000);
    const expectedUntil = device.batchUiPendingUntil;
    renderBatchAction(device);
    setTimeout(() => {
      if (device.batchUiPendingUntil !== expectedUntil || Date.now() < expectedUntil) return;
      clearBatchActionPending(device);
      if (device.id === state.selectedId) renderBatchAction(device);
    }, Math.max(0, expectedUntil - Date.now() + 50));
  }

  function applySnapshotToUi(device) {
    const runtime = device?.snapshot?.runtime;
    if (!runtime) {
      $('liveTemp').textContent = '—';
      $('liveHumidity').textContent = '—';
      $('liveState').textContent = connectionStatus(device) === 'online' ? 'Đang đồng bộ' : 'Offline';
      updateOutput('outputHeater', false);
      updateOutput('outputCirculation', false);
      updateOutput('outputVent', false);
      updateOutput('outputTurn', false, 'ĐANG ĐẢO', 'CHỜ');
      setCurrentActivity('Chưa có dữ liệu vận hành', 'Đang chờ snapshot từ ESP32', 'idle');
      renderBatchAction(device, null);
      return;
    }

    $('liveTemp').textContent = `${numberVi(runtime.temperature)}°C`;
    $('liveHumidity').textContent = `${numberVi(runtime.humidity, 0)}%`;
    $('liveState').textContent = runtime.machineState || (runtime.batchRunning ? 'Đang ấp' : 'Sẵn sàng');
    updateOutput('outputHeater', bool(runtime.heaterOn));
    updateOutput('outputCirculation', bool(runtime.circulationFanOn));
    updateOutput('outputVent', bool(runtime.ventFanOn));

    const turnMap = { 0: 'DỪNG', 1: 'TRÁI', 2: 'PHẢI', 3: 'CHỜ', 4: 'LỖI' };
    const turn = Number(runtime.turnState);
    const outputTurn = $('outputTurn');
    outputTurn.textContent = turnMap[turn] || '—';
    outputTurn.classList.toggle('on', turn === 1 || turn === 2);

    $('batchPill').textContent = runtime.batchRunning ? `NGÀY ${runtime.currentDay || 1}` : 'CHƯA BẮT ĐẦU';
    $('batchPill').className = runtime.batchRunning ? 'pill online' : 'pill soft';

    if ((device?.batchUiPendingTarget === 'running' && runtime.batchRunning) ||
        (device?.batchUiPendingTarget === 'stopped' && !runtime.batchRunning)) {
      clearBatchActionPending(device);
    }
    renderBatchAction(device, runtime);

    const autoTuneState = Number(runtime.autoTuneState || 0);
    const autoTuneProgress = Math.max(0, Math.min(100, Number(runtime.autoTuneProgress || 0)));
    $('tuneBar').style.width = `${autoTuneProgress}%`;
    if (autoTuneState === 1) {
      $('tuneText').textContent = `Đang chạy · ${autoTuneProgress}%`;
      $('pidSummary').textContent = `Đang Auto Tune · ${autoTuneProgress}%`;
      $('startTune').disabled = true;
      $('startTune').textContent = `Đang Auto Tune PID · ${autoTuneProgress}%`;
    } else {
      $('startTune').disabled = false;
      $('startTune').textContent = 'Bắt đầu Auto Tune PID';
      if (autoTuneState === 2) {
        $('tuneText').textContent = 'Hoàn tất · thông số đã được máy lưu';
        $('pidSummary').textContent = 'Đã hoàn tất và tự lưu';
      } else if (autoTuneState === 3) {
        $('tuneText').textContent = 'Auto Tune không hoàn tất';
        $('pidSummary').textContent = 'Auto Tune thất bại';
      } else {
        $('tuneText').textContent = 'Sẵn sàng';
        $('pidSummary').textContent = 'ESP32 tự tìm và lưu thông số';
      }
    }

    if (autoTuneState === 1) {
      setCurrentActivity('Đang Auto Tune PID', `Tiến độ ${autoTuneProgress}%`, 'warning');
    } else if (turn === 1 || turn === 2) {
      setCurrentActivity(`Đang đảo trứng sang ${turn === 1 ? 'trái' : 'phải'}`, 'Đang chờ công tắc hành trình', 'active');
    } else if (runtime.ventFanOn) {
      setCurrentActivity('Quạt thông gió đang chạy', `Nhiệt độ ${numberVi(runtime.temperature)} °C`, 'active');
    } else if (runtime.heaterOn) {
      setCurrentActivity(`Đang gia nhiệt ${numberVi(runtime.temperature)} °C`, `Công suất ${numberVi(runtime.heaterPower, 0)}%`, 'active');
    } else if (runtime.batchRunning) {
      setCurrentActivity(`Đang giữ nhiệt ${numberVi(runtime.temperature)} °C`, `Còn ${runtime.nextTurnMinutes || 0} phút tới lần đảo`, 'active');
    } else {
      setCurrentActivity('Sẵn sàng', runtime.machineState || 'Không có tác vụ đang chạy', 'idle');
    }

    if (runtime.resumeConfirmationRequired && device.bootId && state.lastResumePromptBootId !== device.bootId) {
      state.lastResumePromptBootId = device.bootId;
      promptResumeAfterPower(device);
    }
  }

  async function promptResumeAfterPower(device) {
    const continueBatch = await confirmAction({
      title: 'Tiếp tục mẻ sau mất điện?',
      message: 'ESP32 phát hiện mẻ ấp đang chờ xác nhận sau khi có điện trở lại.',
      accept: 'Tiếp tục mẻ'
    });
    await sendCommand(continueBatch ? 'resume_yes' : 'resume_no', { device });
  }

  function updateSettingSummaries() {
    $('temperatureSummary').textContent = `Đặt ${numberVi($('targetTemp').value)}°C · ngắt khẩn ${numberVi($('emergencyTemp').value)}°C`;
    $('turningSummary').textContent = $('turningEnabled').checked
      ? `Tự động · mỗi ${$('turnInterval').value || '—'} phút`
      : 'Đang tắt đảo tự động';
    $('sensorSummary').textContent = `Bù ${numberVi($('tempOffset').value)}°C · timeout ${$('sensorTimeout').value || '—'} giây`;
    $('notificationSummary').textContent = $('notificationsEnabled').checked ? 'Đang bật' : 'Đang tắt';
  }

  function hasDirtyForm(formId) {
    return Boolean(state.formFlags.get(formId)?.dirty);
  }

  function applyConfigToUi(device, force = false) {
    const config = device?.config;
    if (!config) return;
    const assign = (formId, id, value) => {
      if (!force && hasDirtyForm(formId)) return;
      const element = $(id);
      if (element) element.value = value;
    };
    const check = (formId, id, value) => {
      if (!force && hasDirtyForm(formId)) return;
      const element = $(id);
      if (element) element.checked = Boolean(value);
    };

    assign('quickForm', 'quickTarget', config.targetTemp);
    assign('quickForm', 'quickTurn', config.turnIntervalMin);

    assign('batchForm', 'batchTarget', config.targetTemp);
    assign('batchForm', 'totalDays', config.totalIncubationDays);
    check('batchForm', 'resumeAfterPowerLoss', config.autoResumeAfterPower);
    if (!hasDirtyForm('batchForm') || force) {
      $('batchName').value = device.batchMeta.name;
      $('startDate').value = device.batchMeta.startDate;
      $('targetHumidity').value = device.batchMeta.targetHumidity;
    }

    assign('temperatureForm', 'targetTemp', config.targetTemp);
    assign('temperatureForm', 'lowAlarm', config.lowTempAlarm);
    assign('temperatureForm', 'highAlarm', config.highTempAlarm);
    assign('temperatureForm', 'emergencyTemp', config.emergencyTemp);
    assign('temperatureForm', 'ventOn', config.ventOnTemp);
    assign('temperatureForm', 'ventOff', config.ventOffTemp);

    check('turningForm', 'turningEnabled', config.turningEnabled);
    assign('turningForm', 'turnInterval', config.turnIntervalMin);
    assign('turningForm', 'limitAlarmTime', config.turnMaxRunSec);
    assign('turningForm', 'nextDirection', Number(config.nextDirection) === 1 ? 'right' : 'left');

    assign('sensorForm', 'tempOffset', config.tempOffset);
    assign('sensorForm', 'humidityOffset', config.humidityOffset);
    assign('sensorForm', 'sensorTimeout', config.sensorTimeoutSec);

    ['quickForm', 'batchForm', 'temperatureForm', 'turningForm', 'sensorForm'].forEach((formId) => {
      if (force || !hasDirtyForm(formId)) setFormState(formId, 'saved', 'Đã đồng bộ với ESP32');
    });
    updateSettingSummaries();
  }

  function validateFullConfig(config) {
    return CONFIG_KEYS.every((key) => Object.prototype.hasOwnProperty.call(config, key)) &&
      Number.isFinite(Number(config.targetTemp));
  }

  function configEquals(left, right) {
    if (!left || !right) return false;
    return CONFIG_KEYS.every((key) => {
      const a = left[key];
      const b = right[key];
      if (typeof a === 'number' || typeof b === 'number') return Math.abs(Number(a) - Number(b)) < 0.0005;
      return a === b;
    });
  }

  function buildConfig(group) {
    const device = currentDevice();
    if (!device?.config || !validateFullConfig(device.config)) {
      toast('Chưa nhận đủ cấu hình từ ESP32. Hãy chờ máy đồng bộ.');
      return null;
    }
    const config = { ...device.config };
    if (group === 'quick') {
      config.targetTemp = Number($('quickTarget').value);
      config.turnIntervalMin = Number($('quickTurn').value);
    } else if (group === 'batch') {
      config.targetTemp = Number($('batchTarget').value);
      config.totalIncubationDays = Number($('totalDays').value);
      config.autoResumeAfterPower = $('resumeAfterPowerLoss').checked;
    } else if (group === 'temperature') {
      config.targetTemp = Number($('targetTemp').value);
      config.lowTempAlarm = Number($('lowAlarm').value);
      config.highTempAlarm = Number($('highAlarm').value);
      config.emergencyTemp = Number($('emergencyTemp').value);
      config.ventOnTemp = Number($('ventOn').value);
      config.ventOffTemp = Number($('ventOff').value);
    } else if (group === 'turning') {
      config.turningEnabled = $('turningEnabled').checked;
      config.turnIntervalMin = Number($('turnInterval').value);
      config.turnMaxRunSec = Number($('limitAlarmTime').value);
      config.nextDirection = $('nextDirection').value === 'right' ? 1 : 0;
    } else if (group === 'sensor') {
      config.tempOffset = Number($('tempOffset').value);
      config.humidityOffset = Number($('humidityOffset').value);
      config.sensorTimeoutSec = Number($('sensorTimeout').value);
    }
    return config;
  }

  function nextRevision(device) {
    return Math.max(Number(device.revision || 0) + 1, Math.floor(Date.now() / 1000));
  }

  function publish(topic, payload, options = {}) {
    if (!state.mqttConnected || !state.mqtt?.connected) {
      throw new Error('MQTT chưa kết nối');
    }
    state.mqtt.publish(topic, JSON.stringify(payload), {
      qos: options.qos ?? 1,
      retain: options.retain ?? false
    });
  }

  async function sendConfig(formId, group) {
    const device = currentDevice();
    if (!device) return toast('Hãy thêm thiết bị trước');
    if (!isDeviceOnline(device)) {
      setFormState(formId, 'unconfirmed', 'Thiết bị đang offline · chưa gửi');
      toast('Thiết bị đang offline · chưa gửi');
      return;
    }
    const config = buildConfig(group);
    if (!config) return;

    if (group === 'batch') {
      device.batchMeta = {
        name: $('batchName').value.trim(),
        startDate: $('startDate').value,
        targetHumidity: Number($('targetHumidity').value)
      };
      saveDeviceRuntime(device);
    }

    const revision = nextRevision(device);
    const id = requestId('cfg');
    const payload = { v: PROTOCOL_VERSION, revision, requestId: id, config };
    setFormState(formId, 'pending', 'Đang gửi tới ESP32…');
    state.pending.set(id, {
      kind: 'config', deviceId: device.id, formId, revision, config,
      timeout: setTimeout(() => {
        const pending = state.pending.get(id);
        if (!pending) return;
        state.pending.delete(id);
        setFormState(formId, 'unconfirmed', 'ESP32 chưa xác nhận · có thể thử lưu lại');
        toast('ESP32 chưa xác nhận · có thể thử lưu lại', 3600);
      }, WEB.configTimeoutMs)
    });
    try {
      publish(topics(device.id).config, payload, { qos: 1, retain: true });
    } catch (error) {
      clearPending(id);
      setFormState(formId, 'error', error.message);
      toast(error.message, 3600);
    }
  }

  async function sendCommand(action, options = {}) {
    const device = options.device || currentDevice();
    if (!device) return false;
    if (!isDeviceOnline(device)) {
      toast('Thiết bị đang offline');
      return false;
    }
    if (!device.bootId) {
      toast('Chưa nhận bootId. Hãy chờ ESP32 đồng bộ.');
      sendSession(device.id, true, true);
      return false;
    }

    device.commandSequence = Math.max(device.commandSequence + 1, Math.floor(Date.now() / 1000));
    saveDeviceRuntime(device);
    const id = requestId('cmd');
    const payload = {
      v: PROTOCOL_VERSION,
      sequence: device.commandSequence,
      requestId: id,
      bootId: device.bootId,
      expiresAt: Math.floor(Date.now() / 1000) + 8,
      action,
      validForMs: options.validForMs ?? 5000,
      leaseMs: options.leaseMs ?? 0,
      alarmMask: options.alarmMask ?? 0,
      arg0: options.arg0 ?? 0,
      arg1: options.arg1 ?? 0,
      value: options.value ?? 0
    };

    state.pending.set(id, {
      kind: 'command', deviceId: device.id, action,
      timeout: setTimeout(() => {
        if (!state.pending.has(id)) return;
        state.pending.delete(id);
        if (action === 'batch_start' || action === 'batch_stop') {
          clearBatchActionPending(device);
          if (device.id === state.selectedId) renderBatchAction(device);
        }
        toast('ESP32 chưa phản hồi lệnh');
      }, WEB.commandTimeoutMs)
    });

    try {
      publish(topics(device.id).command, payload, { qos: 1, retain: false });
      return true;
    } catch (error) {
      clearPending(id);
      toast(error.message);
      return false;
    }
  }

  function clearPending(id) {
    const pending = state.pending.get(id);
    if (pending?.timeout) clearTimeout(pending.timeout);
    state.pending.delete(id);
  }

  function handleAck(device, ack) {
    if (Number.isFinite(Number(ack.bootId))) device.bootId = Number(ack.bootId);
    const pending = state.pending.get(String(ack.requestId || ''));
    const result = String(ack.result || '').toLowerCase();
    const message = humanAckMessage(ack);

    if (!pending) {
      if (result === 'rejected' || result === 'invalid' || result === 'expired' || result === 'stale') {
        addBatchLog(device, message);
      }
      return;
    }

    if (pending.kind === 'config') {
      if (result === 'accepted') {
        setFormState(pending.formId, 'pending', 'ESP32 đã nhận · đang lưu EEPROM…');
        return;
      }
      if (result === 'applied') {
        setFormState(pending.formId, 'pending', 'Đã lưu · đang đọc lại cấu hình…');
        return;
      }
      if (result === 'duplicate') {
        setFormState(pending.formId, 'pending', 'Đang đồng bộ cấu hình đã lưu…');
        sendSession(device.id, true, true);
        return;
      }
      clearPending(String(ack.requestId));
      setFormState(pending.formId, 'error', message);
      toast(message);
      return;
    }

    clearPending(String(ack.requestId));
    if (['accepted', 'applied'].includes(result)) {
      toast(message);
    } else {
      if (pending.action === 'batch_start' || pending.action === 'batch_stop') {
        clearBatchActionPending(device);
        if (device.id === state.selectedId) renderBatchAction(device);
      }
      toast(message, 3600);
      addBatchLog(device, message);
    }
  }

  function humanAckMessage(ack) {
    const result = String(ack.result || '');
    const raw = String(ack.message || '').trim();
    const map = {
      accepted: 'ESP32 đã tiếp nhận yêu cầu',
      applied: 'ESP32 đã lưu và áp dụng cấu hình',
      rejected: 'ESP32 từ chối yêu cầu',
      invalid: 'Dữ liệu gửi xuống không hợp lệ',
      duplicate: 'Yêu cầu này đã được xử lý',
      busy: 'ESP32 đang xử lý yêu cầu khác',
      expired: 'Lệnh đã hết thời gian hiệu lực',
      stale: 'Lệnh thuộc lần khởi động cũ',
      unsupported: 'Firmware chưa hỗ trợ lệnh này'
    };
    if (raw && !/^[A-Z0-9 _/.-]+$/.test(raw)) return raw;
    return map[result] || raw || `Phản hồi: ${result || 'không xác định'}`;
  }

  function handleConfigReport(device, report) {
    if (!report.config || !validateFullConfig(report.config)) return;
    device.config = { ...report.config };
    device.revision = Number(report.revision || 0);
    device.configAt = Date.now();
    device.bootId = Number(report.bootId || device.bootId || 0);

    for (const [id, pending] of state.pending.entries()) {
      if (pending.kind !== 'config' || pending.deviceId !== device.id) continue;
      if (device.revision >= pending.revision && configEquals(device.config, pending.config)) {
        clearPending(id);
        setFormState(pending.formId, 'saved', 'Đã lưu và ESP32 xác nhận');
        addBatchLog(device, 'Cấu hình đã được ESP32 lưu và xác nhận');
        toast('Đã lưu và ESP32 xác nhận');
      }
    }

    if (device.id === state.selectedId) applyConfigToUi(device);
  }

  function handleSnapshot(device, snapshot) {
    device.snapshot = snapshot;
    device.snapshotAt = Date.now();
    device.bootId = Number(snapshot.bootId || device.bootId || 0);
    if (Number(snapshot.revision || 0) > device.revision) device.revision = Number(snapshot.revision);
    if (device.id === state.selectedId) {
      renderDevice();
    }
  }

  function handlePresence(device, presence) {
    device.presence = presence;
    device.presenceAt = Date.now();
    if (device.id === state.selectedId) renderDevice();
  }

  // Trang thai mang gui kem trong "value" cua su kien code 90 (NetStateChanged),
  // doi chieu enum NetState trong config.h.
  const NET_STATE_TEXT = {
    0: 'đã tắt (OFFLINE)',
    1: 'đang bật Wi‑Fi',
    2: 'chưa có Wi‑Fi',
    3: 'có Wi‑Fi, chưa lên máy chủ',
    4: 'đã kết nối máy chủ'
  };

  // event.code - 1000 = FaultCode, doi chieu bang loi faultDescriptor() trong
  // config.h. CAP NHAT DONG BO khi firmware them ma loi moi vao FaultCode.
  const FAULT_TITLES = {
    101: 'Mất cảm biến', 102: 'Cảm biến sai', 103: 'Cảm biến bất thường',
    104: 'Cảm biến đứng yên', 110: 'Nhiệt độ thấp', 111: 'Nhiệt độ cao',
    112: 'Quá nhiệt khẩn cấp', 120: 'Độ ẩm thấp - hết nước',
    201: 'Lỗi 2 hành trình', 202: 'Đảo quá thời gian', 203: 'Hành trình bị kẹt',
    204: 'Xung đột lệnh đảo', 301: 'Mất EEPROM', 302: 'EEPROM suy giảm',
    303: 'Reset bất thường', 304: 'Xung đột output', 305: 'Relay đóng cắt nhiều',
    306: 'Lỗi đồng hồ RTC', 307: 'Vòng điều khiển chậm', 308: 'Stack sắp cạn',
    309: 'Thiếu bộ nhớ', 310: 'Lỗi bộ nhớ RAM', 311: 'Lỗi bus I2C',
    312: 'Hàng đợi lưu đầy', 401: 'Nhiệt tăng khi đã tắt', 402: 'Thanh nhiệt không lên'
  };

  function eventText(event) {
    const code = Number(event.code || 0);
    const value = Number(event.value || 0);
    const type = Number(event.type || 0);
    const known = {
      1: 'Máy vừa được cấp nguồn',
      2: 'Máy vừa khởi động lại từ bên ngoài',
      3: 'Firmware vừa khởi động lại',
      4: 'Máy khôi phục sau lỗi hệ thống',
      5: 'Máy khôi phục sau watchdog',
      6: 'Nguồn điện từng bị sụt áp',
      20: 'Mẻ ấp đã bắt đầu',
      21: 'Mẻ ấp đã kết thúc',
      22: 'Mẻ ấp đã được khôi phục',
      23: 'Đang chờ xác nhận tiếp tục mẻ',
      24: 'Đã xác nhận tiếp tục mẻ',
      25: 'Đã hủy khôi phục mẻ',
      30: 'Máy đã chuyển sang chế độ tự động',
      31: 'Máy đã chuyển sang chế độ bằng tay',
      40: 'Cảm biến đã hoạt động trở lại',
      41: 'Mất tín hiệu cảm biến',
      50: 'Cấu hình đã được lưu',
      51: 'Auto Tune PID đã bắt đầu',
      52: 'Auto Tune PID đã hoàn tất',
      53: 'Auto Tune PID không hoàn tất',
      60: 'Bắt đầu đảo trứng sang trái',
      61: 'Bắt đầu đảo trứng sang phải',
      62: 'Đang đưa khay về gốc trái',
      63: 'Đang đưa khay về gốc phải',
      64: 'Đảo trái đã hoàn tất',
      65: 'Đảo phải đã hoàn tất',
      80: 'Một lệnh điều khiển đã bị từ chối'
    };
    if (known[code]) return known[code];
    if (code === 90) return `Trạng thái mạng: ${NET_STATE_TEXT[value] ?? `mã ${value}`}`;
    if (code === 91) return 'Đã mở cổng cấu hình Wi‑Fi (giữ nút BOOT)';
    if (code === 92) return 'Mở cổng cấu hình Wi‑Fi thất bại';
    if (code >= 1000) {
      const title = FAULT_TITLES[code - 1000] || `mã ${code - 1000}`;
      if (type === 5) return `Đã hết lỗi: ${title}`;
      if (type === 6) return `Đã xác nhận lỗi: ${title}`;
      return `Phát sinh lỗi: ${title}`;
    }
    if (code >= 200) return `Đầu ra #${code - 200} ${value ? 'đã bật' : 'đã tắt'}`;
    if (code >= 100) return `Tín hiệu vào #${code - 100} ${value ? 'đã tác động' : 'đã nhả'}`;
    return `Sự kiện máy #${code}`;
  }

  function handleLog(device, event) {
    const entry = {
      sequence: Number(event.sequence || 0),
      time: Number(event.epoch || 0) > 1700000000 ? Number(event.epoch) * 1000 : Date.now(),
      message: eventText(event),
      duration: ''
    };
    if (device.logs.some((item) => item.sequence && item.sequence === entry.sequence)) return;
    device.logs.unshift(entry);
    device.logs = device.logs.slice(0, 100);
    saveDeviceRuntime(device);
    if (device.id === state.selectedId) renderBatchLogs();

    if ($('notificationsEnabled').checked && document.hidden && Notification.permission === 'granted') {
      try { new Notification(device.name, { body: entry.message, icon: './icons/icon-192.png' }); } catch (_) {}
    }
  }

  function addBatchLog(device, message, duration = '') {
    if (!device) return;
    device.logs.unshift({ sequence: 0, time: Date.now(), message, duration });
    device.logs = device.logs.slice(0, 100);
    saveDeviceRuntime(device);
    if (device.id === state.selectedId) renderBatchLogs();
  }

  function renderBatchLogs() {
    const root = $('batchLogList');
    if (!root) return;
    const logs = currentDevice()?.logs?.slice(0, 6) || [];
    root.replaceChildren();
    root.classList.toggle('empty', !logs.length);
    if (!logs.length) {
      const empty = document.createElement('div');
      empty.className = 'batchLogEmpty';
      empty.textContent = 'Chưa có hoạt động do ESP32 gửi lên.';
      root.append(empty);
      return;
    }
    logs.forEach((item) => {
      const row = document.createElement('div');
      row.className = 'batchLogRow';
      const time = document.createElement('time');
      time.dateTime = new Date(item.time).toISOString();
      time.textContent = timeText(new Date(item.time));
      const message = document.createElement('span');
      message.className = 'logMessage';
      message.textContent = item.message;
      message.title = item.message;
      const duration = document.createElement('span');
      duration.className = 'logDuration';
      duration.textContent = item.duration || '';
      row.append(time, message, duration);
      root.append(row);
    });
  }

  function setCurrentActivity(text, meta, mode = 'active') {
    $('currentActivityText').textContent = text;
    $('currentActivityMeta').textContent = meta;
    $('activityPulse').className = `activityPulse ${mode === 'active' ? '' : mode}`.trim();
    state.currentActivityStartedAt = mode === 'idle' ? 0 : Date.now();
  }

  function sendSession(deviceId, active = true, sync = false) {
    if (!state.mqttConnected || !deviceId) return;
    try {
      publish(topics(deviceId).session, {
        active,
        ttlMs: active ? WEB.sessionTtlMs : 1000,
        sync
      }, { qos: 0, retain: false });
    } catch (_) {}
  }

  function activateSelectedSession(sync = false) {
    clearInterval(state.sessionTimer);
    if (!state.selectedId || !state.mqttConnected) return;
    sendSession(state.selectedId, true, sync);
    state.sessionTimer = setInterval(() => sendSession(state.selectedId, true, false), WEB.sessionRefreshMs);
  }

  function deactivateSession(deviceId) {
    if (deviceId) sendSession(deviceId, false, false);
  }

  function syncSelectedDevice(force = false) {
    const device = currentDevice();
    if (!device) return;
    subscribeDevice(device.id);
    activateSelectedSession(force);
    if (device.config) applyConfigToUi(device, force);
    renderDevice();
  }

  function subscribeDevice(deviceId) {
    if (!state.mqttConnected || !state.mqtt?.connected || !deviceId) return;
    const outputTopics = topics(deviceId);
    [outputTopics.presence, outputTopics.snapshot, outputTopics.report, outputTopics.ack, outputTopics.log]
      .forEach((topic) => {
        if (state.subscriptions.has(topic)) return;
        state.mqtt.subscribe(topic, { qos: topic.endsWith('/snapshot') ? 0 : 1 }, (error) => {
          if (!error) state.subscriptions.add(topic);
        });
      });
  }

  function unsubscribeDevice(deviceId) {
    if (!state.mqtt?.connected || !deviceId) return;
    const outputTopics = topics(deviceId);
    [outputTopics.presence, outputTopics.snapshot, outputTopics.report, outputTopics.ack, outputTopics.log]
      .forEach((topic) => {
        if (!state.subscriptions.has(topic)) return;
        state.mqtt.unsubscribe(topic);
        state.subscriptions.delete(topic);
      });
  }

  function connectMqtt() {
    if (!WEB.mqttUrl || !/^wss?:\/\//i.test(WEB.mqttUrl)) {
      state.mqttMessage = 'Chưa cấu hình MQTT WebSocket';
      renderDevice();
      return;
    }
    if (!window.mqtt?.connect) {
      state.mqttMessage = 'Không tải được thư viện MQTT.js';
      renderDevice();
      return;
    }

    const options = {
      clientId: `mayap-web-${Math.random().toString(16).slice(2, 12)}`,
      clean: true,
      reconnectPeriod: WEB.reconnectPeriodMs,
      connectTimeout: WEB.connectTimeoutMs,
      keepalive: WEB.keepaliveSeconds,
      resubscribe: false
    };
    if (WEB.mqttUsername) options.username = WEB.mqttUsername;
    if (WEB.mqttPassword) options.password = WEB.mqttPassword;

    state.mqtt = window.mqtt.connect(WEB.mqttUrl, options);
    state.mqttMessage = 'Đang kết nối MQTT…';
    renderDevice();

    state.mqtt.on('connect', () => {
      state.mqttConnected = true;
      state.mqttMessage = 'MQTT đã kết nối';
      state.subscriptions.clear();
      state.devices.forEach((device) => subscribeDevice(device.id));
      activateSelectedSession(true);
      renderDevice();
    });
    state.mqtt.on('reconnect', () => {
      state.mqttConnected = false;
      state.mqttMessage = 'Đang kết nối lại MQTT…';
      renderDevice();
    });
    state.mqtt.on('close', () => {
      state.mqttConnected = false;
      state.mqttMessage = 'MQTT đã ngắt';
      renderDevice();
    });
    state.mqtt.on('offline', () => {
      state.mqttConnected = false;
      state.mqttMessage = 'Trình duyệt đang offline';
      renderDevice();
    });
    state.mqtt.on('error', (error) => {
      state.mqttMessage = `MQTT lỗi: ${error?.message || 'không xác định'}`;
      renderDevice();
    });
    state.mqtt.on('message', (topic, data) => {
      const parsedTopic = parseTopic(topic);
      if (!parsedTopic) return;
      const device = state.devices.find((item) => item.id === parsedTopic.deviceId);
      if (!device) return;
      let payload;
      try { payload = JSON.parse(data.toString()); } catch (_) { return; }
      if (Number.isFinite(Number(payload.bootId))) device.bootId = Number(payload.bootId);
      if (parsedTopic.channel === 'presence') handlePresence(device, payload);
      else if (parsedTopic.channel === 'snapshot') handleSnapshot(device, payload);
      else if (parsedTopic.channel === 'config/reported') handleConfigReport(device, payload);
      else if (parsedTopic.channel === 'ack') handleAck(device, payload);
      else if (parsedTopic.channel === 'log') handleLog(device, payload);
    });
  }

  function validateQuickForm() {
    clearInvalid('quickForm');
    const target = Number($('quickTarget').value);
    const interval = Number($('quickTurn').value);
    if (!(target >= 30 && target <= 40)) return invalidate('quickForm', 'quickTarget', 'Nhiệt độ đặt phải từ 30,0 đến 40,0 °C.');
    if (!(interval >= 15 && interval <= 720)) return invalidate('quickForm', 'quickTurn', 'Chu kỳ đảo phải từ 15 đến 720 phút.');
    return true;
  }

  function validateBatchForm() {
    clearInvalid('batchForm');
    if (!$('batchName').value.trim()) return invalidate('batchForm', 'batchName', 'Hãy nhập tên mẻ ấp.');
    if (!$('startDate').value) return invalidate('batchForm', 'startDate', 'Hãy chọn ngày bắt đầu.');
    const days = Number($('totalDays').value);
    const temperature = Number($('batchTarget').value);
    const humidity = Number($('targetHumidity').value);
    if (!(days >= 1 && days <= 40)) return invalidate('batchForm', 'totalDays', 'Tổng số ngày ấp phải từ 1 đến 40 ngày.');
    if (!(temperature >= 30 && temperature <= 40)) return invalidate('batchForm', 'batchTarget', 'Nhiệt độ đặt phải từ 30,0 đến 40,0 °C.');
    if (!(humidity >= 20 && humidity <= 95)) return invalidate('batchForm', 'targetHumidity', 'Độ ẩm tham khảo phải từ 20 đến 95 %RH.');
    return true;
  }

  function validateTemperatureForm() {
    clearInvalid('temperatureForm');
    const target = Number($('targetTemp').value);
    const low = Number($('lowAlarm').value);
    const high = Number($('highAlarm').value);
    const emergency = Number($('emergencyTemp').value);
    const ventOn = Number($('ventOn').value);
    const ventOff = Number($('ventOff').value);
    if (!(target >= 30 && target <= 40)) return invalidate('temperatureForm', 'targetTemp', 'Nhiệt độ đặt phải từ 30,0 đến 40,0 °C.');
    if (!(low < target)) return invalidate('temperatureForm', 'lowAlarm', 'Cảnh báo thấp phải nhỏ hơn nhiệt độ đặt.');
    if (!(high > target)) return invalidate('temperatureForm', 'highAlarm', 'Cảnh báo cao phải lớn hơn nhiệt độ đặt.');
    if (!(emergency > high)) return invalidate('temperatureForm', 'emergencyTemp', 'Ngắt khẩn cấp phải cao hơn cảnh báo cao.');
    if (!(ventOn > ventOff)) return invalidate('temperatureForm', 'ventOn', 'Nhiệt bật thông gió phải cao hơn nhiệt tắt thông gió.');
    return true;
  }

  function validateTurningForm() {
    clearInvalid('turningForm');
    const interval = Number($('turnInterval').value);
    const limit = Number($('limitAlarmTime').value);
    if (!(interval >= 15 && interval <= 720)) return invalidate('turningForm', 'turnInterval', 'Chu kỳ đảo phải từ 15 đến 720 phút.');
    if (!(limit >= 3 && limit <= 120)) return invalidate('turningForm', 'limitAlarmTime', 'Thời gian chờ hành trình phải từ 3 đến 120 giây.');
    return true;
  }

  function validateSensorForm() {
    clearInvalid('sensorForm');
    const temperature = Number($('tempOffset').value);
    const humidity = Number($('humidityOffset').value);
    const timeout = Number($('sensorTimeout').value);
    if (!(temperature >= -10 && temperature <= 10)) return invalidate('sensorForm', 'tempOffset', 'Bù nhiệt độ phải từ −10,0 đến 10,0 °C.');
    if (!(humidity >= -20 && humidity <= 20)) return invalidate('sensorForm', 'humidityOffset', 'Bù độ ẩm phải từ −20 đến 20 %RH.');
    if (!(timeout >= 2 && timeout <= 120)) return invalidate('sensorForm', 'sensorTimeout', 'Timeout cảm biến phải từ 2 đến 120 giây.');
    return true;
  }

  function bindUi() {
    $('confirmCancel').addEventListener('click', () => finishConfirm(false));
    $('confirmAccept').addEventListener('click', () => finishConfirm(true));
    $('confirmDialog').addEventListener('cancel', (event) => { event.preventDefault(); finishConfirm(false); });

    document.querySelectorAll('.settingCard').forEach((card) => card.addEventListener('toggle', () => {
      if (!card.open) return;
      document.querySelectorAll('.settingCard').forEach((other) => { if (other !== card) other.open = false; });
    }));
    document.querySelectorAll('.nav button').forEach((button) => button.addEventListener('click', () => showPage(button.dataset.page)));

    $('deviceSelector').addEventListener('change', (event) => {
      const previous = state.selectedId;
      state.selectedId = event.target.value;
      deactivateSession(previous);
      saveDevices();
      syncSelectedDevice(true);
      toast(`Đã chọn ${currentDevice()?.name || 'thiết bị'}`);
    });

    $('addDeviceBtn').addEventListener('click', () => {
      $('newDeviceId').value = '';
      $('newDeviceName').value = '';
      $('deviceDialog').showModal();
      setTimeout(() => $('newDeviceId').focus(), 60);
    });
    $('closeDeviceDialog').addEventListener('click', () => $('deviceDialog').close());
    $('deviceDialog').addEventListener('click', (event) => {
      if (event.target === $('deviceDialog')) $('deviceDialog').close();
    });
    $('addDeviceForm').addEventListener('submit', (event) => {
      event.preventDefault();
      const id = normalizeDeviceId($('newDeviceId').value);
      const name = $('newDeviceName').value.trim();
      if (!DEVICE_ID_RE.test(id)) return toast('ID phải đúng dạng MAP-A1B2C3D4E5F6');
      if (!name) return toast('Hãy nhập tên hiển thị');
      const existed = state.devices.find((device) => device.id === id);
      if (existed) existed.name = name;
      else state.devices.push(createDevice(id, name));
      const previous = state.selectedId;
      state.selectedId = id;
      deactivateSession(previous);
      renderSelector();
      subscribeDevice(id);
      $('deviceDialog').close();
      toast('Đã thêm thiết bị. Website đang chờ dữ liệu thật.');
    });

    $('quickForm').addEventListener('submit', async (event) => {
      event.preventDefault();
      if (!validateQuickForm()) return;
      $('targetTemp').value = $('quickTarget').value;
      $('batchTarget').value = $('quickTarget').value;
      $('turnInterval').value = $('quickTurn').value;
      updateSettingSummaries();
      await sendConfig('quickForm', 'quick');
    });

    $('batchForm').addEventListener('submit', async (event) => {
      event.preventDefault();
      if (!validateBatchForm()) return;
      $('quickTarget').value = $('batchTarget').value;
      $('targetTemp').value = $('batchTarget').value;
      await sendConfig('batchForm', 'batch');
    });

    $('batchAction').addEventListener('click', async () => {
      const device = currentDevice();
      const running = Boolean(device?.snapshot?.runtime?.batchRunning);

      if (!running) {
        if (hasDirtyForm('batchForm')) return toast('Hãy lưu cấu hình mẻ trước khi bắt đầu');
        if (!validateBatchForm()) return;
        const ok = await confirmAction({
          title: 'Bắt đầu mẻ ấp?',
          message: `Máy sẽ bắt đầu mẻ “${$('batchName').value.trim()}” theo cấu hình đã được ESP32 xác nhận.`,
          accept: 'Bắt đầu mẻ'
        });
        if (!ok) return;
        beginBatchActionPending(device, 'running');
        if (!await sendCommand('batch_start')) {
          clearBatchActionPending(device);
          renderBatchAction(device);
        }
        return;
      }

      const ok = await confirmAction({
        title: 'Kết thúc mẻ ấp?',
        message: 'Thanh nhiệt và cơ cấu đảo sẽ dừng theo trình tự an toàn của firmware.',
        accept: 'Kết thúc mẻ',
        danger: true
      });
      if (!ok) return;
      beginBatchActionPending(device, 'stopped');
      if (!await sendCommand('batch_stop')) {
        clearBatchActionPending(device);
        renderBatchAction(device);
      }
    });

    $('temperatureForm').addEventListener('submit', async (event) => {
      event.preventDefault();
      if (!validateTemperatureForm()) return;
      $('quickTarget').value = $('targetTemp').value;
      $('batchTarget').value = $('targetTemp').value;
      updateSettingSummaries();
      await sendConfig('temperatureForm', 'temperature');
    });

    $('turningForm').addEventListener('submit', async (event) => {
      event.preventDefault();
      if (!validateTurningForm()) return;
      $('quickTurn').value = $('turnInterval').value;
      updateSettingSummaries();
      await sendConfig('turningForm', 'turning');
    });

    $('sensorForm').addEventListener('submit', async (event) => {
      event.preventDefault();
      if (!validateSensorForm()) return;
      updateSettingSummaries();
      await sendConfig('sensorForm', 'sensor');
    });

    $('startTune').addEventListener('click', async () => {
      const runtime = currentDevice()?.snapshot?.runtime;
      if (runtime?.batchRunning) return toast('Không thể Auto Tune khi mẻ đang chạy');
      const ok = await confirmAction({
        title: 'Bắt đầu Auto Tune PID?',
        message: 'Chỉ thực hiện khi khoang ấp trống. Firmware sẽ tự điều khiển và lưu kết quả.',
        accept: 'Bắt đầu Auto Tune'
      });
      if (ok) await sendCommand('autotune_start');
    });

    $('notificationsEnabled').addEventListener('change', async (event) => {
      if (!event.target.checked) {
        localStorage.setItem(`${STORAGE}.notifications`, '0');
        updateSettingSummaries();
        return toast('Đã tắt thông báo trình duyệt');
      }
      if (!('Notification' in window)) {
        event.target.checked = false;
        return toast('Trình duyệt này không hỗ trợ thông báo');
      }
      try {
        const permission = await Notification.requestPermission();
        event.target.checked = permission === 'granted';
      } catch (_) {
        event.target.checked = false;
      }
      localStorage.setItem(`${STORAGE}.notifications`, event.target.checked ? '1' : '0');
      updateSettingSummaries();
      toast(event.target.checked ? 'Đã bật thông báo trình duyệt' : 'Trình duyệt chưa cấp quyền thông báo');
    });

    $('openWifiPortal').addEventListener('click', async () => {
      const ok = await confirmAction({
        title: 'Mở trang đổi Wi‑Fi?',
        message: 'Trước tiên giữ nút BOOT trên ESP32 trong 3 giây, sau đó kết nối điện thoại với mạng MAYAP-XXXX.',
        accept: 'Mở 192.168.4.1'
      });
      if (ok) window.location.href = 'http://192.168.4.1/';
    });

    ['quickForm', 'batchForm', 'temperatureForm', 'turningForm', 'sensorForm'].forEach(registerDirty);
  }

  function startTimers() {
    clearInterval(state.staleTimer);
    state.staleTimer = setInterval(() => renderDevice(), 5000);
  }

  function registerServiceWorker() {
    if (!('serviceWorker' in navigator) || location.protocol === 'file:') return;
    navigator.serviceWorker.register('./sw.js').catch(() => {});
  }

  function init() {
    document.body.dataset.page = 'device';
    $('notificationsEnabled').checked = localStorage.getItem(`${STORAGE}.notifications`) === '1';
    bindUi();
    renderSelector();
    updateSettingSummaries();
    renderBatchLogs();
    setCurrentActivity('Đang kết nối', 'Đang chờ dữ liệu từ ESP32', 'idle');
    connectMqtt();
    startTimers();
    registerServiceWorker();
  }

  window.addEventListener('pagehide', () => {
    deactivateSession(state.selectedId);
    clearInterval(state.sessionTimer);
  });
  window.addEventListener('online', () => {
    if (!state.mqttConnected && state.mqtt) state.mqtt.reconnect();
  });

  init();
})();
