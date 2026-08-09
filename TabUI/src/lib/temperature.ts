import type { EnvironmentInput } from '../types';

// Temporary presentation-only override. Sensor telemetry and ESP32_A policy input
// remain unchanged; explicit MOCK/HIL temperature overrides still display normally.
export const TEMPORARY_MEASURED_TEMPERATURE_C = 26;
export const EXTERNAL_DEMO_TEMPERATURE_MIN_C = 30;
export const EXTERNAL_DEMO_TEMPERATURE_MAX_C = 50;

export function displayedInternalTemperature(environment: EnvironmentInput): number {
  return environment.internalTempOverride && environment.internalTemp !== null
    ? environment.internalTemp
    : TEMPORARY_MEASURED_TEMPERATURE_C;
}

export function randomExternalDemoTemperature(): number {
  const range = EXTERNAL_DEMO_TEMPERATURE_MAX_C - EXTERNAL_DEMO_TEMPERATURE_MIN_C + 1;
  return EXTERNAL_DEMO_TEMPERATURE_MIN_C + Math.floor(Math.random() * range);
}
