import { useEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { Camera, CircleAlert, LoaderCircle, X } from 'lucide-react';

const backendUrl = (import.meta.env.VITE_BACKEND_URL ?? '').replace(/\/$/, '');
const framePollDelayMs = 70;
const streamRenewMs = 10_000;

interface CameraStatus {
  requested: boolean;
  frameReady: boolean;
  sequence: number | null;
  width: number | null;
  height: number | null;
  payloadBytes: number | null;
  frameAgeSeconds: number | null;
  goodFrames: number;
  badFrames: number;
  averageFps: number;
  hardwareConnected: boolean;
  transport: 'usb' | 'mock';
}

interface Props {
  onClose: () => void;
}

const pause = (milliseconds: number, signal: AbortSignal) => new Promise<void>((resolve) => {
  let settled = false;
  const finish = () => {
    if (settled) return;
    settled = true;
    window.clearTimeout(timeout);
    signal.removeEventListener('abort', finish);
    resolve();
  };
  const timeout = window.setTimeout(finish, milliseconds);
  signal.addEventListener('abort', finish, { once: true });
});

async function requestStream(enabled: boolean, signal?: AbortSignal): Promise<void> {
  const response = await fetch(`${backendUrl}/api/command`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ type: 'setCameraStream', enabled }),
    cache: 'no-store',
    keepalive: !enabled,
    signal
  });
  const result = await response.json().catch(() => ({})) as { error?: string };
  if (!response.ok) {
    throw new Error(result.error ?? `camera command HTTP ${response.status}`);
  }
}

export function CameraViewer({ onClose }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [status, setStatus] = useState<CameraStatus | null>(null);
  const [message, setMessage] = useState('ESP32_A 카메라 스트림을 요청하는 중입니다.');
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const controller = new AbortController();
    let active = true;
    let lastSequence = -1;

    const refreshStatus = async () => {
      try {
        const response = await fetch(`${backendUrl}/api/camera/status`, {
          cache: 'no-store',
          signal: controller.signal
        });
        if (!response.ok) throw new Error(`camera status HTTP ${response.status}`);
        const next = await response.json() as CameraStatus;
        if (active) setStatus(next);
      } catch (reason) {
        if (active && !(reason instanceof DOMException && reason.name === 'AbortError')) {
          setError(reason instanceof Error ? reason.message : String(reason));
        }
      }
    };

    const renderFrames = async () => {
      while (active) {
        try {
          const response = await fetch(`${backendUrl}/api/camera/frame?after=${lastSequence}`, {
            cache: 'no-store',
            signal: controller.signal
          });
          if (response.status === 204) {
            await pause(framePollDelayMs, controller.signal);
            continue;
          }
          if (!response.ok) throw new Error(`camera frame HTTP ${response.status}`);

          const width = Number(response.headers.get('X-Frame-Width'));
          const height = Number(response.headers.get('X-Frame-Height'));
          const sequence = Number(response.headers.get('X-Frame-Sequence'));
          const bitmap = await createImageBitmap(await response.blob());
          const canvas = canvasRef.current;
          const context = canvas?.getContext('2d', { alpha: false });
          if (canvas && context && width > 0 && height > 0) {
            if (canvas.width !== width || canvas.height !== height) {
              canvas.width = width;
              canvas.height = height;
            }
            context.drawImage(bitmap, 0, 0, width, height);
            lastSequence = sequence;
            setMessage('실시간 수신 중');
            setError(null);
          }
          bitmap.close();
        } catch (reason) {
          if (!active || (reason instanceof DOMException && reason.name === 'AbortError')) break;
          setError(reason instanceof Error ? reason.message : String(reason));
          await pause(300, controller.signal);
        }
      }
    };

    const start = async () => {
      try {
        await requestStream(true, controller.signal);
        if (!active) return;
        setMessage('첫 JPEG 프레임을 기다리는 중입니다.');
        await refreshStatus();
      } catch (reason) {
        if (active && !(reason instanceof DOMException && reason.name === 'AbortError')) {
          setError(reason instanceof Error ? reason.message : String(reason));
        }
      } finally {
        if (active) void renderFrames();
      }
    };

    void start();
    const statusInterval = window.setInterval(() => void refreshStatus(), 1000);
    const leaseInterval = window.setInterval(() => {
      void requestStream(true, controller.signal).catch((reason: unknown) => {
        if (active) setError(reason instanceof Error ? reason.message : String(reason));
      });
    }, streamRenewMs);

    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', onKeyDown);

    return () => {
      active = false;
      controller.abort();
      window.clearInterval(statusInterval);
      window.clearInterval(leaseInterval);
      window.removeEventListener('keydown', onKeyDown);
      void requestStream(false).catch(() => undefined);
    };
  }, [onClose]);

  const frameLive = Boolean(
    status?.frameReady
    && status.frameAgeSeconds !== null
    && status.frameAgeSeconds < 2
    && !error
  );
  const waitingMessage = status?.transport === 'mock'
    ? 'MOCK에는 실제 카메라 영상이 없습니다. LIVE 연결에서 확인하세요.'
    : !status?.hardwareConnected
      ? 'ESP32_A 연결이 끊겼습니다. USB 연결과 펌웨어 상태를 확인하세요.'
      : error ?? (status?.frameReady ? '새 카메라 프레임을 기다리는 중입니다.' : message);

  return createPortal(
    <div className="camera-viewer-backdrop" role="presentation" onMouseDown={(event) => {
      if (event.target === event.currentTarget) onClose();
    }}>
      <section className="camera-viewer-dialog" role="dialog" aria-modal="true" aria-labelledby="camera-viewer-title">
        <header className="camera-viewer-heading">
          <div>
            <span className="camera-viewer-kicker"><Camera size={14} /> ESP32_A · OV2640</span>
            <h2 id="camera-viewer-title">카메라 영상</h2>
          </div>
          <button type="button" onClick={onClose} aria-label="카메라 영상 닫기" title="닫기">
            <X size={20} />
          </button>
        </header>
        <div className="camera-viewer-stage">
          <div className="camera-viewer-frame">
            <canvas ref={canvasRef} width={640} height={480} aria-label="ESP32_A 실시간 카메라 영상" />
            <div className="camera-roi-overlay" aria-hidden="true">
              <span>운전석 ROI · CH0/CH2</span>
              <span>조수석 ROI · CH1/CH3</span>
            </div>
          </div>
          {!frameLive ? (
            <div className="camera-viewer-waiting">
              {error || status?.transport === 'mock' ? <CircleAlert size={25} /> : <LoaderCircle className="spin" size={25} />}
              <strong>{waitingMessage}</strong>
              <span>영상 요청은 제어 정책과 별도로 동작합니다.</span>
            </div>
          ) : null}
        </div>
        <footer className="camera-viewer-footer">
          <span className={frameLive ? 'live' : 'waiting'}>
            <i /> {frameLive ? 'LIVE' : 'WAITING'}
          </span>
          <div>
            <b>{status?.width ?? 640}×{status?.height ?? 480}</b>
            <b>{status?.averageFps?.toFixed(1) ?? '0.0'} fps</b>
            <b>{Math.round((status?.payloadBytes ?? 0) / 1024)} KiB</b>
            <b>정상 {status?.goodFrames ?? 0}</b>
            <b>오류 {status?.badFrames ?? 0}</b>
          </div>
        </footer>
      </section>
    </div>,
    document.body
  );
}
