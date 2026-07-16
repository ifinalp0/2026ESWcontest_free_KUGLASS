import type {
  CameraMetrics,
  ChannelState,
  ControlCommand,
  DemoMode,
  EnvironmentInput,
  OpticalState,
  SimulationState,
  VehicleMode
} from '../types';

interface ChannelConfig {
  channel: number;
  bearingDeg: number | null;
  visibilityFloor: number;
}

interface PolicyResult {
  targets: Record<number, number>;
  reason: string;
  fastAttackChannels: Set<number>;
}

const CHANNEL_CONFIGS: ChannelConfig[] = [
  { channel: 0, bearingDeg: 340, visibilityFloor: 0.58 },
  { channel: 1, bearingDeg: 20, visibilityFloor: 0.5 },
  { channel: 2, bearingDeg: 290, visibilityFloor: 0.34 },
  { channel: 3, bearingDeg: 70, visibilityFloor: 0.34 },
  { channel: 4, bearingDeg: 235, visibilityFloor: 0.18 },
  { channel: 5, bearingDeg: 125, visibilityFloor: 0.18 },
  { channel: 6, bearingDeg: 180, visibilityFloor: 0.16 },
  { channel: 7, bearingDeg: null, visibilityFloor: 0.12 }
];

const POLICY = {
  clearMi: 0.95,
  campingMi: 0.04,
  parkedMi: 0.03,
  frontGlareThreshold: 0.62,
  frontLeftSaturationThreshold: 0.7,
  frontRightSaturationThreshold: 0.7,
  frontBiasMargin: 0.08,
  ch0GlareMi: 0.42,
  ch1GlareMi: 0.38,
  flashlightMinMi: 0.05
};

const THERMAL = {
  trigger: 0.05,
  reasonThreshold: 0.25,
  tempBaseC: 28,
  tempSpanC: 12,
  luxBase: 550,
  luxSpan: 1900,
  topLuxBase: 280,
  topLuxSpan: 820,
  demoBoost: 0.35,
  tempWeight: 0.42,
  luxWeight: 0.38,
  topWeight: 0.2,
  channelAmount: [0.15, 0.18, 0.36, 0.36, 0.5, 0.5, 0.58, 0.7]
};

const DIRECTIONAL = {
  confidenceGate: 0.12,
  reasonConfidence: 0.2,
  confidenceScale: 2.4,
  maxAngleDeg: 100,
  angleScale: 0.9,
  frontAmount: 0.2,
  frontDoorAmount: 0.42,
  rearAmount: 0.5
};

const SERVO = {
  snapEpsilon: 0.003,
  fastAttackRate: 1.25,
  frostRate: 0.45,
  clearRate: 0.28
};

const CAMERA = {
  dimmingGain: 0.52,
  edgeLossGain: 0.1,
  glareFrontLuxBase: 650,
  glareFrontLuxSpan: 2200
};

const FLASHLIGHT = {
  confidenceFloor: 0.35,
  channelAmount: 0.84,
  topLuxBase: 250,
  topLuxSpan: 850,
  topAmount: 0.78,
  baseLux: 110,
  strengthLux: 980
};

function nowSeconds() {
  return Date.now() / 1000;
}

function clamp(value: number, lower = 0, upper = 1) {
  return Math.max(lower, Math.min(upper, value));
}

function round3(value: number) {
  return Math.round(value * 1000) / 1000;
}

function safeLux(value: number | null) {
  return Math.max(0, value ?? 0);
}

function baseEnvironment(): EnvironmentInput {
  return {
    frontLux: 280,
    rightLux: 180,
    rearLux: 140,
    leftLux: 170,
    topLux: 260,
    internalTemp: 27,
    weatherTemp: 28,
    frontLeftSaturation: 0.08,
    frontRightSaturation: 0.07,
    edgeDensity: 0.86
  };
}

function estimatedTransmittance(mi: number) {
  return round3(0.12 + 0.83 * (clamp(mi) ** 1.18));
}

