import { useCallback, useState } from 'react';
import { Activity, Camera, SunMedium, Video } from 'lucide-react';
import { pct } from '../lib/labels';
import type { SimulationState } from '../types';
import { CameraViewer } from './CameraViewer';

interface Props {
  state: SimulationState;
  connected: boolean;
}

export function EvidencePanel({ state, connected }: Props) {
  const [viewerOpen, setViewerOpen] = useState(false);
  const closeViewer = useCallback(() => setViewerOpen(false), []);
  const { cameraMetrics, environment } = state;
  const beforeSat = Math.max(environment.frontLeftSaturation, environment.frontRightSaturation);
  const afterSat = Math.max(cameraMetrics.frontLeftSaturation, cameraMetrics.frontRightSaturation);
  const reduction = beforeSat > 0 ? Math.max(0, (beforeSat - afterSat) / beforeSat) : 0;
  const comparativeEvidence = state.link.transport === 'mock';
  return (
    <section className="panel evidence-panel">
      <div className="panel-heading">
        <div className="panel-title-group">
          <div>
            <h2>정책 근거</h2>
            <p title={state.decisionReason}>{state.decisionReason}</p>
          </div>
        </div>
        <span className={`panel-state ${connected ? 'ok' : 'manual'}`}>
          <Activity size={14} />
          {state.link.transport === 'mock' ? 'MOCK' : connected ? 'LIVE' : 'WAIT A'}
        </span>
      </div>
      <div className="mock-camera">
        <div className="mock-camera-frame">
          <div className="camera-hud">
            <span>FRONT</span>
            <strong>ROI</strong>
          </div>
          <div className="road-line" />
          <div className="roi left">L {pct(cameraMetrics.frontLeftSaturation)}</div>
          <div className="roi right">R {pct(cameraMetrics.frontRightSaturation)}</div>
          <div className="glare-spot" style={{ opacity: Math.min(0.92, cameraMetrics.glare + 0.05) }} />
          <button
            className="camera-view-button"
            type="button"
            disabled={!connected}
            onClick={() => setViewerOpen(true)}
            title={connected ? 'ESP32_A 카메라 영상 보기' : 'ESP32_A 연결 후 사용할 수 있습니다'}
          >
            <Video size={15} />
            영상 보기
          </button>
        </div>
      </div>
      <div className="metric-grid">
        <div className="metric">
          <Camera size={18} />
          <span>개입 후 포화</span>
          <strong>{pct(Math.max(cameraMetrics.frontLeftSaturation, cameraMetrics.frontRightSaturation))}</strong>
        </div>
        <div className="metric">
          <Activity size={18} />
          <span>Edge 보존</span>
          <strong>{pct(cameraMetrics.edgeDensity)}</strong>
        </div>
        <div className="metric">
          <SunMedium size={18} />
          <span>강광</span>
          <strong>{pct(cameraMetrics.glare)}</strong>
        </div>
      </div>
      <div
        className="evidence-delta"
        aria-label={comparativeEvidence
          ? `포화 면적 ${pct(beforeSat)}에서 ${pct(afterSat)}로 감소`
          : `현재 ROI 포화 면적 ${pct(afterSat)}`}
      >
        <span>{comparativeEvidence ? '포화 면적' : '현재 ROI 포화'}</span>
        <strong>{comparativeEvidence ? <>{pct(beforeSat)} <i>→</i> {pct(afterSat)}</> : pct(afterSat)}</strong>
        <b>{comparativeEvidence ? `-${pct(reduction)}` : connected ? 'LIVE' : 'WAIT'}</b>
      </div>
      {viewerOpen ? <CameraViewer onClose={closeViewer} /> : null}
    </section>
  );
}
