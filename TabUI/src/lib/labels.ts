import type { DemoMode, OpticalState, VehicleMode } from '../types';

export const channelLabels = [
  'CH0 운전석 창문',
  'CH1 조수석 창문·선루프',
  'CH2 운전석 옆 창문',
  'CH3 조수석 옆 창문'
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
  camera_saturation: '강한 역광'
};

export function pct(value: number) {
  return `${Math.round(value * 100)}%`;
}