function opticalState(mi: number): OpticalState {
  if (mi >= 0.72) {
    return 'CLEAR';
  }
  if (mi >= 0.3) {
    return 'DIM';
  }
  return 'FROST';
}

function withOptics(channel: ChannelState, appliedMi = channel.appliedMi): ChannelState {
  return {
    ...channel,
    appliedMi: round3(clamp(appliedMi)),
    targetMi: round3(clamp(channel.targetMi)),
    estimatedTransmittance: estimatedTransmittance(appliedMi),
    opticalState: opticalState(appliedMi)
  };
}

function clearTargets() {
  const targets: Record<number, number> = {};
  CHANNEL_CONFIGS.forEach((config) => {
    targets[config.channel] = POLICY.clearMi;
  });
  return targets;
}

function uniformTargets(mi: number) {
  const targets: Record<number, number> = {};
  CHANNEL_CONFIGS.forEach((config) => {
    targets[config.channel] = mi;
  });
  return targets;
}

function angularDistance(a: number, b: number) {
  return Math.abs((a - b + 180) % 360 - 180);
}

function angularHit(channelBearing: number, sourceBearing: number) {
  const distance = angularDistance(channelBearing, sourceBearing);
  if (distance >= DIRECTIONAL.maxAngleDeg) {
    return 0;
  }
  return clamp(Math.cos(distance * DIRECTIONAL.angleScale * Math.PI / 180));
}

function lightBearingAndConfidence(environment: EnvironmentInput): [number, number] {
  const x = safeLux(environment.rightLux) - safeLux(environment.leftLux);
  const y = safeLux(environment.frontLux) - safeLux(environment.rearLux);
  const magnitude = Math.hypot(x, y);
  const total = safeLux(environment.frontLux) + safeLux(environment.rightLux) + safeLux(environment.rearLux) + safeLux(environment.leftLux);
  if (total <= 1 || magnitude <= 1) {
    return [0, 0];
  }
  return [
    (Math.atan2(x, y) * 180 / Math.PI + 360) % 360,
    clamp(magnitude / Math.max(total, 1) * DIRECTIONAL.confidenceScale)
  ];
}

function thermalRisk(environment: EnvironmentInput, demoMode: DemoMode) {
  const luxTotal = safeLux(environment.frontLux) + safeLux(environment.rightLux) + safeLux(environment.rearLux) + safeLux(environment.leftLux) + safeLux(environment.topLux);
  const tempComponent = clamp((Math.max(environment.internalTemp, environment.weatherTemp) - THERMAL.tempBaseC) / THERMAL.tempSpanC);
  const luxComponent = clamp((luxTotal - THERMAL.luxBase) / THERMAL.luxSpan);
  const topComponent = clamp((safeLux(environment.topLux) - THERMAL.topLuxBase) / THERMAL.topLuxSpan);
  const boost = demoMode === 'hot_summer' ? THERMAL.demoBoost : 0;
  return clamp(
    THERMAL.tempWeight * tempComponent
    + THERMAL.luxWeight * luxComponent
    + THERMAL.topWeight * topComponent
    + boost
  );
}

function frontGlare(environment: EnvironmentInput, demoMode: DemoMode): [number, number, number] {
  const left = clamp(environment.frontLeftSaturation);
  const right = clamp(environment.frontRightSaturation);
  const frontLux = clamp((safeLux(environment.frontLux) - 520) / 1050);
  let glare = clamp(Math.max(left, right) * 0.75 + frontLux * 0.25);
  if (demoMode === 'camera_saturation') {
    glare = Math.max(glare, 0.88);
  }
  return [left, right, glare];
}

function vehicleModeForDemo(demoMode: DemoMode): VehicleMode {
  if (demoMode === 'camping') {
    return 'camping';
  }
  if (demoMode === 'parked') {
    return 'parked';
  }
  return 'driving';
}

