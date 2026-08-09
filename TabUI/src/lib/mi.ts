export const MAX_MI = 0.7;

export function clampMi(value: number): number {
  return Math.max(0, Math.min(MAX_MI, Number.isFinite(value) ? value : 0));
}

export function normalizedMi(value: number): number {
  return clampMi(value) / MAX_MI;
}
