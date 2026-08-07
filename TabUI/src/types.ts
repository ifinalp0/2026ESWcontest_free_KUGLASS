export type VehicleMode = 'driving' | 'stopped' | 'camping' | 'parked';
export type DemoMode = 'none' | 'hot_summer' | 'camping' | 'parked' | 'camera_saturation';
export type OpticalState = 'CLEAR' | 'DIM' | 'FROST';
export type BrandTheme = 'hyundai' | 'konkuk';

export interface ChannelState {
  channel: number;
  name: string;
  targetMi: number;
  commandedMi: number;
  commandedEnable: boolean;
  commandedEnableKnown: boolean;
  appliedMi: number;
  appliedKnown: boolean;
  estimatedTransmittance: number;
  opticalState: OpticalState;
  policyEstimatedTransmittance: number | null;
  policyOpticalState: OpticalState | null;
  appliedSource: string | null;
  masterFault: boolean;
  downstreamFault: boolean;
  fault: boolean;
  manualUntil: number | null;
}

export interface EnvironmentInput {
  internalTemp: number | null;
  frontLeftSaturation: number;
  frontRightSaturation: number;
  edgeDensity: number;
}

export interface CameraMetrics {
  valid?: boolean | null;
  aeMetadataValid?: boolean | null;
  frontLeftSaturation: number;
  frontRightSaturation: number;
  edgeDensity: number;
  glare: number;
  frameId: number;
  timestamp: number;
}

export interface DownstreamAdcChannel {
  channel: number;
  currentRaw: number | null;
  temperatureRaw: number | null;
  currentMv: number | null;
  temperatureMv: number | null;
}

export interface DownstreamAdc {
  initialized: boolean;
  currentCalibrated: boolean;
  temperatureCalibrated: boolean;
  rawValidMask: number;
  mvValidMask: number;
  channels: DownstreamAdcChannel[];
}

export interface DownstreamControlResult {
  command: 'reset_fault';
  seq: number;
  sourceSessionId: number;
  ok: boolean;
  error: string;
}

export interface DownstreamDiagnostics {
  bootId: number | null;
  statusSeq: number | null;
  resetChallenge: number | null;
  estopActive: boolean | null;
  faultCode: string | null;
  diagnostic: string | null;
  operationalFault: boolean;
  controlResult: DownstreamControlResult | null;
  adc: DownstreamAdc;
}

export interface ControllerLink {
  transport: 'usb' | 'mock';
  hardwareConnected: boolean;
  hilEnabled: boolean;
  port: string | null;
  lastTelemetryAt: number | null;
  lastCommandSeq: number;
  lastAckSeq: number | null;
  lastAckCommand: string | null;
  lastAckOk: boolean | null;
  lastAckError: string | null;
  downstreamHealthy: boolean | null;
  downstreamError: string | null;
  downstreamOperationalFault: boolean;
  downstreamBootId: number | null;
  downstreamStatusSeq: number | null;
  downstreamResetChallenge: number | null;
  downstreamEstop: boolean | null;
  downstreamFaultCode: string | null;
  downstreamDiagnostic: string | null;
  downstreamControlResult: DownstreamControlResult | null;
  downstreamAdc: DownstreamAdc;
  error: string | null;
}

export interface ControllerDiagnostics {
  protocolVersion: number | null;
  role: string | null;
  sourceSessionId: number | null;
  downstreamReady: boolean | null;
  firmwareDiagnosticsEnabled: boolean | null;
  stateSeq: number | null;
  thermalRisk: number | null;
}

export interface SimulationState {
  schemaVersion: number;
  vehicleMode: VehicleMode;
  demoMode: DemoMode;
  environment: EnvironmentInput;
  channels: ChannelState[];
  cameraMetrics: CameraMetrics;
  controllerDiagnostics: ControllerDiagnostics;
  downstreamDiagnostics: DownstreamDiagnostics;
  decisionReason: string;
  timestamp: number;
  link: ControllerLink;
}

export interface FastState {
  schemaVersion: number;
  vehicleMode: VehicleMode;
  demoMode: DemoMode;
  channels: ChannelState[];
  timestamp: number;
}

export type ControlCommand =
  | { type: 'setManualChannel'; channel: number; mi: number; enable?: boolean; ttlSeconds?: number }
  | { type: 'returnAuto'; channel?: number }
  | { type: 'setScenario'; demoMode: DemoMode }
  | { type: 'setEnvironment'; environment: Partial<EnvironmentInput> }
  | { type: 'setChannelFault'; channel: number; fault: boolean }
  | { type: 'setCameraStream'; enabled: boolean }
  | { type: 'resetFault' }
  | { type: 'saveReplay'; name?: string }
  | { type: 'loadReplay'; name: string };
