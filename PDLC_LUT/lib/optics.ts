export type Roi = {
  x: number;
  y: number;
  width: number;
  height: number;
};

export type RegionStats = {
  red: number;
  green: number;
  blue: number;
  luma: number;
  contrast: number;
  saturationRatio: number;
  lowClipRatio: number;
  pixels: number;
};

export type RegionPair = {
  reference: RegionStats;
  sample: RegionStats;
};

export type OpticalEstimate = {
  transmission: number;
  clarity: number;
  referenceSignal: number;
  sampleSignal: number;
  saturationRatio: number;
  lowClipRatio: number;
  valid: boolean;
  warnings: string[];
};

export type LutPoint = {
  id: string;
  channel: number;
  /** PDLC_LUT의 정규화된 조절값(0..1). */
  mi: number;
  /** ESP32_A wire protocol에 전송된 실제 controller MI(0..0.60). */
  controllerMi: number;
  direction: "up" | "down" | "manual";
  transmission: number;
  clarity: number;
  repeatability: number;
  valid: boolean;
  timestamp: string;
  source: "camera" | "demo";
};

export type NormalizedLutPoint = {
  mi: number;
  controllerMi: number;
  transmission: number;
  opticalResponse: number;
};

const EMPTY_REGION: RegionStats = {
  red: 0,
  green: 0,
  blue: 0,
  luma: 0,
  contrast: 0,
  saturationRatio: 0,
  lowClipRatio: 1,
  pixels: 0,
};

function clamp(value: number, minimum: number, maximum: number) {
  return Math.min(maximum, Math.max(minimum, value));
}

function median(values: number[]) {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle];
}

function percentile(sorted: number[], ratio: number) {
  if (sorted.length === 0) return 0;
  const index = clamp(ratio, 0, 1) * (sorted.length - 1);
  const low = Math.floor(index);
  const high = Math.ceil(index);
  if (low === high) return sorted[low];
  return sorted[low] + (sorted[high] - sorted[low]) * (index - low);
}

function gammaDecode(value: number, gamma: number) {
  return Math.pow(clamp(value / 255, 0, 1), gamma);
}

export function analyzeRegion(image: ImageData, roi: Roi, gamma: number): RegionStats {
  const x0 = clamp(Math.floor(roi.x * image.width), 0, image.width - 1);
  const y0 = clamp(Math.floor(roi.y * image.height), 0, image.height - 1);
  const x1 = clamp(Math.ceil((roi.x + roi.width) * image.width), x0 + 1, image.width);
  const y1 = clamp(Math.ceil((roi.y + roi.height) * image.height), y0 + 1, image.height);

  let red = 0;
  let green = 0;
  let blue = 0;
  let luma = 0;
  let saturated = 0;
  let lowClipped = 0;
  let pixels = 0;
  const lumaSamples: number[] = [];

  // Sampling every second pixel keeps VGA analysis light while retaining
  // enough spatial samples for a stable median and contrast estimate.
  for (let y = y0; y < y1; y += 2) {
    for (let x = x0; x < x1; x += 2) {
      const offset = (y * image.width + x) * 4;
      const rawRed = image.data[offset];
      const rawGreen = image.data[offset + 1];
      const rawBlue = image.data[offset + 2];
      const linearRed = gammaDecode(rawRed, gamma);
      const linearGreen = gammaDecode(rawGreen, gamma);
      const linearBlue = gammaDecode(rawBlue, gamma);
      const linearLuma =
        0.2126 * linearRed + 0.7152 * linearGreen + 0.0722 * linearBlue;

      red += linearRed;
      green += linearGreen;
      blue += linearBlue;
      luma += linearLuma;
      lumaSamples.push(linearLuma);
      if (rawRed >= 250 || rawGreen >= 250 || rawBlue >= 250) saturated += 1;
      if (rawRed <= 5 && rawGreen <= 5 && rawBlue <= 5) lowClipped += 1;
      pixels += 1;
    }
  }

  if (pixels === 0) return EMPTY_REGION;
  lumaSamples.sort((a, b) => a - b);
  const low = percentile(lumaSamples, 0.1);
  const high = percentile(lumaSamples, 0.9);
  const contrast = high + low > 1e-6 ? (high - low) / (high + low) : 0;

  return {
    red: red / pixels,
    green: green / pixels,
    blue: blue / pixels,
    luma: luma / pixels,
    contrast,
    saturationRatio: saturated / pixels,
    lowClipRatio: lowClipped / pixels,
    pixels,
  };
}

