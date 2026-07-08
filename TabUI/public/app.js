const CHANNELS = [
  { id: 0, key: 'front_left', label: '전면 좌측', short: 'CH0', zone: 'front-left', angle: 345, role: '전면 fast-attack' },
  { id: 1, key: 'front_right', label: '전면 우측', short: 'CH1', zone: 'front-right', angle: 15, role: '전면 fast-attack' },
  { id: 2, key: 'left_front_door', label: '좌측 앞문', short: 'CH2', zone: 'left-front', angle: 285, role: '전석 측면' },
  { id: 3, key: 'right_front_door', label: '우측 앞문', short: 'CH3', zone: 'right-front', angle: 75, role: '전석 측면' },
  { id: 4, key: 'left_rear_door', label: '좌측 뒷문', short: 'CH4', zone: 'left-rear', angle: 245, role: '후석 열부하' },
  { id: 5, key: 'right_rear_door', label: '우측 뒷문', short: 'CH5', zone: 'right-rear', angle: 115, role: '후석 열부하' },
  { id: 6, key: 'rear_window', label: '후면 유리', short: 'CH6', zone: 'rear', angle: 180, role: '후면 열부하' },
  { id: 7, key: 'sunroof', label: '선루프', short: 'CH7', zone: 'roof', angle: null, role: '최우선 열부하' }
];

const SCENARIOS = [
  {
    id: 'none',
    label: '기본',
    meta: 'AUTO 복귀',
    command: [{ command: 'return_auto' }],
    story: '기본 주행 상태에서 모든 채널이 필요할 때만 개입한다.'
  },
  {
    id: 'hot_summer',
    label: '열부하',
    meta: '1순위',
    command: [{ command: 'set_mode', mode: 'driving' }, { command: 'set_demo', demo_mode: 'hot_summer' }],
    story: '상부와 후면을 우선 차광해 내부 온도 상승을 줄인다.'
  },
  {
    id: 'camping',
    label: '차박',
    meta: '2순위',
    command: [{ command: 'set_mode', mode: 'camping' }],
    story: '전 채널을 강한 산란 상태로 전환해 외부 시선을 차단한다.'
  },
  {
    id: 'parked',
    label: '주차',
    meta: '도난방지',
    command: [{ command: 'set_mode', mode: 'parked' }],
    story: '시동 OFF 주차 상황에서 실내 물품 노출을 낮춘다.'
  },
  {
    id: 'camera_saturation',
    label: '역광',
    meta: '3순위',
    command: [{ command: 'set_mode', mode: 'driving' }, { command: 'set_demo', demo_mode: 'camera_saturation' }],
    story: 'AE가 감당하기 어려운 전면 순간 강광을 CH0/CH1 fast-attack으로 완화한다.'
  },
  {
    id: 'flashlight_360',
    label: '360도',
    meta: '조도 벡터',
    command: [{ command: 'set_mode', mode: 'driving_stopped' }, { command: 'set_demo', demo_mode: 'flashlight_360' }],
    story: '전·우·후·좌 VEML7700 조도 벡터와 채널 방향성을 시각화한다.'
  }
];

const LABELS = {
  driving: '주행',
  driving_stopped: '정차',
  camping: '차박',
  parked: '주차',
  none: '기본',
  hot_summer: '열부하',
  camera_saturation: '역광',
  flashlight_360: '360도',
  CLEAR: 'CLEAR',
  DIM: 'DIM',
  SCATTER: 'SCATTER',
  OFF: 'OFF'
};

const app = document.querySelector('#app');
const state = {
  selectedChannel: 0,
  manualMi: 0.82,
  lastApiOk: false,
  lastApiAt: 0,
  current: null,
  localScenario: 'none',
  pending: false,
  error: ''
};

state.current = buildMockState('none');

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function pct(value, digits = 0) {
  return `${Math.round(clamp(Number(value) || 0, 0, 1) * 100 * 10 ** digits) / 10 ** digits}%`;
}

