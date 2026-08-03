export type VehicleMode = 'driving' | 'stopped' | 'camping' | 'parked';
export type DemoMode = 'none' | 'hot_summer' | 'camping' | 'parked' | 'camera_saturation';
export type OpticalState = 'CLEAR' | 'DIM' | 'FROST';
export type BrandTheme = 'hyundai' | 'konkuk';

export interface ChannelState {
  channel: number;
  name: string;
  targetMi: number;
  commandedMi: number;
  appliedMi: number;
  appliedKnown: boolean;
  estimatedTransmittance: number;
  opticalState: OpticalState;
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
  frontLeftSaturation: number;
  frontRightSaturation: number;
  edgeDensity: number;
  glare: number;
  frameId: number;
  timestamp: number;
}

export interface ControllerLink {
  transport: 'serial' | 'mock';
  hardwareConnected: boolean;
  hilEnabled: boolean;
  port: string | null;
  lastTelemetryAt: number | null;
  lastCommandSeq: number;
  lastAckSeq: number | null;
  downstreamHealthy: boolean | null;
  downstreamError: string | null;
  error: string | null;
}

export interface SimulationState {
  schemaVersion: number;
  vehicleMode: VehicleMode;
  demoMode: DemoMode;
  environment: EnvironmentInput;
  channels: ChannelState[];
  cameraMetrics: CameraMetrics;
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
  | { type: 'setManualChannel'; channel: number; mi: number; ttlSeconds?: number }
  | { type: 'returnAuto'; channel?: number }
  | { type: 'setScenario'; demoMode: DemoMode }
  | { type: 'setEnvironment'; environment: Partial<EnvironmentInput> }
  | { type: 'setChannelFault'; channel: number; fault: boolean }
  | { type: 'resetFault' }
  | { type: 'saveReplay'; name?: string }
  | { type: 'loadReplay'; name: string };
