"use client";

import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import {
  aggregatePairs,
  analyzeFrame,
  buildNormalizedLut,
  createDemoPoints,
  estimateOptics,
  summarizeEstimates,
  type LutPoint,
  type OpticalEstimate,
  type RegionPair,
  type Roi,
} from "../lib/optics";
import {
  Esp32SerialClient,
  webSerialSupported,
  type CameraFrame,
} from "../lib/esp32-serial";

type ConnectionState = "idle" | "connecting" | "connected" | "error" | "unsupported";
type FrameRecord = { id: number; pair: RegionPair };

type Settings = {
  channel: number;
  startMi: number;
  endMi: number;
  stepMi: number;
  settleMs: number;
  framesPerPoint: number;
  gamma: number;
};

const DEFAULT_REFERENCE_ROI: Roi = { x: 0.12, y: 0.24, width: 0.28, height: 0.48 };
const DEFAULT_SAMPLE_ROI: Roi = { x: 0.6, y: 0.24, width: 0.28, height: 0.48 };
const DEFAULT_SETTINGS: Settings = {
  channel: 0,
  startMi: 0,
  endMi: 1,
  stepMi: 0.05,
  settleMs: 1500,
  framesPerPoint: 7,
  gamma: 2.2,
};

const LAB_MI_MAX = 1;
const CONTROLLER_MI_MAX = 0.6;
const STORAGE_KEY = "kuglass-pdlc-lut-session-v2";
const LEGACY_STORAGE_KEY = "kuglass-pdlc-lut-session-v1";

function clamp(value: number, minimum: number, maximum: number) {
  return Math.min(maximum, Math.max(minimum, value));
}

function finiteOr(value: unknown, fallback: number) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : fallback;
}

function toControllerMi(labMi: number) {
  return Number((clamp(labMi, 0, LAB_MI_MAX) * CONTROLLER_MI_MAX).toFixed(6));
}

function fromControllerMi(controllerMi: number) {
  return clamp(controllerMi / CONTROLLER_MI_MAX, 0, LAB_MI_MAX);
}

function sanitizeSettings(value: unknown, legacyControllerScale = false): Settings {
  const raw = value && typeof value === "object" ? value as Partial<Settings> : {};
  const scale = legacyControllerScale ? 1 / CONTROLLER_MI_MAX : 1;
  const first = clamp(finiteOr(raw.startMi, DEFAULT_SETTINGS.startMi) * scale, 0, LAB_MI_MAX);
  const second = clamp(finiteOr(raw.endMi, DEFAULT_SETTINGS.endMi) * scale, 0, LAB_MI_MAX);
  return {
    channel: Math.round(clamp(finiteOr(raw.channel, DEFAULT_SETTINGS.channel), 0, 3)),
    startMi: Math.min(first, second),
    endMi: Math.max(first, second),
    stepMi: clamp(finiteOr(raw.stepMi, DEFAULT_SETTINGS.stepMi) * scale, 0.01, 0.25),
    settleMs: Math.round(clamp(finiteOr(raw.settleMs, DEFAULT_SETTINGS.settleMs), 500, 5000)),
    framesPerPoint: Math.round(clamp(finiteOr(raw.framesPerPoint, DEFAULT_SETTINGS.framesPerPoint), 1, 15)),
    gamma: clamp(finiteOr(raw.gamma, DEFAULT_SETTINGS.gamma), 1, 3),
  };
}

function sanitizeRoi(value: unknown, fallback: Roi): Roi {
  const raw = value && typeof value === "object" ? value as Partial<Roi> : {};
  const x = clamp(finiteOr(raw.x, fallback.x), 0, 0.95);
  const y = clamp(finiteOr(raw.y, fallback.y), 0, 0.95);
  return {
    x,
    y,
    width: clamp(finiteOr(raw.width, fallback.width), 0.05, 1 - x),
    height: clamp(finiteOr(raw.height, fallback.height), 0.05, 1 - y),
  };
}

function sanitizeLutPoint(
  value: unknown,
  fallbackId: string,
  channelOverride?: number,
  sourceOverride?: LutPoint["source"],
  legacyControllerScale = false,
): LutPoint | null {
  if (!value || typeof value !== "object") return null;
  const raw = value as Partial<LutPoint>;
  const channel = channelOverride ?? finiteOr(raw.channel, -1);
  const storedMi = finiteOr(raw.mi, -1);
  const mi = legacyControllerScale ? fromControllerMi(storedMi) : storedMi;
  const transmission = finiteOr(raw.transmission, -1);
  const clarity = finiteOr(raw.clarity, -1);
  const repeatability = finiteOr(raw.repeatability, -1);
  const direction = raw.direction;
  const source = sourceOverride ?? raw.source;
  if (!Number.isInteger(channel) || channel < 0 || channel > 3) return null;
  if (mi < 0 || mi > LAB_MI_MAX) return null;
  if (transmission < 0 || transmission > 100) return null;
  if (clarity < 0 || clarity > 100 || repeatability < 0 || repeatability > 100) return null;
  if (direction !== "up" && direction !== "down" && direction !== "manual") return null;
  if (source !== "camera" && source !== "demo") return null;
  if (typeof raw.valid !== "boolean") return null;
  return {
    id: typeof raw.id === "string" && raw.id ? raw.id : fallbackId,
    channel,
    mi,
    controllerMi: toControllerMi(mi),
    direction,
    transmission,
    clarity,
    repeatability,
    valid: raw.valid,
    timestamp: typeof raw.timestamp === "string" && Number.isFinite(Date.parse(raw.timestamp))
      ? raw.timestamp
      : new Date().toISOString(),
    source,
  };
}

function roisOverlap(left: Roi, right: Roi) {
  return left.x < right.x + right.width
    && left.x + left.width > right.x
    && left.y < right.y + right.height
    && left.y + left.height > right.y;
}

function formatPercent(value: number | null, digits = 1) {
  return value === null || !Number.isFinite(value) ? "—" : `${(value * 100).toFixed(digits)}%`;
}

function sleep(milliseconds: number) {
  return new Promise((resolve) => window.setTimeout(resolve, milliseconds));
}

function buildSweepValues(start: number, end: number, step: number) {
  const values: number[] = [];
  const safeStep = clamp(step, 0.01, 0.25);
  for (let value = start; value <= end + 1e-6; value += safeStep) {
    values.push(Number(Math.min(value, end).toFixed(4)));
  }
  if (values.at(-1) !== end) values.push(Number(end.toFixed(4)));
  return values;
}

function median(values: number[]) {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle];
}

function downloadText(filename: string, content: string, type: string) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