function computePolicy(state: SimulationState): PolicyResult {
  const targets = clearTargets();
  const fastAttackChannels = new Set<number>();

  if (state.demoMode === 'camping' || state.vehicleMode === 'camping') {
    return {
      targets: uniformTargets(POLICY.campingMi),
      reason: '차박 프라이버시: 전 채널을 최저 투명도에 가까운 산란 상태로 전환합니다.',
      fastAttackChannels
    };
  }

  if (state.demoMode === 'parked' || state.vehicleMode === 'parked') {
    return {
      targets: uniformTargets(POLICY.parkedMi),
      reason: '주차 도난방지·열부하: 전 채널을 최대 불투명에 가깝게 유지합니다.',
      fastAttackChannels
    };
  }

  if (state.demoMode === 'flashlight_360') {
    return computeFlashlightPolicy(state.environment);
  }

  const risk = thermalRisk(state.environment, state.demoMode);
  if (risk > THERMAL.trigger) {
    THERMAL.channelAmount.forEach((amount, channel) => {
      targets[channel] = Math.min(targets[channel], POLICY.clearMi - amount * risk);
    });
  }

  const [bearing, confidence] = lightBearingAndConfidence(state.environment);
  if (confidence > DIRECTIONAL.confidenceGate) {
    CHANNEL_CONFIGS.forEach((config) => {
      if (config.bearingDeg === null) {
        return;
      }
      const hit = angularHit(config.bearingDeg, bearing) * confidence;
      const amount = config.channel <= 1
        ? DIRECTIONAL.frontAmount
        : config.channel <= 3
          ? DIRECTIONAL.frontDoorAmount
          : DIRECTIONAL.rearAmount;
      targets[config.channel] = Math.min(targets[config.channel], POLICY.clearMi - amount * hit);
    });
  }

  const [leftSat, rightSat, glare] = frontGlare(state.environment, state.demoMode);
  const strongFrontLight = glare >= POLICY.frontGlareThreshold
    || leftSat >= POLICY.frontLeftSaturationThreshold
    || rightSat >= POLICY.frontRightSaturationThreshold;
  if (strongFrontLight) {
    if (leftSat >= rightSat - POLICY.frontBiasMargin) {
      targets[0] = Math.min(targets[0], POLICY.ch0GlareMi);
      fastAttackChannels.add(0);
    }
    if (rightSat >= leftSat - POLICY.frontBiasMargin) {
      targets[1] = Math.min(targets[1], POLICY.ch1GlareMi);
      fastAttackChannels.add(1);
    }
  }

  if (state.vehicleMode === 'driving') {
    CHANNEL_CONFIGS.forEach((config) => {
      targets[config.channel] = Math.max(targets[config.channel], config.visibilityFloor);
    });
  }

  const reasonParts: string[] = [];
  if (risk > THERMAL.reasonThreshold) {
    reasonParts.push('열부하 우선순위 CH7->CH6->CH4/5->CH2/3->CH0/1 적용');
  }
  if (confidence > DIRECTIONAL.reasonConfidence) {
    reasonParts.push(`방향성 조도 벡터 ${bearing.toFixed(0)}도 방향 감지`);
  }
  if (strongFrontLight) {
    reasonParts.push('전면 순간 강광 fast-attack으로 CH0/CH1 산란 개입');
  }
  if (reasonParts.length === 0) {
    reasonParts.push('주행 기본 상태로 유리부를 투명에 가깝게 유지');
  }

  return {
    targets: Object.fromEntries(Object.entries(targets).map(([channel, mi]) => [channel, round3(clamp(mi))])),
    reason: `${reasonParts.join('; ')}.`,
    fastAttackChannels
  };
}

