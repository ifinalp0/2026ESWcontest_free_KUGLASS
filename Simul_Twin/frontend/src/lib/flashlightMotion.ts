export function normalizeAngle(angle: number) {
  return ((angle % 360) + 360) % 360;
}

export function signedAngleDelta(from: number, to: number) {
  return ((normalizeAngle(to) - normalizeAngle(from) + 540) % 360) - 180;
}

export function unwrapAngle(previous: number, next: number) {
  return previous + signedAngleDelta(previous, next);
}