function makePoint(
  estimate: OpticalEstimate,
  repeatability: number,
  channel: number,
  mi: number,
  direction: LutPoint["direction"],
): LutPoint {
  return {
    id: `${Date.now()}-${channel}-${mi}-${direction}-${Math.random().toString(16).slice(2)}`,
    channel,
    mi,
    controllerMi: toControllerMi(mi),
    direction,
    transmission: estimate.transmission,
    clarity: estimate.clarity,
    repeatability,
    valid: estimate.valid && repeatability <= 0.03,
    timestamp: new Date().toISOString(),
    source: "camera",
  };
}

export function LutLab() {
  const [connection, setConnection] = useState<ConnectionState>("idle");
  const [connectionMessage, setConnectionMessage] = useState("ESP32_A 연결 대기");
  const [statusMessage, setStatusMessage] = useState("암전과 무시료 기준을 순서대로 저장하세요.");
  const [frameInfo, setFrameInfo] = useState<{ sequence: number; width: number; height: number } | null>(null);
  const [latestPair, setLatestPair] = useState<RegionPair | null>(null);
  const [dark, setDark] = useState<RegionPair | null>(null);
  const [blank, setBlank] = useState<RegionPair | null>(null);
  const [referenceRoi, setReferenceRoi] = useState<Roi>(DEFAULT_REFERENCE_ROI);
  const [sampleRoi, setSampleRoi] = useState<Roi>(DEFAULT_SAMPLE_ROI);
  const [settings, setSettings] = useState<Settings>(DEFAULT_SETTINGS);
  const [manualMi, setManualMi] = useState(0.5);
  const [points, setPoints] = useState<LutPoint[]>([]);
  const [safetyArmed, setSafetyArmed] = useState(false);
  const [busy, setBusy] = useState(false);
  const [sweepProgress, setSweepProgress] = useState({ current: 0, total: 0, label: "" });
  const [lastState, setLastState] = useState<Record<string, unknown> | null>(null);
  const [mounted, setMounted] = useState(false);
  const downstream = lastState?.downstream as Record<string, unknown> | undefined;
  const downstreamHealthy = downstream?.healthy === true;
  const roiOverlap = roisOverlap(referenceRoi, sampleRoi);

  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const chartRef = useRef<HTMLCanvasElement | null>(null);
  const clientRef = useRef<Esp32SerialClient | null>(null);
  const frameRecordsRef = useRef<FrameRecord[]>([]);
  const frameIdRef = useRef(0);
  const latestPairRef = useRef<RegionPair | null>(null);
  const referenceRoiRef = useRef(referenceRoi);
  const sampleRoiRef = useRef(sampleRoi);
  const gammaRef = useRef(settings.gamma);
  const connectionRef = useRef(connection);
  const darkRef = useRef<RegionPair | null>(dark);
  const blankRef = useRef<RegionPair | null>(blank);
  const sequenceRef = useRef(1);
  const pendingAcksRef = useRef(new Map<number, {
    resolve: () => void;
    reject: (error: Error) => void;
    timer: number;
  }>());
  const stopSweepRef = useRef(false);
  const streamLeaseTimerRef = useRef<number | null>(null);

  useEffect(() => { referenceRoiRef.current = referenceRoi; }, [referenceRoi]);
  useEffect(() => { sampleRoiRef.current = sampleRoi; }, [sampleRoi]);
  useEffect(() => { gammaRef.current = settings.gamma; }, [settings.gamma]);
  useEffect(() => { connectionRef.current = connection; }, [connection]);
  useEffect(() => { darkRef.current = dark; }, [dark]);
  useEffect(() => { blankRef.current = blank; }, [blank]);

  useEffect(() => {
    /* eslint-disable react-hooks/set-state-in-effect -- one-time hydration from browser-owned storage and capabilities */
    try {
      const currentSaved = localStorage.getItem(STORAGE_KEY);
      const saved = currentSaved ?? localStorage.getItem(LEGACY_STORAGE_KEY);
      const legacyControllerScale = !currentSaved && Boolean(saved);
      if (saved) {
        const parsed = JSON.parse(saved) as Record<string, unknown>;
        if (Array.isArray(parsed.points)) {
          const restored = parsed.points
            .map((point, index) => sanitizeLutPoint(
              point,
              `restored-${index}`,
              undefined,
              undefined,
              legacyControllerScale,
            ))
            .filter((point): point is LutPoint => point !== null);
          setPoints(restored);
        }
        setSettings(sanitizeSettings(parsed.settings, legacyControllerScale));
        setReferenceRoi(sanitizeRoi(parsed.referenceRoi, DEFAULT_REFERENCE_ROI));
        setSampleRoi(sanitizeRoi(parsed.sampleRoi, DEFAULT_SAMPLE_ROI));
      }
    } catch {
      setStatusMessage("저장된 로컬 세션을 읽지 못해 새 측정을 시작합니다.");
    }
    setMounted(true);
    if (!webSerialSupported()) {
      setConnection("unsupported");
      setConnectionMessage("Web Serial 미지원 · 이미지 업로드 사용");
    }
    /* eslint-enable react-hooks/set-state-in-effect */
  }, []);

  useEffect(() => {
    if (!mounted) return;
    localStorage.setItem(STORAGE_KEY, JSON.stringify({
      points,
      settings,
      referenceRoi,
      sampleRoi,
    }));
  }, [mounted, points, referenceRoi, sampleRoi, settings]);

  const nextSequence = useCallback(() => {
    sequenceRef.current = (sequenceRef.current + 1) >>> 0;
    if (sequenceRef.current === 0) sequenceRef.current = 1;
    return sequenceRef.current;
  }, []);

  const sendCommand = useCallback(async (
    command: Record<string, unknown>,
    waitForAck = true,
  ) => {
    const client = clientRef.current;
    if (!client || connectionRef.current !== "connected") {
      throw new Error("ESP32_A가 연결되지 않았습니다.");
    }
    const seq = nextSequence();
    const record = { v: 1, type: "ui_command", seq, ...command };
    if (!waitForAck) {
      await client.send(record);
      return;
    }
    const ack = new Promise<void>((resolve, reject) => {
      const timer = window.setTimeout(() => {
        pendingAcksRef.current.delete(seq);
        reject(new Error(`명령 ACK timeout (seq ${seq})`));
      }, 3000);
      pendingAcksRef.current.set(seq, { resolve, reject, timer });
    });
    try {
      await client.send(record);
    } catch (error) {
      const pending = pendingAcksRef.current.get(seq);
      if (pending) window.clearTimeout(pending.timer);
      pendingAcksRef.current.delete(seq);
      throw error;
    }
    await ack;
  }, [nextSequence]);

  const handleLine = useCallback((line: string) => {
    try {
      const record = JSON.parse(line) as Record<string, unknown>;
      if (record.type === "ack" && typeof record.seq === "number") {
        const pending = pendingAcksRef.current.get(record.seq);
        if (pending) {
          window.clearTimeout(pending.timer);
          pendingAcksRef.current.delete(record.seq);
          if (record.ok === true) pending.resolve();
          else pending.reject(new Error(String(record.error ?? "ESP32_A가 명령을 거부했습니다.")));
        }
      }
      if (record.type === "state") setLastState(record);
      if (record.type === "protocol_error") {
        setStatusMessage(`ESP32_A protocol error: ${String(record.error ?? "UNKNOWN")}`);
      }
    } catch {
      // ROM boot banners and diagnostic logs are not JSON records.
    }
  }, []);

  const decodeFrame = useCallback(async (frame: CameraFrame) => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const clone = new Uint8Array(frame.jpeg.length);
    clone.set(frame.jpeg);
    const bitmap = await createImageBitmap(new Blob([clone.buffer], { type: frame.mimeType ?? "image/jpeg" }));
    canvas.width = bitmap.width;
    canvas.height = bitmap.height;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    if (!context) return;
    context.drawImage(bitmap, 0, 0);
    bitmap.close();
    const image = context.getImageData(0, 0, canvas.width, canvas.height);
    const pair = analyzeFrame(
      image,
      referenceRoiRef.current,
      sampleRoiRef.current,
      gammaRef.current,
    );
    const id = ++frameIdRef.current;
    latestPairRef.current = pair;
    frameRecordsRef.current.push({ id, pair });
    if (frameRecordsRef.current.length > 240) frameRecordsRef.current.splice(0, 80);
    setLatestPair(pair);
    setFrameInfo({ sequence: frame.sequence, width: frame.width, height: frame.height });
  }, []);

  const renewCameraLease = useCallback(async () => {
    try {
      await sendCommand({ command: "camera_stream", enable: true, ttl_ms: 15000 }, false);
    } catch (error) {
      setStatusMessage(error instanceof Error ? error.message : "카메라 lease 갱신 실패");
    }
  }, [sendCommand]);

  const disconnect = useCallback(async () => {
    if (streamLeaseTimerRef.current !== null) {
      window.clearInterval(streamLeaseTimerRef.current);
      streamLeaseTimerRef.current = null;
    }
    try {
      if (connectionRef.current === "connected") {
        await sendCommand({ command: "camera_stream", enable: false, ttl_ms: 15000 }, false);
      }
    } catch {
      // A disconnected port cannot accept the best-effort lease release.
    }
    await clientRef.current?.disconnect();
    clientRef.current = null;
    for (const pending of pendingAcksRef.current.values()) {
      window.clearTimeout(pending.timer);
      pending.reject(new Error("ESP32_A USB 연결이 종료되었습니다."));
    }
    pendingAcksRef.current.clear();
    connectionRef.current = "idle";
    setConnection("idle");
    setConnectionMessage("ESP32_A 연결 대기");
  }, [sendCommand]);

  useEffect(() => () => {
    if (streamLeaseTimerRef.current !== null) window.clearInterval(streamLeaseTimerRef.current);
    void clientRef.current?.disconnect();
  }, []);

  const connect = useCallback(async () => {
    if (!webSerialSupported()) {
      setConnection("unsupported");
      setConnectionMessage("Web Serial 미지원 · 이미지 업로드 사용");
      return;
    }
    setConnection("connecting");
    setConnectionMessage("USB 장치 선택 중");
    try {
      const client = new Esp32SerialClient(
        (frame) => void decodeFrame(frame),
        handleLine,
        (message) => {
          setConnection("error");
          setConnectionMessage("USB 연결 오류");
          setStatusMessage(message);
        },
      );
      clientRef.current = client;
      await client.connect();
      sequenceRef.current = (Date.now() >>> 0) || 1;
      setConnection("connected");
      connectionRef.current = "connected";
      setConnectionMessage("ESP32_A USB 연결됨");
      setStatusMessage("KUGLCAM1 영상 lease를 요청했습니다.");
      await sendCommand({ command: "camera_stream", enable: true, ttl_ms: 15000 });
      streamLeaseTimerRef.current = window.setInterval(() => void renewCameraLease(), 10000);
    } catch (error) {
      await clientRef.current?.disconnect();
      clientRef.current = null;
      setConnection("error");
      setConnectionMessage("USB 연결 실패");
      setStatusMessage(error instanceof Error ? error.message : "ESP32_A 연결 실패");
    }
  }, [decodeFrame, handleLine, renewCameraLease, sendCommand]);

  const handleImageUpload = useCallback(async (file: File | undefined) => {
    if (!file) return;
    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      await decodeFrame({ sequence: Date.now() >>> 0, width: 0, height: 0, jpeg: bytes, mimeType: file.type });
      setConnectionMessage("정지 이미지 분석 모드");
      setStatusMessage(`${file.name} 프레임을 불러왔습니다.`);
    } catch {
      setStatusMessage("이미지를 디코딩하지 못했습니다. JPEG 또는 PNG 파일을 확인하세요.");
    }
  }, [decodeFrame]);

  const collectPairs = useCallback(async (requestedCount: number) => {
    const current = latestPairRef.current;
    if (!current) throw new Error("분석할 카메라 프레임이 없습니다.");
    if (connectionRef.current !== "connected") return [current];

    const startId = frameIdRef.current;
    const count = clamp(Math.round(requestedCount), 1, 15);
    const deadline = Date.now() + Math.max(6000, count * 1200);
    while (Date.now() < deadline) {
      const collected = frameRecordsRef.current
        .filter((record) => record.id > startId)
        .slice(0, count)
        .map((record) => record.pair);
      if (collected.length >= count) return collected;
      await sleep(80);
    }
    const fallback = frameRecordsRef.current
      .filter((record) => record.id > startId)
      .map((record) => record.pair);
    if (fallback.length > 0) return fallback;
    throw new Error("새 카메라 프레임을 받지 못했습니다.");
  }, []);

  const captureDark = useCallback(async () => {
    setBusy(true);
    try {
      const pairs = await collectPairs(settings.framesPerPoint);
      const value = aggregatePairs(pairs);
      setDark(value);
      setBlank(null);
      setStatusMessage(`암전 기준 ${pairs.length} frame을 저장했습니다. 이제 광원을 켜고 PDLC를 제거하세요.`);
    } catch (error) {
      setStatusMessage(error instanceof Error ? error.message : "암전 기준 저장 실패");
    } finally {
      setBusy(false);
    }
  }, [collectPairs, settings.framesPerPoint]);

  const captureBlank = useCallback(async () => {
    if (!darkRef.current) {
      setStatusMessage("암전 기준을 먼저 저장하세요.");
      return;
    }
    if (roisOverlap(referenceRoiRef.current, sampleRoiRef.current)) {
      setStatusMessage("REFERENCE와 PDLC ROI가 겹칩니다. 두 영역을 분리하세요.");
      return;
    }
    setBusy(true);
    try {
      const pairs = await collectPairs(settings.framesPerPoint);
      const value = aggregatePairs(pairs);
      const referenceSignal = value.reference.green - darkRef.current.reference.green;
      const sampleSignal = value.sample.green - darkRef.current.sample.green;
      if (referenceSignal <= 0.015 || sampleSignal <= 0.015) {
        throw new Error("무시료 기준 신호가 너무 낮습니다. 광원과 ROI를 확인하세요.");
      }
      if (Math.max(value.reference.saturationRatio, value.sample.saturationRatio) > 0.02) {
        throw new Error("무시료 기준의 포화 픽셀이 2%를 넘습니다. 광량을 낮추세요.");
      }
      setBlank(value);
      setStatusMessage(`무시료 기준 ${pairs.length} frame을 저장했습니다. PDLC를 sample ROI 경로에 설치하세요.`);
    } catch (error) {
      setStatusMessage(error instanceof Error ? error.message : "무시료 기준 저장 실패");
    } finally {
      setBusy(false);
    }
  }, [collectPairs, settings.framesPerPoint]);

  const recordMeasurement = useCallback(async (
    mi: number,
    direction: LutPoint["direction"],
  ) => {
    const darkValue = darkRef.current;
    const blankValue = blankRef.current;
    if (!darkValue || !blankValue) throw new Error("암전과 무시료 기준이 모두 필요합니다.");
    if (roisOverlap(referenceRoiRef.current, sampleRoiRef.current)) {
      throw new Error("REFERENCE와 PDLC ROI가 겹칩니다. 두 영역을 분리하세요.");
    }
    const pairs = await collectPairs(settings.framesPerPoint);
    const summary = summarizeEstimates(
      pairs.map((pair) => estimateOptics(pair, darkValue, blankValue)),
    );
    if (!summary.estimate) throw new Error("유효한 광학 측정값을 만들지 못했습니다.");
    const point = makePoint(
      summary.estimate,
      summary.repeatability,
      settings.channel,
      mi,
      direction,
    );
    setPoints((current) => [...current, point]);
    if (summary.estimate.warnings.length) {
      setStatusMessage(summary.estimate.warnings.join(" "));
    }
    return point;
  }, [collectPairs, settings.channel, settings.framesPerPoint]);

  const sendManualMi = useCallback(async (mi: number) => {
    const controllerMi = toControllerMi(mi);
    await sendCommand({
      command: "manual_channel",
      channel_id: settings.channel,
      target_mi: controllerMi,
      ttl_ms: 15000,
      enable: controllerMi > 0,
    });
  }, [sendCommand, settings.channel]);

  const captureManualPoint = useCallback(async () => {
    if (!dark || !blank) {
      setStatusMessage("암전과 무시료 기준을 먼저 저장하세요.");
      return;
    }
    setBusy(true);
    try {
      if (connectionRef.current === "connected") {
        if (!safetyArmed) throw new Error("출력 제어 안전 확인을 먼저 선택하세요.");
        if (!downstreamHealthy) throw new Error("ESP32_B healthy 상태를 확인할 수 없어 출력을 적용하지 않습니다.");
        await sendManualMi(manualMi);
        await sleep(settings.settleMs);
      }
      const point = await recordMeasurement(manualMi, "manual");
      setStatusMessage(`Lab MI ${point.mi.toFixed(2)} / controller MI ${point.controllerMi.toFixed(3)} 측정값을 기록했습니다.`);
    } catch (error) {
      setStatusMessage(error instanceof Error ? error.message : "수동 측정 실패");
    } finally {
      setBusy(false);
    }
  }, [blank, dark, downstreamHealthy, manualMi, recordMeasurement, safetyArmed, sendManualMi, settings.settleMs]);

  const outputOff = useCallback(async () => {
    stopSweepRef.current = true;
    if (connectionRef.current !== "connected") return;
    try {
      await sendCommand({
        command: "manual_channel",
        channel_id: settings.channel,
        target_mi: 0,
        ttl_ms: 15000,
        enable: false,
      });
      setStatusMessage("선택 채널에 MI 0 / disable 명령을 보냈습니다. 물리 E-Stop 상태도 확인하세요.");
    } catch (error) {
      setStatusMessage(error instanceof Error ? error.message : "출력 OFF 명령 실패");
    }
  }, [sendCommand, settings.channel]);

  const returnAuto = useCallback(async () => {
    if (connectionRef.current !== "connected") return;
    try {
      await sendCommand({ command: "return_auto", channel_id: settings.channel });
      setStatusMessage(`CH${settings.channel}을 AUTO 정책으로 복귀시켰습니다.`);
    } catch (error) {
      setStatusMessage(error instanceof Error ? error.message : "AUTO 복귀 실패");
    }
  }, [sendCommand, settings.channel]);

  const runSweep = useCallback(async () => {
    if (connectionRef.current !== "connected") {
      setStatusMessage("자동 sweep에는 ESP32_A USB 연결이 필요합니다.");
      return;
    }
    if (!darkRef.current || !blankRef.current) {
      setStatusMessage("암전과 무시료 기준을 먼저 저장하세요.");
      return;
    }
    if (!safetyArmed) {
      setStatusMessage("출력 제어 안전 확인을 먼저 선택하세요.");
      return;
    }
    if (!downstreamHealthy) {
      setStatusMessage("ESP32_B healthy 상태를 확인할 수 없어 자동 sweep을 시작하지 않습니다.");
      return;
    }
    const up = buildSweepValues(settings.startMi, settings.endMi, settings.stepMi);
    const plan = [
      ...up.map((mi) => ({ mi, direction: "up" as const })),
      ...[...up].reverse().slice(1).map((mi) => ({ mi, direction: "down" as const })),
    ];
    stopSweepRef.current = false;
    setBusy(true);
    setSweepProgress({ current: 0, total: plan.length, label: "sweep 준비" });
    try {
      for (let index = 0; index < plan.length; index += 1) {
        if (stopSweepRef.current) throw new Error("사용자가 sweep을 중지했습니다.");
        const step = plan[index];
        setSweepProgress({
          current: index + 1,
          total: plan.length,
          label: `Lab ${step.mi.toFixed(2)} · controller ${toControllerMi(step.mi).toFixed(3)} · ${step.direction === "up" ? "상승" : "하강"}`,
        });
        await sendManualMi(step.mi);
        await sleep(settings.settleMs);
        if (stopSweepRef.current) throw new Error("사용자가 sweep을 중지했습니다.");
        await recordMeasurement(step.mi, step.direction);
      }
      setStatusMessage(`CH${settings.channel} 상승·하강 sweep ${plan.length}점을 완료했습니다.`);
    } catch (error) {
      setStatusMessage(error instanceof Error ? error.message : "MI sweep 실패");
    } finally {
      try {
        await sendCommand({
          command: "manual_channel",
          channel_id: settings.channel,
          target_mi: 0,
          ttl_ms: 15000,
          enable: false,
        });
      } catch {
        // Physical E-Stop remains authoritative if the best-effort off command fails.
      }
      setBusy(false);
      setSweepProgress({ current: 0, total: 0, label: "" });
    }
  }, [downstreamHealthy, recordMeasurement, safetyArmed, sendCommand, sendManualMi, settings]);

  const liveEstimate = useMemo(() => {
    if (!latestPair || !dark || !blank) return null;
    return estimateOptics(latestPair, dark, blank);
  }, [blank, dark, latestPair]);

  const normalizedLut = useMemo(
    () => buildNormalizedLut(points, settings.channel),
    [points, settings.channel],
  );

  const realPoints = useMemo(
    () => points.filter((point) => point.source === "camera"),
    [points],
  );

  const channelPoints = useMemo(
    () => points.filter((point) => point.channel === settings.channel),
    [points, settings.channel],
  );

  useEffect(() => {
    const canvas = chartRef.current;
    if (!canvas) return;
    const context = canvas.getContext("2d");
    if (!context) return;
    const width = 920;
    const height = 310;
    canvas.width = width;
    canvas.height = height;
    context.clearRect(0, 0, width, height);
    context.fillStyle = "#18201d";
    context.fillRect(0, 0, width, height);

    const margin = { left: 58, right: 26, top: 24, bottom: 42 };
    const plotWidth = width - margin.left - margin.right;
    const plotHeight = height - margin.top - margin.bottom;
    context.strokeStyle = "rgba(255,255,255,.11)";
    context.fillStyle = "rgba(255,255,255,.48)";
    context.font = "11px ui-monospace, monospace";
    context.lineWidth = 1;
    for (let index = 0; index <= 5; index += 1) {
      const x = margin.left + (index / 5) * plotWidth;
      context.beginPath();
      context.moveTo(x, margin.top);
      context.lineTo(x, margin.top + plotHeight);
      context.stroke();
      context.fillText((index / 5).toFixed(1), x - 9, height - 18);
    }
    for (let index = 0; index <= 5; index += 1) {
      const y = margin.top + (index / 5) * plotHeight;
      context.beginPath();
      context.moveTo(margin.left, y);
      context.lineTo(margin.left + plotWidth, y);
      context.stroke();
      context.fillText(`${Math.round((1.2 - index * 0.24) * 100)}`, 16, y + 4);
    }
    context.fillText("LAB MI", width - 60, height - 18);
    context.save();
    context.translate(14, 130);
    context.rotate(-Math.PI / 2);
    context.fillText("RELATIVE RESPONSE %", 0, 0);
    context.restore();

    const xFor = (mi: number) => margin.left + clamp(mi / LAB_MI_MAX, 0, 1) * plotWidth;
    const yFor = (value: number) => margin.top + (1 - clamp(value / 1.2, 0, 1)) * plotHeight;
    const sorted = [...channelPoints].sort((a, b) => a.mi - b.mi || a.timestamp.localeCompare(b.timestamp));

    const drawSeries = (key: "transmission" | "clarity", color: string, dashed: boolean) => {
      context.strokeStyle = color;
      context.lineWidth = 2;
      context.setLineDash(dashed ? [7, 6] : []);
      context.beginPath();
      sorted.forEach((point, index) => {
        const x = xFor(point.mi);
        const y = yFor(point[key]);
        if (index === 0) context.moveTo(x, y);
        else context.lineTo(x, y);
      });
      context.stroke();
      context.setLineDash([]);
    };
    if (sorted.length) {
      drawSeries("transmission", "#d7f36a", false);
      drawSeries("clarity", "#73ddd4", true);
      sorted.forEach((point) => {
        context.fillStyle = point.source === "demo"
          ? "rgba(255,255,255,.45)"
          : point.valid ? "#d7f36a" : "#ff806d";
        context.beginPath();
        context.arc(xFor(point.mi), yFor(point.transmission), 4, 0, Math.PI * 2);
        context.fill();
      });
    } else {
      context.fillStyle = "rgba(255,255,255,.42)";
      context.font = "13px system-ui, sans-serif";
      context.fillText("기록된 측정점이 없습니다", width / 2 - 76, height / 2);
    }
  }, [channelPoints]);

  const updateRoi = useCallback((kind: "reference" | "sample", key: keyof Roi, percent: number) => {
    const setter = kind === "reference" ? setReferenceRoi : setSampleRoi;
    setter((current) => {
      const value = clamp(percent / 100, key === "width" || key === "height" ? 0.05 : 0, 0.95);
      const next = { ...current, [key]: value };
      if (next.x + next.width > 1) next.width = 1 - next.x;
      if (next.y + next.height > 1) next.height = 1 - next.y;
      return next;
    });
    setDark(null);
    setBlank(null);
    setStatusMessage("ROI가 변경되어 암전·무시료 기준을 초기화했습니다.");
  }, []);

  const exportJson = useCallback(() => {
    const channels = [0, 1, 2, 3].map((channel) => ({
      channel_id: channel,
      measured_points: realPoints
        .filter((point) => point.channel === channel)
        .map((point) => ({
          timestamp: point.timestamp,
          channel: point.channel,
          lab_mi: Number(point.mi.toFixed(4)),
          controller_mi: Number(point.controllerMi.toFixed(4)),
          direction: point.direction,
          transmission: point.transmission,
          clarity: point.clarity,
          repeatability: point.repeatability,
          valid: point.valid,
          source: point.source,
        })),
      normalized_lut: buildNormalizedLut(realPoints, channel).map((point) => ({
        optical_response: Number(point.opticalResponse.toFixed(6)),
        lab_mi: Number(point.mi.toFixed(4)),
        controller_mi: Number(point.controllerMi.toFixed(4)),
        camera_relative_transmission: Number(point.transmission.toFixed(6)),
      })),
    }));
    const artifact = {
      schema_version: 2,
      generated_at: new Date().toISOString(),
      title: "KUGLASS camera-relative PDLC LUT",
      measurement: {
        sensor: "OV2640 via ESP32_A KUGLCAM1 JPEG",
        metric: "same-frame reference-normalized green-channel response",
        gamma: settings.gamma,
        reference_roi: referenceRoi,
        sample_roi: sampleRoi,
        frames_per_point: settings.framesPerPoint,
        settle_ms: settings.settleMs,
        mi_mapping: "controller_mi = lab_mi * 0.60",
      },
      caution: "Not a standard luminous-transmittance or haze measurement. Vrms is not measured.",
      channels,
    };
    downloadText("kuglass-camera-relative-lut.json", JSON.stringify(artifact, null, 2), "application/json");
  }, [realPoints, referenceRoi, sampleRoi, settings]);

  const exportCsv = useCallback(() => {
    const header = "timestamp,channel,lab_mi,controller_mi,direction,camera_relative_transmission,clarity_proxy,repeatability,valid\n";
    const rows = realPoints.map((point) => [
      point.timestamp,
      point.channel,
      point.mi.toFixed(4),
      point.controllerMi.toFixed(4),
      point.direction,
      point.transmission.toFixed(6),
      point.clarity.toFixed(6),
      point.repeatability.toFixed(6),
      point.valid,
    ].join(","));
    downloadText("kuglass-camera-relative-lut.csv", header + rows.join("\n"), "text/csv;charset=utf-8");
  }, [realPoints]);

  const exportHeader = useCallback(() => {
    if (normalizedLut.length < 2) {
      setStatusMessage("C++ LUT를 만들려면 현재 채널에 유효한 실측점이 2개 이상 필요합니다.");
      return;
    }
    const rows = normalizedLut
      .map((point) => `    {${point.opticalResponse.toFixed(6)}f, ${point.mi.toFixed(4)}f, ${point.controllerMi.toFixed(4)}f},`)
      .join("\n");
    const content = `#pragma once\n\n// Generated by PDLC_LUT. Camera-relative response only; not absolute transmittance.\n// Lab MI 0..1 maps to ESP32_A controller MI 0..0.60. Channel: CH${settings.channel}\nstruct PdlcCameraLutPoint { float optical_response; float lab_mi; float controller_mi; };\n\nconstexpr PdlcCameraLutPoint kPdlcCameraLutCh${settings.channel}[] = {\n${rows}\n};\n`;
    downloadText(`pdlc_camera_lut_ch${settings.channel}.h`, content, "text/plain;charset=utf-8");
  }, [normalizedLut, settings.channel]);

  const importJson = useCallback(async (file: File | undefined) => {
    if (!file) return;
    try {
      const parsed = JSON.parse(await file.text()) as {
        schema_version?: number;
        channels?: Array<{ channel_id: number; measured_points?: unknown[] }>;
      };
      const legacyControllerScale = (parsed.schema_version ?? 1) < 2;
      const imported = (parsed.channels ?? []).flatMap((channel) =>
        (channel.measured_points ?? [])
          .map((point, index) => {
            const raw = point && typeof point === "object"
              ? point as Record<string, unknown>
              : {};
            const candidate = legacyControllerScale
              ? raw
              : { ...raw, mi: raw.lab_mi ?? raw.mi };
            return sanitizeLutPoint(
              candidate,
              `import-${Date.now()}-${channel.channel_id}-${index}`,
              channel.channel_id,
              "camera",
              legacyControllerScale,
            );
          })
          .filter((point): point is LutPoint => point !== null),
      );
      if (!imported.length) throw new Error("측정점이 없습니다.");
      setPoints((current) => [...current.filter((point) => point.source !== "demo"), ...imported]);
      setStatusMessage(`${imported.length}개 LUT 측정점을 가져왔습니다.`);
    } catch (error) {
      setStatusMessage(error instanceof Error ? `JSON 가져오기 실패: ${error.message}` : "JSON 가져오기 실패");
    }
  }, []);

  const cameraValid = Boolean(frameInfo && latestPair);
  const progressRatio = sweepProgress.total > 0 ? sweepProgress.current / sweepProgress.total : 0;

  return (
    <main className="shell">
      <header className="topbar">
        <div className="brand">
          <span className="brandMark">KG</span>
          <div><strong>KUGLASS</strong><span>Optical calibration lab</span></div>
        </div>
        <div className={`connectionBadge ${connection}`}>
          <span className="statusDot" />{connectionMessage}
        </div>
      </header>

      <section className="intro">
        <div>
          <p className="eyebrow">PDLC · CAMERA-RELATIVE RESPONSE</p>
          <h1>빛의 변화를<br />측정 가능한 LUT로.</h1>
          <p className="introCopy">
            OV2640 영상의 기준 영역과 PDLC 영역을 같은 프레임에서 비교해
            CH0~CH3의 Lab MI 0~1별 상대 광응답과 선명도 변화를 기록합니다.
          </p>
        </div>
        <ol className="stepRail" aria-label="측정 단계">
          <li className={cameraValid ? "done" : "active"}><b>01</b><span>카메라 연결</span></li>
          <li className={blank ? "done" : dark ? "active" : ""}><b>02</b><span>기준 보정</span></li>
          <li className={busy ? "active" : points.length ? "done" : ""}><b>03</b><span>MI 스윕</span></li>
          <li className={normalizedLut.length >= 2 ? "active" : ""}><b>04</b><span>LUT 검토</span></li>
        </ol>
      </section>

      <section className="workspace">
        <article className="cameraCard">
          <div className="cardHeader">
            <div><span className="sectionIndex">01</span><h2>카메라 프레임</h2></div>
            <div className="headerActions">
              <label className="ghostButton fileButton">
                이미지 불러오기
                <input type="file" accept="image/jpeg,image/png" onChange={(event) => void handleImageUpload(event.target.files?.[0])} />
              </label>
              {connection === "connected" ? (
                <button className="ghostButton" type="button" onClick={() => void disconnect()}>연결 해제</button>
              ) : (
                <button className="primaryButton" type="button" onClick={() => void connect()} disabled={connection === "connecting" || connection === "unsupported"}>
                  USB 카메라 연결
                </button>
              )}
            </div>
          </div>
          <div className="cameraStage">
            <div className="frameGrid" />
            <canvas ref={canvasRef} className={cameraValid ? "cameraCanvas visible" : "cameraCanvas"} aria-label="ESP32_A OV2640 프레임" />
            <div className="roi roiReference" style={{ left: `${referenceRoi.x * 100}%`, top: `${referenceRoi.y * 100}%`, width: `${referenceRoi.width * 100}%`, height: `${referenceRoi.height * 100}%` }}><span>REFERENCE ROI</span></div>
            <div className="roi roiSample" style={{ left: `${sampleRoi.x * 100}%`, top: `${sampleRoi.y * 100}%`, width: `${sampleRoi.width * 100}%`, height: `${sampleRoi.height * 100}%` }}><span>PDLC ROI</span></div>
            {!cameraValid && (
              <div className="emptyCamera">
                <span className="aperture" />
                <strong>ESP32_A 영상 스트림 대기 중</strong>
                <small>Chrome/Edge에서 USB를 연결하거나 기준 이미지를 불러오세요</small>
              </div>
            )}
          </div>
          <div className="cameraFooter">
            <span>{frameInfo ? `${frameInfo.width || "—"} × ${frameInfo.height || "—"} · FRAME ${frameInfo.sequence}` : "640 × 480 · KUGLCAM1 JPEG"}</span>
            <span>GAMMA {settings.gamma.toFixed(2)} · REFERENCE / SAMPLE</span>
          </div>
        </article>

        <aside className="metricPanel">
          <div className="cardHeader compact"><div><span className="sectionIndex">LIVE</span><h2>광학 상태</h2></div></div>
          <div className="heroMetric">
            <span>카메라 상대 투과</span>
            <strong>{liveEstimate ? (liveEstimate.transmission * 100).toFixed(1) : "—"}<small>%</small></strong>
            <div className="metricTrack"><i style={{ width: `${clamp((liveEstimate?.transmission ?? 0) * 100, 0, 100)}%` }} /></div>
          </div>
          <dl className="metricList">
            <div><dt>Reference G</dt><dd>{latestPair ? latestPair.reference.green.toFixed(4) : "—"}</dd></div>
            <div><dt>PDLC G</dt><dd>{latestPair ? latestPair.sample.green.toFixed(4) : "—"}</dd></div>
            <div><dt>Clarity proxy</dt><dd>{formatPercent(liveEstimate?.clarity ?? null)}</dd></div>
            <div><dt>Saturation</dt><dd>{formatPercent(liveEstimate?.saturationRatio ?? null, 2)}</dd></div>
          </dl>
          <div className={`qualityFlag ${liveEstimate?.valid && !roiOverlap ? "valid" : ""}`}>
            <b>{roiOverlap ? "ROI 분리 필요" : !dark ? "DARK 필요" : !blank ? "BLANK 필요" : liveEstimate?.valid ? "측정 유효" : "프레임 확인"}</b>
            <p>{roiOverlap ? "REFERENCE와 PDLC ROI는 서로 겹치지 않아야 합니다." : liveEstimate?.warnings.join(" ") || "같은 프레임의 기준 ROI로 전역 밝기 변화를 정규화합니다."}</p>
          </div>
        </aside>
      </section>

      <section className="sectionBlock">
        <div className="sectionHeading">
          <div><span className="sectionIndex">02</span><h2>기준 보정</h2></div>
          <p>ROI 또는 gamma를 바꾸면 기준을 다시 저장해야 합니다.</p>
        </div>
        <div className="calibrationGrid">
          <article className={`calibrationCard ${dark ? "complete" : ""}`}>
            <span className="calibrationNumber">01</span>
            <h3>암전 기준</h3>
            <p>광원을 끄거나 렌즈를 완전히 가린 뒤 dark offset을 저장합니다.</p>
            <button type="button" onClick={() => void captureDark()} disabled={busy || !latestPair}>{dark ? "암전 다시 측정" : "암전 저장"}</button>
            <small>{dark ? `REF ${dark.reference.green.toFixed(5)} · PDLC ${dark.sample.green.toFixed(5)}` : "미측정"}</small>
          </article>
          <article className={`calibrationCard ${blank ? "complete" : ""}`}>
            <span className="calibrationNumber">02</span>
            <h3>무시료 기준</h3>
            <p>광원을 켜고 sample 경로에서 PDLC를 제거한 기준 프레임입니다.</p>
            <button type="button" onClick={() => void captureBlank()} disabled={busy || !dark || !latestPair}>{blank ? "기준 다시 측정" : "무시료 저장"}</button>
            <small>{blank ? `REF ${blank.reference.green.toFixed(4)} · PDLC ${blank.sample.green.toFixed(4)}` : "암전 이후 측정"}</small>
          </article>
          <article className="calibrationCard roiControlCard">
            <span className="calibrationNumber">03</span>
            <h3>분석 조건</h3>
            <label>응답 gamma <output>{settings.gamma.toFixed(2)}</output><input type="range" min="1" max="3" step="0.05" value={settings.gamma} onChange={(event) => { setSettings((current) => ({ ...current, gamma: Number(event.target.value) })); setDark(null); setBlank(null); }} /></label>
            <details>
              <summary>ROI 좌표 조정</summary>
              {(["reference", "sample"] as const).map((kind) => {
                const roi = kind === "reference" ? referenceRoi : sampleRoi;
                return (
                  <fieldset key={kind}>
                    <legend>{kind === "reference" ? "REFERENCE" : "PDLC"}</legend>
                    {(["x", "y", "width", "height"] as const).map((key) => (
                      <label key={key}>{key}<input type="number" min="0" max="95" step="1" value={Math.round(roi[key] * 100)} onChange={(event) => updateRoi(kind, key, Number(event.target.value))} /></label>
                    ))}
                  </fieldset>
                );
              })}
              <button className="textButton" type="button" onClick={() => { setReferenceRoi(DEFAULT_REFERENCE_ROI); setSampleRoi(DEFAULT_SAMPLE_ROI); setDark(null); setBlank(null); }}>좌우 기본값</button>
            </details>
          </article>
        </div>
      </section>

      <section className="sectionBlock controlBlock">
        <div className="sectionHeading">
          <div><span className="sectionIndex">03</span><h2>MI 스윕</h2></div>
          <p>{downstream?.healthy === true ? "ESP32_B 상태 정상" : "ESP32_B 상태는 웹에서 검증되지 않았습니다"}</p>
        </div>
        <div className="safetyBanner">
          <span>!</span>
          <div><b>카메라는 전기·고전압 계측기를 대체하지 않습니다.</b><p>Logic-only·저전압 검증을 통과한 한 채널만 사용하고, 물리 E-Stop과 방전 절차를 유지하세요. USB 단절 후 AUTO가 재개될 수 있습니다.</p></div>
          <label><input type="checkbox" checked={safetyArmed} onChange={(event) => setSafetyArmed(event.target.checked)} /> 안전 조건 확인</label>
        </div>
        <div className="controlGrid">
          <article className="sweepCard">
            <div className="subhead"><h3>자동 상승·하강 sweep</h3><span>Lab 1.00 = controller 0.60</span></div>
            <div className="inputGrid">
              <label>채널<select value={settings.channel} onChange={(event) => setSettings((current) => ({ ...current, channel: Number(event.target.value) }))}>{[0,1,2,3].map((channel) => <option key={channel} value={channel}>CH{channel}</option>)}</select></label>
              <label>시작 Lab MI<input type="number" min="0" max="1" step="0.01" value={settings.startMi} onChange={(event) => setSettings((current) => ({ ...current, startMi: clamp(Number(event.target.value), 0, current.endMi) }))} /></label>
              <label>종료 Lab MI<input type="number" min="0" max="1" step="0.01" value={settings.endMi} onChange={(event) => setSettings((current) => ({ ...current, endMi: clamp(Number(event.target.value), current.startMi, LAB_MI_MAX) }))} /></label>
              <label>Lab MI 간격<input type="number" min="0.01" max="0.25" step="0.01" value={settings.stepMi} onChange={(event) => setSettings((current) => ({ ...current, stepMi: clamp(Number(event.target.value), 0.01, 0.25) }))} /></label>
              <label>안정화 ms<input type="number" min="500" max="5000" step="100" value={settings.settleMs} onChange={(event) => setSettings((current) => ({ ...current, settleMs: clamp(Number(event.target.value), 500, 5000) }))} /></label>
              <label>평균 frame<input type="number" min="1" max="15" step="1" value={settings.framesPerPoint} onChange={(event) => setSettings((current) => ({ ...current, framesPerPoint: clamp(Number(event.target.value), 1, 15) }))} /></label>
            </div>
            {sweepProgress.total > 0 && (
              <div className="progressBox"><div><span>{sweepProgress.label}</span><b>{sweepProgress.current}/{sweepProgress.total}</b></div><i><em style={{ width: `${progressRatio * 100}%` }} /></i></div>
            )}
            <div className="buttonRow">
              <button className="primaryButton wide" type="button" disabled={busy || connection !== "connected" || !dark || !blank || !safetyArmed || !downstreamHealthy || roiOverlap} onClick={() => void runSweep()}>{busy ? "측정 중…" : "상승·하강 sweep 시작"}</button>
              <button className="dangerButton" type="button" onClick={() => void outputOff()}>중지 / CH OFF</button>
            </div>
          </article>
          <article className="manualCard">
            <div className="subhead"><h3>단일점 측정</h3><span>정지 이미지도 가능</span></div>
            <div className="manualValue"><span>LAB MI</span><strong>{manualMi.toFixed(2)}</strong><small>controller {toControllerMi(manualMi).toFixed(3)}</small></div>
            <input className="miSlider" type="range" min="0" max="1" step="0.01" value={manualMi} onChange={(event) => setManualMi(Number(event.target.value))} />
            <div className="sliderTicks"><span>FROST · 0.00</span><span>0.50</span><span>1.00 · CLEAR</span></div>
            <button className="primaryButton wide" type="button" disabled={busy || !dark || !blank || !latestPair || roiOverlap || (connection === "connected" && (!safetyArmed || !downstreamHealthy))} onClick={() => void captureManualPoint()}>{connection === "connected" ? "MI 적용 후 측정" : "현재 이미지로 기록"}</button>
            <div className="minorActions"><button type="button" onClick={() => void outputOff()}>MI 0 / disable</button><button type="button" onClick={() => void returnAuto()}>AUTO 복귀</button></div>
          </article>
        </div>
        <div className="statusStrip"><span className={busy ? "pulseDot" : "statusDot"} /><b>STATUS</b><p>{statusMessage}</p></div>
      </section>

      <section className="sectionBlock resultsBlock">
        <div className="sectionHeading">
          <div><span className="sectionIndex">04</span><h2>LUT 결과</h2></div>
          <div className="resultActions">
            <button type="button" onClick={() => setPoints((current) => [...current.filter((point) => !(point.source === "demo" && point.channel === settings.channel)), ...createDemoPoints(settings.channel)])}>예시 데이터</button>
            <label className="fileButton">JSON 가져오기<input type="file" accept="application/json" onChange={(event) => void importJson(event.target.files?.[0])} /></label>
            <button type="button" onClick={exportCsv} disabled={!realPoints.length}>CSV</button>
            <button type="button" onClick={exportJson} disabled={!realPoints.length}>JSON</button>
            <button type="button" onClick={exportHeader} disabled={normalizedLut.length < 2}>C++ header</button>
          </div>
        </div>
        <div className="resultSummary">
          <div><span>현재 채널</span><strong>CH{settings.channel}</strong></div>
          <div><span>실측점</span><strong>{channelPoints.filter((point) => point.source === "camera").length}</strong></div>
          <div><span>유효 LUT점</span><strong>{normalizedLut.length}</strong></div>
          <div><span>평균 반복편차</span><strong>{formatPercent(channelPoints.filter((point) => point.source === "camera").length ? median(channelPoints.filter((point) => point.source === "camera").map((point) => point.repeatability)) : null, 2)}</strong></div>
        </div>
        <div className="chartCard">
          <div className="chartLegend"><span className="lime">상대 투과</span><span className="cyan">Clarity proxy</span><small>{channelPoints.some((point) => point.source === "demo") ? "회색 점은 예시이며 export에서 제외됩니다" : "상승·하강 raw point를 함께 표시합니다"}</small></div>
          <canvas ref={chartRef} aria-label={`CH${settings.channel} Lab MI 대비 상대 광응답 그래프`} />
        </div>
        <div className="tableWrap">
          <table>
            <thead><tr><th>채널</th><th>Lab MI</th><th>Controller MI</th><th>방향</th><th>상대 투과</th><th>Clarity</th><th>반복편차</th><th>상태</th><th /></tr></thead>
            <tbody>
              {channelPoints.length === 0 ? (
                <tr><td colSpan={9} className="emptyCell">보정 후 단일점 측정 또는 자동 sweep을 실행하세요.</td></tr>
              ) : channelPoints
                .slice()
                .sort((a, b) => a.mi - b.mi || a.timestamp.localeCompare(b.timestamp))
                .map((point) => (
                  <tr key={point.id} className={point.source === "demo" ? "demoRow" : ""}>
                    <td>CH{point.channel}</td><td>{point.mi.toFixed(2)}</td><td>{point.controllerMi.toFixed(3)}</td><td>{point.direction}</td><td>{formatPercent(point.transmission)}</td><td>{formatPercent(point.clarity)}</td><td>{formatPercent(point.repeatability, 2)}</td><td><span className={`tableStatus ${point.valid ? "pass" : "fail"}`}>{point.source === "demo" ? "DEMO" : point.valid ? "VALID" : "CHECK"}</span></td><td><button className="deleteButton" type="button" aria-label={`Lab MI ${point.mi.toFixed(2)} 측정점 삭제`} onClick={() => setPoints((current) => current.filter((candidate) => candidate.id !== point.id))}>×</button></td>
                  </tr>
                ))}
            </tbody>
          </table>
        </div>
        <div className="footnote"><b>결과 의미</b><p>이 웹은 OV2640 JPEG의 green 채널과 동일 프레임 reference ROI를 이용한 상대 응답을 추정합니다. 표준 총광선투과율·헤이즈·Vrms 측정값이 아니며, 기준 계측과 교차검증하기 전 production 물리값으로 사용하지 마세요.</p><button type="button" onClick={() => { setPoints([]); setDark(null); setBlank(null); setStatusMessage("로컬 측정 세션을 초기화했습니다."); }}>로컬 세션 초기화</button></div>
      </section>
    </main>
  );
}