export function analyzeFrame(
  image: ImageData,
  referenceRoi: Roi,
  sampleRoi: Roi,
  gamma: number,
): RegionPair {
  return {
    reference: analyzeRegion(image, referenceRoi, gamma),
    sample: analyzeRegion(image, sampleRoi, gamma),
  };
}

function aggregateRegion(regions: RegionStats[]): RegionStats {
  if (regions.length === 0) return EMPTY_REGION;
  const field = (key: keyof RegionStats) => median(regions.map((region) => region[key]));
  return {
    red: field("red"),
    green: field("green"),
    blue: field("blue"),
    luma: field("luma"),
    contrast: field("contrast"),
    saturationRatio: field("saturationRatio"),
    lowClipRatio: field("lowClipRatio"),
    pixels: Math.round(field("pixels")),
  };
}

export function aggregatePairs(pairs: RegionPair[]): RegionPair {
  return {
    reference: aggregateRegion(pairs.map((pair) => pair.reference)),
    sample: aggregateRegion(pairs.map((pair) => pair.sample)),
  };
}

export function estimateOptics(
  current: RegionPair,
  dark: RegionPair,
  blank: RegionPair,
): OpticalEstimate {
  const referenceSignal = current.reference.green - dark.reference.green;
  const sampleSignal = current.sample.green - dark.sample.green;
  const blankReference = blank.reference.green - dark.reference.green;
  const blankSample = blank.sample.green - dark.sample.green;
  const blankRatio = blankReference > 1e-6 ? blankSample / blankReference : 0;
  const currentRatio = referenceSignal > 1e-6 ? sampleSignal / referenceSignal : 0;
  const transmission = blankRatio > 1e-6 ? currentRatio / blankRatio : 0;

  const blankClarityRatio = blank.reference.contrast > 1e-6
    ? blank.sample.contrast / blank.reference.contrast
    : 0;
  const currentClarityRatio = current.reference.contrast > 1e-6
    ? current.sample.contrast / current.reference.contrast
    : 0;
  const clarity = blankClarityRatio > 1e-6
    ? currentClarityRatio / blankClarityRatio
    : 0;

  const saturationRatio = Math.max(
    current.reference.saturationRatio,
    current.sample.saturationRatio,
  );
  const lowClipRatio = Math.max(
    current.reference.lowClipRatio,
    current.sample.lowClipRatio,
  );
  const warnings: string[] = [];
  if (referenceSignal <= 0.015) warnings.push("기준 ROI 신호가 너무 낮습니다.");
  if (sampleSignal <= 0.003) warnings.push("PDLC ROI 신호가 암부에 묻혔습니다.");
  if (saturationRatio > 0.02) warnings.push("포화 픽셀이 2%를 넘습니다.");
  if (lowClipRatio > 0.2) warnings.push("암부 클리핑이 20%를 넘습니다.");
  if (!Number.isFinite(transmission) || transmission < 0) {
    warnings.push("상대 투과 계산이 유효하지 않습니다.");
  }

  return {
    transmission: Number.isFinite(transmission) ? Math.max(0, transmission) : 0,
    clarity: Number.isFinite(clarity) ? Math.max(0, clarity) : 0,
    referenceSignal: Math.max(0, referenceSignal),
    sampleSignal: Math.max(0, sampleSignal),
    saturationRatio,
    lowClipRatio,
    valid: warnings.length === 0,
    warnings,
  };
}