function fmt(value, digits = 0, suffix = '') {
  if (value === undefined || value === null || Number.isNaN(Number(value))) {
    return '-';
  }
  return `${Number(value).toFixed(digits)}${suffix}`;
}

function label(value) {
  return LABELS[value] || value || '-';
}

function getActiveScenario(normalized) {
  if (normalized.vehicleMode === 'camping') return 'camping';
  if (normalized.vehicleMode === 'parked') return 'parked';
  return normalized.demoMode || 'none';
}

function stateAgeMs(raw) {
  const ts = Number(raw?.timestamp_ms || raw?.decision?.timestamp_ms || 0);
  return ts > 0 ? Math.max(0, Date.now() - ts) : 0;
}

function flattenTelemetry(telemetry) {
  const byChannel = new Map();
  for (const item of Array.isArray(telemetry) ? telemetry : []) {
    for (const ch of Array.isArray(item?.channels) ? item.channels : []) {
      byChannel.set(Number(ch.channel_id), ch);
    }
  }
  return byChannel;
}

function normalizeState(raw, apiOk) {
  const inputs = raw?.inputs || {};
  const decision = raw?.decision || {};
  const telemetryByChannel = flattenTelemetry(raw?.telemetry);
  const targets = Array.isArray(decision.targets) ? decision.targets : [];
  const targetById = new Map(targets.map((target) => [Number(target.channel_id), target]));
  const channels = CHANNELS.map((spec) => {
    const target = targetById.get(spec.id) || {};
    const telemetry = telemetryByChannel.get(spec.id) || {};
    const mi = Number(telemetry.applied_mi ?? target.target_mi ?? 0.86);
    const targetMi = Number(target.target_mi ?? telemetry.target_mi ?? mi);
    const transmission = Number(target.target_transmission ?? mi);
    return {
      ...spec,
      targetMi: clamp(targetMi, 0, 1),
      appliedMi: clamp(mi, 0, 1),
      transmission: clamp(transmission, 0, 1),
      opticalState: target.optical_state || stateForTransmission(transmission),
      score: clamp(Number(target.score || 0), 0, 1),
      reason: target.reason || '상태 대기',
      fault: Boolean(telemetry.fault || target.fault),
      enable: target.enable !== false && telemetry.enable !== false
    };
  });
  const frontCamera = inputs.front_camera || {};
  const rois = Array.isArray(frontCamera.rois) ? frontCamera.rois : [];
  const lux = inputs.lux || {};
  const glare = inputs.glare || {};
  const weather = inputs.weather || {};
  const vehicleMode = decision.mode || inputs.vehicle_mode || 'driving';
  const demoMode = decision.demo_mode || inputs.demo_mode || 'none';
  const manualOverrides = inputs.manual_overrides || {};
  return {
    raw,
    apiOk,
    stale: apiOk && stateAgeMs(raw) > 3000,
    status: raw?.status || 'ok',
    timestampMs: Number(raw?.timestamp_ms || decision.timestamp_ms || Date.now()),
    vehicleMode,
    demoMode,
    thermalRisk: clamp(Number(decision.thermal_risk || 0), 0, 1),
    privacyNeed: clamp(Number(decision.privacy_need || 0), 0, 1),
    strongFrontLight: Boolean(decision.strong_front_light || glare.strong),
    channels,
    selected: channels.find((ch) => ch.id === state.selectedChannel) || channels[0],
    camera: {
      frameId: frontCamera.frame_id || 0,
      ae: frontCamera.ae || {},
      rois
    },
    glare: {
      left: clamp(Number(glare.left_score || 0), 0, 1),
      right: clamp(Number(glare.right_score || 0), 0, 1),
      total: clamp(Number(glare.total_score || 0), 0, 1),
      dominant: glare.dominant_side || 'center',
      strong: Boolean(glare.strong)
    },
    lux: {
      front: Number(lux.lux_f || 0),
      right: Number(lux.lux_r || 0),
      rear: Number(lux.lux_b || 0),
      left: Number(lux.lux_l || 0),
      theta: Number(lux.theta_deg || 0),
      confidence: clamp(Number(lux.confidence || 0), 0, 1),
      total: Number(lux.total_lux || 0),
      degraded: Boolean(lux.degraded)
    },
    weather: {
      temp: Number(weather.temperature_c || 0),
      uv: Number(weather.uv_index || 0),
      stale: Boolean(weather.stale),
      source: weather.source || 'mock'
    },
    internalTemp: Number(inputs.internal_temp_c || 0),
    manualActive: Object.keys(manualOverrides).length > 0,
    fault: channels.some((channel) => channel.fault)
  };
}