function computeFlashlightPolicy(environment: EnvironmentInput): PolicyResult {
  const [bearing, measuredConfidence] = lightBearingAndConfidence(environment);
  const confidence = Math.max(measuredConfidence, FLASHLIGHT.confidenceFloor);
  const targets = clearTargets();

  CHANNEL_CONFIGS.forEach((config) => {
    if (config.bearingDeg === null) {
      const topIntensity = clamp((safeLux(environment.topLux) - FLASHLIGHT.topLuxBase) / FLASHLIGHT.topLuxSpan);
      targets[config.channel] = POLICY.clearMi - FLASHLIGHT.topAmount * topIntensity;
      return;
    }
    targets[config.channel] = POLICY.clearMi - FLASHLIGHT.channelAmount * angularHit(config.bearingDeg, bearing) * confidence;
  });

  return {
    targets: Object.fromEntries(
      Object.entries(targets).map(([channel, mi]) => [channel, round3(clamp(mi, POLICY.flashlightMinMi, POLICY.clearMi))])
    ),
    reason: `360도 손전등: ${bearing.toFixed(0)}도 방향의 mock 조도 벡터를 기준으로 가장 가까운 유리부터 산란 개입합니다.`,
    fastAttackChannels: new Set([0, 1])
  };
}

function servoChannel(channel: ChannelState, targetMi: number, dt: number, fastAttack: boolean) {
  if (channel.fault) {
    return 0;
  }

  const delta = targetMi - channel.appliedMi;
  if (Math.abs(delta) < SERVO.snapEpsilon) {
    return targetMi;
  }

  const movingToFrost = delta < 0;
  const rate = fastAttack && movingToFrost
    ? SERVO.fastAttackRate
    : movingToFrost
      ? SERVO.frostRate
      : SERVO.clearRate;
  const maxDelta = rate * dt;
  return channel.appliedMi + Math.sign(delta) * Math.min(Math.abs(delta), maxDelta);
}

function cameraMetrics(state: SimulationState, channels: ChannelState[], now: number): CameraMetrics {
  const environment = state.environment;
  const frontMi = ((channels[0]?.appliedMi ?? POLICY.clearMi) + (channels[1]?.appliedMi ?? POLICY.clearMi)) / 2;
  const dimmingEffect = clamp((POLICY.clearMi - frontMi) / 0.75);
  const leftSat = clamp(environment.frontLeftSaturation * (1 - CAMERA.dimmingGain * dimmingEffect));
  const rightSat = clamp(environment.frontRightSaturation * (1 - CAMERA.dimmingGain * dimmingEffect));
  const edge = clamp(environment.edgeDensity - CAMERA.edgeLossGain * dimmingEffect);
  const glare = clamp(
    Math.max(leftSat, rightSat)
    + Math.max(0, safeLux(environment.frontLux) - CAMERA.glareFrontLuxBase) / CAMERA.glareFrontLuxSpan
  );

  return {
    frontLeftSaturation: round3(leftSat),
    frontRightSaturation: round3(rightSat),
    edgeDensity: round3(edge),
    glare: round3(glare),
    frameId: state.cameraMetrics.frameId + 1,
    timestamp: now
  };
}

function manualReasonSuffix(reason: string, channels: ChannelState[], now: number) {
  const active = channels
    .filter((channel) => channel.manualUntil !== null)
    .map((channel) => `CH${channel.channel} ${Math.max(0, (channel.manualUntil ?? now) - now).toFixed(0)}초 후 자동 복귀`);
  if (active.length === 0) {
    return reason;
  }
  return `${reason} 수동 오버라이드 적용 중: ${active.join(', ')}.`;
}

function mergeEnvironment(environment: EnvironmentInput, updates: Partial<EnvironmentInput>) {
  const next: EnvironmentInput = { ...environment };
  const assignable = next as unknown as Record<keyof EnvironmentInput, number | null>;
  (Object.keys(updates) as Array<keyof EnvironmentInput>).forEach((key) => {
    const value = updates[key];
    if (value === undefined || (value === null && (key === 'internalTemp' || key === 'weatherTemp'))) {
      return;
    }
    assignable[key] = value;
  });
  return next;
}

