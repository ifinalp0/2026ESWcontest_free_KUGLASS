import type { DemoMode, EnvironmentInput, OpticalState, VehicleMode } from '../types';

export const channelLabels = [
  'CH0 전면 좌측',
  'CH1 전면 우측',
  'CH2 좌측 전방 도어',
  'CH3 우측 전방 도어',
  'CH4 좌측 후방 도어',
  'CH5 우측 후방 도어',
  'CH6 후면 유리',
  'CH7 선루프'
];

export function channelDisplayName(name: string) {
  return name.replace(/^ch0*\d+\s*/i, '');
}

export const opticalStateLabels: Record<OpticalState, string> = {
  CLEAR: '투명',
  DIM: '중간 산란',
  FROST: '강산란'
};

export const vehicleModeLabels: Record<VehicleMode, string> = {
  driving: '주행',
  stopped: '정차',
  camping: '차박',
  parked: '주차'
};

export const demoModeLabels: Record<DemoMode, string> = {
  none: '기본',
  hot_summer: '열부하 경감',
  camping: '차박 프라이버시',
  parked: '주차 도난방지',
  camera_saturation: '강한 역광',
  flashlight_360: '360° 손전등'
};

export function pct(value: number) {
  return `${Math.round(value * 100)}%`;
}

function luxVector(environment: EnvironmentInput): [number, number] {
  const vectorX = environment.rightLux !== null && environment.leftLux !== null
    ? environment.rightLux - environment.leftLux
    : 0;
  const vectorY = environment.frontLux !== null && environment.rearLux !== null
    ? environment.frontLux - environment.rearLux
    : 0;
  return [vectorX, vectorY];
}

export function hasLuxBearing(environment: EnvironmentInput) {
  const [vectorX, vectorY] = luxVector(environment);
  return Math.hypot(vectorX, vectorY) > 1;
}

export function luxBearing(environment: EnvironmentInput) {
  const [vectorX, vectorY] = luxVector(environment);
  return (Math.atan2(vectorX, vectorY) * 180 / Math.PI + 360) % 360;
}