function stateForTransmission(transmission) {
  if (transmission >= 0.74) return 'CLEAR';
  if (transmission >= 0.24) return 'DIM';
  return 'SCATTER';
}

function mockTargetsForScenario(scenarioId, tick) {
  const base = {
    none: [0.88, 0.86, 0.82, 0.82, 0.76, 0.76, 0.72, 0.70],
    hot_summer: [0.72, 0.70, 0.58, 0.58, 0.42, 0.42, 0.34, 0.22],
    camping: [0.08, 0.08, 0.06, 0.06, 0.05, 0.05, 0.05, 0.04],
    parked: [0.10, 0.10, 0.08, 0.08, 0.07, 0.07, 0.06, 0.05],
    camera_saturation: [0.34, 0.70, 0.76, 0.78, 0.74, 0.74, 0.70, 0.70],
    flashlight_360: [0.64, 0.52, 0.70, 0.28, 0.82, 0.38, 0.84, 0.44]
  }[scenarioId] || [];
  if (scenarioId !== 'flashlight_360') {
    return base;
  }
  const theta = (tick * 24) % 360;
  return CHANNELS.map((channel) => {
    if (channel.id === 7) return 0.38;
    const diff = Math.abs(((theta - channel.angle + 540) % 360) - 180);
    const hit = Math.max(0, 1 - diff / 80);
    return clamp(0.88 - hit * 0.72, 0.08, 0.9);
  });
}

