import type { SimulationState } from '../types';
import { channelLabels } from './labels';

export const defaultState: SimulationState = {
  schemaVersion: 1,
  vehicleMode: 'driving',
  demoMode: 'none',
  environment: {
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
  },
  channels: channelLabels.map((name, channel) => ({
    channel,
    name,
    targetMi: 0.95,
    appliedMi: 0.95,
    estimatedTransmittance: 0.92,
    opticalState: 'CLEAR',
    fault: false,
    manualUntil: null
  })),
  cameraMetrics: {
    frontLeftSaturation: 0.08,
    frontRightSaturation: 0.07,
    edgeDensity: 0.86,
    glare: 0,
    frameId: 0,
    timestamp: 0
  },
  decisionReason: '백엔드 연결 전입니다. UI는 기본 MOCK 상태를 표시합니다.',
  timestamp: 0
};