export function summarizeEstimates(estimates: OpticalEstimate[]) {
  const valid = estimates.filter((estimate) => Number.isFinite(estimate.transmission));
  if (valid.length === 0) {
    return { estimate: null, repeatability: 0 };
  }
  const transmissions = valid.map((estimate) => estimate.transmission);
  const center = median(transmissions);
  const variance = transmissions.reduce((sum, value) => sum + (value - center) ** 2, 0)
    / transmissions.length;
  const template = valid[Math.floor(valid.length / 2)];
  const warnings = [...new Set(valid.flatMap((estimate) => estimate.warnings))];
  return {
    estimate: {
      ...template,
      transmission: center,
      clarity: median(valid.map((estimate) => estimate.clarity)),
      referenceSignal: median(valid.map((estimate) => estimate.referenceSignal)),
      sampleSignal: median(valid.map((estimate) => estimate.sampleSignal)),
      saturationRatio: median(valid.map((estimate) => estimate.saturationRatio)),
      lowClipRatio: median(valid.map((estimate) => estimate.lowClipRatio)),
      valid: valid.every((estimate) => estimate.valid),
      warnings,
    } satisfies OpticalEstimate,
    repeatability: Math.sqrt(variance),
  };
}

function isotonicIncreasing(values: number[]) {
  const blocks = values.map((value, index) => ({ start: index, end: index, sum: value, count: 1 }));
  let cursor = 0;
  while (cursor < blocks.length - 1) {
    const left = blocks[cursor];
    const right = blocks[cursor + 1];
    if (left.sum / left.count <= right.sum / right.count) {
      cursor += 1;
      continue;
    }
    left.end = right.end;
    left.sum += right.sum;
    left.count += right.count;
    blocks.splice(cursor + 1, 1);
    if (cursor > 0) cursor -= 1;
  }
  const output = new Array(values.length).fill(0);
  for (const block of blocks) {
    const mean = block.sum / block.count;
    for (let index = block.start; index <= block.end; index += 1) output[index] = mean;
  }
  return output;
}

export function buildNormalizedLut(points: LutPoint[], channel: number): NormalizedLutPoint[] {
  const buckets = new Map<number, number[]>();
  for (const point of points) {
    if (!point.valid || point.source !== "camera" || point.channel !== channel) continue;
    const key = Number(point.mi.toFixed(4));
    const values = buckets.get(key) ?? [];
    values.push(point.transmission);
    buckets.set(key, values);
  }
  const measurements = [...buckets.entries()]
    .map(([mi, values]) => ({ mi, transmission: median(values) }))
    .sort((a, b) => a.mi - b.mi);
  if (measurements.length < 2) return [];

  const fitted = isotonicIncreasing(measurements.map((measurement) => measurement.transmission));
  const minimum = fitted[0];
  const maximum = fitted[fitted.length - 1];
  const span = maximum - minimum;
  if (span <= 1e-6) return [];
  return measurements.map((measurement, index) => ({
    mi: measurement.mi,
    controllerMi: clamp(measurement.mi * 0.6, 0, 0.6),
    transmission: fitted[index],
    opticalResponse: clamp((fitted[index] - minimum) / span, 0, 1),
  }));
}

export function createDemoPoints(channel: number): LutPoint[] {
  return Array.from({ length: 21 }, (_, index) => {
    const mi = index * 0.05;
    const controllerMi = mi * 0.6;
    const response = 0.12 + 0.83 / (1 + Math.exp(-12 * (controllerMi - 0.31)));
    return {
      id: `demo-${channel}-${index}`,
      channel,
      mi,
      controllerMi,
      direction: "up" as const,
      transmission: response,
      clarity: Math.max(0.04, (response - 0.08) / 0.88),
      repeatability: 0.006 + (index % 3) * 0.002,
      valid: true,
      timestamp: new Date().toISOString(),
      source: "demo" as const,
    };
  });
}