function buildMockState(scenarioId = state.localScenario || 'none') {
  const tick = Math.floor(Date.now() / 1000);
  const targets = mockTargetsForScenario(scenarioId, tick);
  const theta = scenarioId === 'flashlight_360' ? (tick * 24) % 360 : scenarioId === 'camera_saturation' ? 350 : 80;
  const luxHit = (angle) => {
    const diff = Math.abs(((theta - angle + 540) % 360) - 180);
    return 650 + Math.max(0, 1 - diff / 90) ** 2 * 14500;
  };
  const vehicleMode =
    scenarioId === 'camping' ? 'camping' : scenarioId === 'parked' ? 'parked' : scenarioId === 'flashlight_360' ? 'driving_stopped' : 'driving';
  const demoMode = ['hot_summer', 'camera_saturation', 'flashlight_360'].includes(scenarioId) ? scenarioId : 'none';
  const saturationLeft = scenarioId === 'camera_saturation' ? 0.64 : 0.16 + Math.sin(tick / 2) * 0.03;
  const saturationRight = scenarioId === 'camera_saturation' ? 0.24 : 0.12;
  const targetsPayload = CHANNELS.map((channel, index) => {
    const mi = targets[index] ?? 0.8;
    return {
      channel_id: channel.id,
      target_transmission: mi,
      target_mi: mi,
      enable: true,
      optical_state: stateForTransmission(mi),
      score: clamp(1 - mi, 0, 1),
      reason:
        scenarioId === 'hot_summer'
          ? 'thermal load priority'
          : scenarioId === 'camera_saturation' && channel.id === 0
            ? 'front fast response: AE glare/saturation'
            : scenarioId === 'flashlight_360'
              ? `directional light theta=${Math.round(theta)}`
              : scenarioId === 'camping' || scenarioId === 'parked'
                ? `${vehicleMode}: privacy/off state`
                : 'clear: no strong glare or thermal need'
    };
  });
  return normalizeState(
    {
      timestamp_ms: Date.now(),
      inputs: {
        vehicle_mode: vehicleMode,
        demo_mode: demoMode,
        internal_temp_c: scenarioId === 'hot_summer' ? 36.4 : 27.8,
        front_camera: {
          frame_id: tick,
          ae: { exposure_us: scenarioId === 'camera_saturation' ? 2400 : 8500, analog_gain: 1, digital_gain: 1, ae_enabled: true },
          rois: [
            {
              name: 'front_left',
              x: 0,
              y: 0,
              w: 160,
              h: 180,
              mean_brightness: 0.74,
              saturation_ratio: saturationLeft,
              edge_density: scenarioId === 'camera_saturation' ? 0.09 : 0.18,
              highlight_area: saturationLeft,
              frame_id: tick,
              timestamp_ms: Date.now()
            },
            {
              name: 'front_right',
              x: 160,
              y: 0,
              w: 160,
              h: 180,
              mean_brightness: 0.42,
              saturation_ratio: saturationRight,
              edge_density: 0.16,
              highlight_area: saturationRight,
              frame_id: tick,
              timestamp_ms: Date.now()
            }
          ]
        },
        glare: {
          left_score: saturationLeft,
          right_score: saturationRight,
          total_score: Math.max(saturationLeft, saturationRight),
          strong: scenarioId === 'camera_saturation',
          dominant_side: saturationLeft > saturationRight + 0.05 ? 'left' : 'center'
        },
        lux: {
          lux_f: luxHit(0),
          lux_r: luxHit(90),
          lux_b: luxHit(180),
          lux_l: luxHit(270),
          theta_deg: theta,
          confidence: scenarioId === 'none' ? 0.34 : 0.82,
          total_lux: luxHit(0) + luxHit(90) + luxHit(180) + luxHit(270),
          degraded: false
        },
        weather: {
          temperature_c: scenarioId === 'hot_summer' ? 33.2 : 26.4,
          uv_index: scenarioId === 'hot_summer' ? 8.1 : 4.2,
          stale: !navigator.onLine,
          source: 'mock'
        },
        manual_overrides: {}
      },
      decision: {
        targets: targetsPayload,
        thermal_risk: scenarioId === 'hot_summer' ? 0.86 : 0.28,
        privacy_need: scenarioId === 'camping' || scenarioId === 'parked' ? 1 : 0,
        strong_front_light: scenarioId === 'camera_saturation',
        mode: vehicleMode,
        demo_mode: demoMode,
        timestamp_ms: Date.now()
      },
      telemetry: []
    },
    false
  );
}

async function fetchState() {
  try {
    const response = await fetch(`/api/state?ts=${Date.now()}`, { cache: 'no-store' });
    if (!response.ok) throw new Error(`state ${response.status}`);
    const raw = await response.json();
    if (raw.status === 'empty' || raw.status === 'corrupt') throw new Error(raw.status);
    state.lastApiOk = true;
    state.lastApiAt = Date.now();
    state.error = '';
    state.current = normalizeState(raw, true);
  } catch (error) {
    state.lastApiOk = false;
    state.error = String(error.message || error);
    state.current = buildMockState(state.localScenario);
  }
  render();
}

