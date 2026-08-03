import type { SimulationState } from '../types';
import { channelLabels } from './labels';

export const defaultState: SimulationState = {
  schemaVersion: 1,
  vehicleMode: 'driving',
  demoMode: 'none',
  environment: {
    internalTemp: 27,
    frontLeftSaturation: 0.08,
    frontRightSaturation: 0.07,
    edgeDensity: 0.86
  },
  channels: channelLabels.map((name, channel) => ({
    channel,
    name,
    targetMi: 0.95,
    commandedMi: 0.95,
    appliedMi: 0,
    appliedKnown: false,
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
  decisionReason: 'TabUI 백엔드와 ESP32_A 텔레메트리를 기다리는 중입니다.',
  timestamp: 0,
  link: {
    transport: 'serial',
    hardwareConnected: false,
    hilEnabled: false,
    port: null,
    lastTelemetryAt: null,
    lastCommandSeq: 0,
    lastAckSeq: null,
    downstreamHealthy: null,
    downstreamError: null,
    error: null
  }
};
