import type { SimulationState } from '../types';

const channelNames = [
  'CH0 Front Left',
  'CH1 Front Right',
  'CH2 Left Front Door',
  'CH3 Right Front Door',
  'CH4 Left Rear Door',
  'CH5 Right Rear Door',
  'CH6 Rear Glass',
  'CH7 Sunroof'
];

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
  channels: channelNames.map((name, channel) => ({
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
  decisionReason: 'Backend not connected yet. UI is showing default MOCK state.',
  timestamp: 0
};