async function sendPayload(payload) {
  try {
    const response = await fetch('/api/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    if (!response.ok) throw new Error(`command ${response.status}`);
    return true;
  } catch (error) {
    state.error = String(error.message || error);
    return false;
  }
}

async function runScenario(scenario) {
  state.pending = true;
  state.localScenario = scenario.id;
  render();
  for (const payload of scenario.command) {
    await sendPayload(payload);
  }
  state.pending = false;
  await fetchState();
}

async function setManualChannel(channelId, mi) {
  state.manualMi = mi;
  const ok = await sendPayload({
    command: 'manual_channel',
    channel_id: channelId,
    target_mi: mi,
    ttl_ms: 15000,
    enable: true
  });
  if (!ok) {
    const mock = buildMockState(state.localScenario);
    const target = mock.channels.find((channel) => channel.id === channelId);
    if (target) {
      target.targetMi = mi;
      target.appliedMi = mi;
      target.transmission = mi;
      target.opticalState = stateForTransmission(mi);
      target.reason = 'manual override TTL';
    }
    mock.manualActive = true;
    state.current = mock;
    render();
  }
}

async function returnAuto() {
  await sendPayload({ command: 'return_auto' });
  state.localScenario = 'none';
  await fetchState();
}

function statusPill(normalized) {
  if (normalized.apiOk && !normalized.stale) return '<span class="pill ok">LIVE</span>';
  if (normalized.apiOk && normalized.stale) return '<span class="pill warn">STALE</span>';
  return '<span class="pill mock">MOCK</span>';
}

function render() {
  const normalized = state.current;
  const activeScenario = getActiveScenario(normalized);
  const selected = normalized.channels.find((channel) => channel.id === state.selectedChannel) || normalized.channels[0];
  state.manualMi = selected.targetMi;
  app.innerHTML = `
    <header class="topbar">
      <div class="brand">
        <div class="mark">KG</div>
        <div>
          <h1>KUGLASS 태블릿 시연</h1>
          <p>능동형 스마트 글라스 모빌리티 /demo</p>
        </div>
      </div>
      <div class="top-status">
        ${statusPill(normalized)}
        <span class="pill ${normalized.manualActive ? 'warn' : ''}">${normalized.manualActive ? 'MANUAL TTL' : 'AUTO'}</span>
        <span class="pill ${normalized.fault ? 'danger' : ''}">${normalized.fault ? 'FAULT' : 'FAULT OK'}</span>
        <span class="pill ${normalized.weather.stale ? 'warn' : ''}">Weather ${normalized.weather.stale ? 'cached' : 'live'}</span>
        <span class="pill">AE ${normalized.camera.ae.ae_enabled === false ? 'OFF' : 'ON'}</span>
      </div>
    </header>

    <section class="status-strip">
      ${metricBlock('차량 상황', `${label(normalized.vehicleMode)} / ${label(normalized.demoMode)}`, 'State')}
      ${metricBlock('전면 강광', `${pct(normalized.glare.total)} · ${normalized.glare.dominant}`, normalized.strongFrontLight ? 'Fast attack' : 'Monitor')}
      ${metricBlock('4방향 조도', `${Math.round(normalized.lux.theta)} deg · ${pct(normalized.lux.confidence)}`, 'VEML7700')}
      ${metricBlock('열부하', `${pct(normalized.thermalRisk)} · ${fmt(normalized.internalTemp, 1, 'C')}`, 'Internal')}
      ${metricBlock('날씨', `${fmt(normalized.weather.temp, 1, 'C')} · UV ${fmt(normalized.weather.uv, 1)}`, normalized.weather.source)}
    </section>

    <section class="dashboard">
      <article class="panel vehicle-panel">
        <div class="panel-heading">
          <div>
            <h2>8채널 PDLC 상태</h2>
            <p>선택 채널 highlight · applied MI 기준</p>
          </div>
          <button class="small-button" data-action="return-auto">Auto</button>
        </div>
        ${vehicleView(normalized, selected)}
      </article>

      <article class="panel evidence-panel">
        <div class="panel-heading">
          <div>
            <h2>Camera Evidence</h2>
            <p>AE 유지, ROI 포화/Edge 보존 근거</p>
          </div>
          <span class="frame-id">frame ${normalized.camera.frameId || '-'}</span>
        </div>
        ${cameraEvidence(normalized)}
      </article>

      <article class="panel control-panel">
        <div class="panel-heading">
          <div>
            <h2>시연 제어</h2>
            <p>명령은 CommandQueue로만 전달</p>
          </div>
        </div>
        ${controlPanel(selected, normalized)}
      </article>
    </section>

    <section class="scenario-bar">
      ${SCENARIOS.map((scenario) => scenarioButton(scenario, activeScenario)).join('')}
    </section>

    <footer class="footline">
      <span>${normalized.apiOk ? 'RasPi UI API connected' : 'Offline mock rehearsal'}</span>
      <span>${state.error ? `last note: ${state.error}` : `updated ${new Date(normalized.timestampMs).toLocaleTimeString('ko-KR')}`}</span>
    </footer>
  `;
  bindEvents();
}

function metricBlock(title, value, meta) {
  return `
    <div class="metric">
      <span>${title}</span>
      <strong>${value}</strong>
      <small>${meta}</small>
    </div>
  `;
}

function vehicleView(normalized, selected) {
  const style = `--theta:${normalized.lux.theta}deg; --confidence:${normalized.lux.confidence}`;
  return `
    <div class="vehicle-layout">
      <div class="vehicle-map" style="${style}">
        <div class="lux-compass">
          <span class="bearing north">F</span>
          <span class="bearing east">R</span>
          <span class="bearing south">B</span>
          <span class="bearing west">L</span>
          <span class="lux-arrow"></span>
        </div>
        <div class="car-shell">
          ${normalized.channels.map((channel) => channelZone(channel, selected)).join('')}
          <div class="cabin-line"></div>
          <div class="car-label">1:10 IONIQ5</div>
        </div>
      </div>
      <div class="channel-list">
        ${normalized.channels.map((channel) => channelRow(channel, selected)).join('')}
      </div>
    </div>
  `;
}

function channelZone(channel, selected) {
  const frost = clamp(1 - channel.appliedMi, 0, 1);
  return `
    <button
      class="glass-zone ${channel.zone} ${channel.opticalState.toLowerCase()} ${selected.id === channel.id ? 'selected' : ''}"
      style="--frost:${frost}"
      data-channel="${channel.id}"
      title="${channel.short} ${channel.label}"
    >
      <span>${channel.short}</span>
    </button>
  `;
}

function channelRow(channel, selected) {
  return `
    <button class="channel-row ${selected.id === channel.id ? 'active' : ''}" data-channel="${channel.id}">
      <span class="channel-name">${channel.short} ${channel.label}</span>
      <span class="state-tag ${channel.opticalState.toLowerCase()}">${label(channel.opticalState)}</span>
      <span class="mi-bar"><span style="width:${pct(channel.appliedMi)}"></span></span>
      <span class="mi-value">MI ${fmt(channel.appliedMi, 2)}</span>
    </button>
  `;
}

function cameraEvidence(normalized) {
  const left = normalized.camera.rois.find((roi) => roi.name === 'front_left') || {};
  const right = normalized.camera.rois.find((roi) => roi.name === 'front_right') || {};
  const leftSat = clamp(Number(left.saturation_ratio || 0), 0, 1);
  const rightSat = clamp(Number(right.saturation_ratio || 0), 0, 1);
  const leftEdge = clamp(Number(left.edge_density || 0), 0, 1);
  const rightEdge = clamp(Number(right.edge_density || 0), 0, 1);
  const reason = normalized.selected.reason;
  return `
    <div class="camera-stage">
      <div class="video-feed">
        <div class="road-horizon"></div>
        <div class="hotspot left" style="opacity:${0.25 + leftSat}"></div>
        <div class="hotspot right" style="opacity:${0.18 + rightSat}"></div>
        <div class="roi left ${leftSat > 0.45 ? 'alert' : ''}">
          <span>ROI-L</span>
          <strong>${pct(leftSat)}</strong>
        </div>
        <div class="roi right ${rightSat > 0.45 ? 'alert' : ''}">
          <span>ROI-R</span>
          <strong>${pct(rightSat)}</strong>
        </div>
      </div>
      <div class="evidence-bars">
        ${barLine('좌측 포화', leftSat, leftSat > 0.45 ? 'danger' : '')}
        ${barLine('우측 포화', rightSat, rightSat > 0.45 ? 'danger' : '')}
        ${barLine('좌측 Edge', leftEdge, 'edge')}
        ${barLine('우측 Edge', rightEdge, 'edge')}
      </div>
    </div>
    <div class="reason-box">
      <span>판단 이유</span>
      <strong>${reason}</strong>
      <p>노출 고정 없이 AE 메타데이터, ROI 포화, Edge Density, 4방향 조도 벡터를 함께 표시한다.</p>
    </div>
  `;
}

function barLine(labelText, value, tone = '') {
  return `
    <div class="bar-line">
      <span>${labelText}</span>
      <div class="bar-track ${tone}"><span style="width:${pct(value)}"></span></div>
      <strong>${pct(value)}</strong>
    </div>
  `;
}

function controlPanel(selected, normalized) {
  const frostIntensity = Math.round((1 - selected.targetMi) * 100);
  return `
    <div class="selected-card">
      <span class="eyebrow">선택 채널</span>
      <h3>${selected.short} ${selected.label}</h3>
      <p>${selected.role}</p>
      <div class="selected-stats">
        <span>Target MI <strong>${fmt(selected.targetMi, 2)}</strong></span>
        <span>Applied MI <strong>${fmt(selected.appliedMi, 2)}</strong></span>
        <span>Score <strong>${pct(selected.score)}</strong></span>
      </div>
    </div>
    <label class="slider-block">
      <span>Clear ↔ Frost 수동 TTL</span>
      <input type="range" min="0" max="100" value="${frostIntensity}" data-action="manual-slider" />
      <div class="slider-labels">
        <small>Clear</small>
        <strong>${frostIntensity}% Frost</strong>
        <small>Frost</small>
      </div>
    </label>
    <div class="command-grid">
      <button class="command-button" data-action="manual-clear">Clear TTL</button>
      <button class="command-button" data-action="manual-frost">Frost TTL</button>
      <button class="command-button wide" data-action="return-auto">Return Auto</button>
    </div>
    <div class="lux-panel">
      ${luxCell('전방', normalized.lux.front)}
      ${luxCell('우측', normalized.lux.right)}
      ${luxCell('후방', normalized.lux.rear)}
      ${luxCell('좌측', normalized.lux.left)}
    </div>
    <div class="safety-note">
      <strong>Safety boundary</strong>
      <p>TabUI는 ESP32 또는 전력부를 직접 제어하지 않는다. Fault 중 실제 출력 차단은 control/firmware 계층이 우선한다.</p>
    </div>
  `;
}

function luxCell(name, value) {
  return `
    <div class="lux-cell">
      <span>${name}</span>
      <strong>${Math.round(value).toLocaleString('ko-KR')}</strong>
      <small>lux</small>
    </div>
  `;
}

function scenarioButton(scenario, activeScenario) {
  const active = scenario.id === activeScenario;
  return `
    <button class="scenario-button ${active ? 'active' : ''}" data-scenario="${scenario.id}" ${state.pending ? 'disabled' : ''}>
      <span>${scenario.label}</span>
      <strong>${scenario.meta}</strong>
      <small>${scenario.story}</small>
    </button>
  `;
}

function bindEvents() {
  app.querySelectorAll('[data-channel]').forEach((node) => {
    node.addEventListener('click', () => {
      state.selectedChannel = Number(node.dataset.channel);
      render();
    });
  });
  app.querySelectorAll('[data-scenario]').forEach((node) => {
    node.addEventListener('click', () => {
      const scenario = SCENARIOS.find((item) => item.id === node.dataset.scenario);
      if (scenario) runScenario(scenario);
    });
  });
  app.querySelectorAll('[data-action="return-auto"]').forEach((node) => node.addEventListener('click', returnAuto));
  app.querySelector('[data-action="manual-clear"]')?.addEventListener('click', () => setManualChannel(state.selectedChannel, 0.92));
  app.querySelector('[data-action="manual-frost"]')?.addEventListener('click', () => setManualChannel(state.selectedChannel, 0.08));
  app.querySelector('[data-action="manual-slider"]')?.addEventListener('change', (event) => {
    const frost = Number(event.target.value) / 100;
    setManualChannel(state.selectedChannel, clamp(1 - frost, 0.04, 0.95));
  });
}

if ('serviceWorker' in navigator && location.protocol !== 'file:') {
  navigator.serviceWorker.register('/sw.js').catch(() => {});
}

render();
fetchState();
setInterval(fetchState, 600);
