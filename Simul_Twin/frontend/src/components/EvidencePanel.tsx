import { Activity, Camera, SunMedium } from 'lucide-react';
import { hasLuxBearing, luxBearing, pct } from '../lib/labels';
import type { SimulationState } from '../types';

interface Props {
  state: SimulationState;
  connected: boolean;
}

export function EvidencePanel({ state, connected }: Props) {
  const { cameraMetrics, environment } = state;
  const bearing = luxBearing(environment);
  const bearingAvailable = hasLuxBearing(environment);
  const beforeSat = Math.max(environment.frontLeftSaturation, environment.frontRightSaturation);
  const afterSat = Math.max(cameraMetrics.frontLeftSaturation, cameraMetrics.frontRightSaturation);
  const reduction = beforeSat > 0 ? Math.max(0, (beforeSat - afterSat) / beforeSat) : 0;
  return (
    <section className="panel evidence-panel">
      <div className="panel-heading">
        <div className="panel-title-group">
          <div>
            <h2>정책 근거</h2>
            <p title={state.decisionReason}>{state.decisionReason}</p>
          </div>
        </div>
        <span className={`panel-state ${connected ? 'ok' : 'manual'}`}><Activity size={14} /> {connected ? 'LIVE' : 'MOCK'}</span>
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
      <div className="evidence-delta" aria-label={`포화 면적 ${pct(beforeSat)}에서 ${pct(afterSat)}로 감소`}>
        <span>포화 면적</span>
        <strong>{pct(beforeSat)} <i>→</i> {pct(afterSat)}</strong>
        <b>-{pct(reduction)}</b>
      </div>
      <div className="vector-panel">
        <div className="compass">
          <span
            className="compass-arrow"
            style={{
              opacity: bearingAvailable ? 1 : 0,
              transform: `translate(-50%, -50%) rotate(${bearing}deg)`
            }}
          />
          <b>N / 전</b>
        </div>
        <div>
          <h3>{bearingAvailable ? `조도 방향 · ${Math.round(bearing)}°` : '조도 방향 · 산출 불가'}</h3>
        </div>
      </div>
    </section>
  );
}
