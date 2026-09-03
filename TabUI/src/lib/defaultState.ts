import type { DownstreamAdc, SimulationState } from '../types';
import { channelLabels } from './labels';
import { MAX_MI } from './mi';

function defaultDownstreamAdc(): DownstreamAdc {
  return {
    initialized: false,
    currentCalibrated: false,
    temperatureCalibrated: false,
    rawValidMask: 0,
    mvValidMask: 0,
    channels: channelLabels.map((_, channel) => ({
      channel,
      currentRaw: null,
      temperatureRaw: null,
      currentMv: null,
      temperatureMv: null,
      temperatureNominalC: null
    }))
  };
}

export const defaultState: SimulationState = {
  schemaVersion: 1,
  vehicleMode: 'driving',
  demoMode: 'none',
  environment: {
    internalTemp: null,
    internalTempOverride: false,
    frontLeftSaturation: 0.08,
    frontRightSaturation: 0.07,
    edgeDensity: 0.86
  },
  channels: channelLabels.map((name, channel) => ({
    channel,
    name,
    targetMi: MAX_MI,
    commandedMi: MAX_MI,
    commandedEnable: false,
    commandedEnableKnown: false,
    appliedMi: 0,
    appliedKnown: false,
    estimatedTransmittance: 0.12,
    opticalState: 'FROST',
    policyEstimatedTransmittance: null,
    policyOpticalState: null,
    appliedSource: null,
    masterFault: false,
    downstreamFault: false,
    fault: false,
    manualUntil: null,
    manualPersistent: false
  })),
  cameraMetrics: {
    valid: null,
    aeMetadataValid: null,
    frontLeftSaturation: 0.08,
    frontRightSaturation: 0.07,
    edgeDensity: 0.86,
    glare: 0,
    frameId: 0,
    timestamp: 0
  },
  controllerDiagnostics: {
    protocolVersion: null,
    role: null,
    sourceSessionId: null,
    downstreamReady: null,
    firmwareDiagnosticsEnabled: null,
    stateSeq: null,
    thermalRisk: null
  },
  downstreamDiagnostics: {
    bootId: null,
    statusSeq: null,
    resetChallenge: null,
    estopActive: null,
    faultCode: null,
    diagnostic: null,
    operationalFault: false,
    controlResult: null,
    adc: defaultDownstreamAdc()
  },
  decisionReason: 'TabUI 백엔드와 ESP32_A 텔레메트리를 기다리는 중입니다.',
  timestamp: 0,
  link: {
    backendRunning: false,
    transport: 'usb',
    hardwareConnected: false,
    hilEnabled: false,
    port: null,
    lastTelemetryAt: null,
    lastCommandSeq: 0,
    lastAckSeq: null,
    lastAckCommand: null,
    lastAckOk: null,
    lastAckError: null,
    downstreamHealthy: null,
    downstreamError: null,
    downstreamOperationalFault: false,
    downstreamBootId: null,
    downstreamStatusSeq: null,
    downstreamResetChallenge: null,
    downstreamEstop: null,
    downstreamFaultCode: null,
    downstreamDiagnostic: null,
    downstreamControlResult: null,
    downstreamAdc: defaultDownstreamAdc(),
    error: null
  }
};
