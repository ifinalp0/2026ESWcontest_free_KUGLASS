export type VehicleMode = 'driving' | 'stopped' | 'camping' | 'parked';
export type DemoMode = 'none' | 'hot_summer' | 'camping' | 'parked' | 'camera_saturation' | 'flashlight_360';
export type OpticalState = 'CLEAR' | 'DIM' | 'FROST';

export interface ChannelState {
  channel: number;
  name: string;
  targetMi: number;
  appliedMi: number;
  estimatedTransmittance: number;
  opticalState: OpticalState;
  fault: boolean;
  manualUntil: number | null;
}

export interface EnvironmentInput {
  frontLux: number | null;
  rightLux: number | null;
  rearLux: number | null;
  leftLux: number | null;
  topLux: number | null;
  internalTemp: number;
  weatherTemp: number;
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

export interface SimulationState {
  schemaVersion: number;
  vehicleMode: VehicleMode;
  demoMode: DemoMode;
  environment: EnvironmentInput;
  channels: ChannelState[];
  cameraMetrics: CameraMetrics;
  decisionReason: string;
  timestamp: number;
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
  | { type: 'setFlashlightAngle'; angleDeg: number }
  | { type: 'setEnvironment'; environment: Partial<EnvironmentInput> }
  | { type: 'resetFault' }
  | { type: 'saveReplay'; name?: string }
  | { type: 'loadReplay'; name: string };
