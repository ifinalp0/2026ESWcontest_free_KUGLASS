import type { EnvironmentInput } from '../types';

export const EXTERNAL_DEMO_TEMPERATURE_MIN_C = 30;
export const EXTERNAL_DEMO_TEMPERATURE_MAX_C = 50;

export function displayedInternalTemperature(environment: EnvironmentInput): number | null {
  return environment.internalTemp !== null && Number.isFinite(environment.internalTemp)
    ? environment.internalTemp
    : null;
}

export function randomExternalDemoTemperature(): number {
  const range = EXTERNAL_DEMO_TEMPERATURE_MAX_C - EXTERNAL_DEMO_TEMPERATURE_MIN_C + 1;
  return EXTERNAL_DEMO_TEMPERATURE_MIN_C + Math.floor(Math.random() * range);
}