function scenarioEnvironment(demoMode: DemoMode): EnvironmentInput {
  if (demoMode === 'hot_summer') {
    return {
      frontLux: 760,
      rightLux: 620,
      rearLux: 540,
      leftLux: 600,
      topLux: 980,
      internalTemp: 39,
      weatherTemp: 36,
      frontLeftSaturation: 0.18,
      frontRightSaturation: 0.16,
      edgeDensity: 0.84
    };
  }
  if (demoMode === 'camping') {
    return {
      frontLux: 80,
      rightLux: 90,
      rearLux: 70,
      leftLux: 85,
      topLux: 50,
      internalTemp: 24,
      weatherTemp: 22,
      frontLeftSaturation: 0.08,
      frontRightSaturation: 0.07,
      edgeDensity: 0.72
    };
  }
  if (demoMode === 'parked') {
    return {
      frontLux: 500,
      rightLux: 460,
      rearLux: 410,
      leftLux: 420,
      topLux: 760,
      internalTemp: 33,
      weatherTemp: 32,
      frontLeftSaturation: 0.08,
      frontRightSaturation: 0.07,
      edgeDensity: 0.76
    };
  }
  if (demoMode === 'camera_saturation') {
    return {
      frontLux: 1180,
      rightLux: 220,
      rearLux: 120,
      leftLux: 340,
      topLux: 240,
      internalTemp: 28,
      weatherTemp: 29,
      frontLeftSaturation: 0.9,
      frontRightSaturation: 0.36,
      edgeDensity: 0.83
    };
  }
  if (demoMode === 'flashlight_360') {
    return flashlightEnvironment(0);
  }
  return baseEnvironment();
}

function flashlightEnvironment(angleDeg: number): EnvironmentInput {
  const angle = angleDeg * Math.PI / 180;
  const front = FLASHLIGHT.baseLux + FLASHLIGHT.strengthLux * Math.max(0, Math.cos(angle));
  const right = FLASHLIGHT.baseLux + FLASHLIGHT.strengthLux * Math.max(0, Math.sin(angle));
  const rear = FLASHLIGHT.baseLux + FLASHLIGHT.strengthLux * Math.max(0, -Math.cos(angle));
  const left = FLASHLIGHT.baseLux + FLASHLIGHT.strengthLux * Math.max(0, -Math.sin(angle));

  return {
    frontLux: Math.round(front * 10) / 10,
    rightLux: Math.round(right * 10) / 10,
    rearLux: Math.round(rear * 10) / 10,
    leftLux: Math.round(left * 10) / 10,
    topLux: 210,
    internalTemp: 26,
    weatherTemp: 27,
    frontLeftSaturation: clamp((front - 650) / 720),
    frontRightSaturation: clamp((front - 760) / 760),
    edgeDensity: 0.86
  };
}

export function stepOfflineMock(state: SimulationState, at = nowSeconds(), preferredDt?: number): SimulationState {
  const previousTimestamp = state.timestamp > 0 ? state.timestamp : at - 0.1;
  const dt = preferredDt ?? clamp(at - previousTimestamp, 0.05, 0.3);
  const policy = computePolicy(state);

  const channels = state.channels.map((channel) => {
    const manualActive = channel.manualUntil !== null && channel.manualUntil > at;
    const targetMi = manualActive ? channel.targetMi : policy.targets[channel.channel] ?? POLICY.clearMi;
    const appliedMi = servoChannel(channel, targetMi, dt, policy.fastAttackChannels.has(channel.channel));
    return withOptics({
      ...channel,
      targetMi: round3(targetMi),
      manualUntil: manualActive ? channel.manualUntil : null
    }, appliedMi);
  });

  const faultedChannels = channels.filter((channel) => channel.fault).map((channel) => `CH${channel.channel}`);
  const faultReason = faultedChannels.length > 0
    ? ` 구동기 고장 ${faultedChannels.join(', ')}은 fail-safe 산란 상태입니다.`
    : '';
  const reason = `오프라인 MOCK 정책: ${manualReasonSuffix(policy.reason, channels, at)}${faultReason}`;
  return {
    ...state,
    channels,
    cameraMetrics: cameraMetrics(state, channels, at),
    decisionReason: reason,
    timestamp: at
  };
}

export function applyOfflineMockCommand(state: SimulationState, command: ControlCommand): SimulationState {
  const at = nowSeconds();

  if (command.type === 'setManualChannel') {
    const channelId = Math.max(0, Math.min(state.channels.length - 1, Math.trunc(command.channel)));
    const mi = round3(clamp(command.mi));
    const ttl = Math.max(0, command.ttlSeconds ?? 15);
    const channels = state.channels.map((channel) => (
      channel.channel === channelId
        ? withOptics({ ...channel, targetMi: mi, manualUntil: at + ttl }, mi)
        : channel
    ));
    const next = {
      ...state,
      channels,
      cameraMetrics: cameraMetrics(state, channels, at),
      decisionReason: `오프라인 MOCK 정책: CH${channelId} 수동 투명도 ${Math.round(mi * 100)}% 적용, ${Math.round(ttl)}초 후 자동 복귀.`,
      timestamp: at
    };
    return stepOfflineMock(next, at, 0.05);
  }

  if (command.type === 'returnAuto') {
    const channels = state.channels.map((channel) => {
      if (command.channel === undefined || command.channel === channel.channel) {
        return { ...channel, manualUntil: null };
      }
      return channel;
    });
    return stepOfflineMock({ ...state, channels }, at, 0.18);
  }

  if (command.type === 'setScenario') {
    const demoMode = command.demoMode;
    const channels = state.channels.map((channel) => ({ ...channel, manualUntil: null }));
    return stepOfflineMock({
      ...state,
      demoMode,
      vehicleMode: vehicleModeForDemo(demoMode),
      environment: scenarioEnvironment(demoMode),
      channels
    }, at, 0.18);
  }

  if (command.type === 'setFlashlightAngle') {
    const angle = ((command.angleDeg % 360) + 360) % 360;
    return stepOfflineMock({
      ...state,
      demoMode: 'flashlight_360',
      vehicleMode: vehicleModeForDemo('flashlight_360'),
      environment: flashlightEnvironment(angle)
    }, at, 0.18);
  }

  if (command.type === 'setEnvironment') {
    return stepOfflineMock({
      ...state,
      environment: mergeEnvironment(state.environment, command.environment)
    }, at, 0.16);
  }

  if (command.type === 'setChannelFault') {
    const channelId = Math.max(0, Math.min(state.channels.length - 1, Math.trunc(command.channel)));
    const channels = state.channels.map((channel) => (
      channel.channel === channelId
        ? { ...channel, fault: command.fault, manualUntil: command.fault ? null : channel.manualUntil }
        : channel
    ));
    return stepOfflineMock({
      ...state,
      channels,
      decisionReason: command.fault
        ? `오프라인 MOCK 정책: CH${channelId} 구동기 고장을 주입해 fail-safe 산란 상태를 검증합니다.`
        : `오프라인 MOCK 정책: CH${channelId} 구동기 고장을 해제하고 자동 정책으로 복귀합니다.`
    }, at, 0.1);
  }

  if (command.type === 'resetFault') {
    const channels = state.channels.map((channel) => ({ ...channel, fault: false }));
    return stepOfflineMock({ ...state, channels }, at, 0.1);
  }

  if (command.type === 'saveReplay') {
    return {
      ...state,
      decisionReason: '오프라인 MOCK 정책: 로컬 오프라인 상태에서는 리플레이 파일 저장 없이 현재 상태를 유지합니다.',
      timestamp: at
    };
  }

  return {
    ...state,
    decisionReason: '오프라인 MOCK 정책: 로컬 오프라인 상태에서는 리플레이 불러오기를 사용할 수 없습니다.',
    timestamp: at
  };
}
