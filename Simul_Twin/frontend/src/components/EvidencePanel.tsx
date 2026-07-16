import { Activity, Camera, SunMedium } from 'lucide-react';
import { luxBearing, pct } from '../lib/labels';
import type { SimulationState } from '../types';

interface Props {
  state: SimulationState;
}

export function EvidencePanel({ state }: Props) {
  const { cameraMetrics, environment } = state;
  const bearing = luxBearing(environment);
  const beforeSat = Math.max(environment.frontLeftSaturation, environment.frontRightSaturation);
  const afterSat = Math.max(cameraMetrics.frontLeftSaturation, cameraMetrics.frontRightSaturation);
  const reduction = beforeSat > 0 ? Math.max(0, (beforeSat - afterSat) / beforeSat) : 0;
  const luxValues = [
    ['전방', environment.frontLux],
    ['우측', environment.rightLux],
    ['후방', environment.rearLux],
    ['좌측', environment.leftLux],
    ['상부', environment.topLux]
  ] as const;

  return (
    <section className="panel evidence-panel">
      <div className="panel-heading">
        <div>
          <h2>판단 근거</h2>
          <p>{state.decisionReason}</p>
        </div>
      </div>
      <div className="mock-camera">
        <div className="mock-camera-frame">
          <div className="road-line" />
          <div className="roi left">좌측 ROI {pct(cameraMetrics.frontLeftSaturation)}</div>
          <div className="roi right">우측 ROI {pct(cameraMetrics.frontRightSaturation)}</div>
          <div className="glare-spot" style={{ opacity: Math.min(0.92, cameraMetrics.glare + 0.05) }} />
        </div>
      </div>
      <div className="metric-grid">
        <div className="metric">
          <Camera size={18} />
          <span>산란 개입 후 포화</span>
          <strong>{pct(Math.max(cameraMetrics.frontLeftSaturation, cameraMetrics.frontRightSaturation))}</strong>
        </div>
        <div className="metric">
          <Activity size={18} />
          <span>Edge 보존</span>
          <strong>{pct(cameraMetrics.edgeDensity)}</strong>
        </div>
        <div className="metric">
          <SunMedium size={18} />
          <span>강광 지표</span>
          <strong>{pct(cameraMetrics.glare)}</strong>
        </div>
      </div>
      <div className="comparison-strip">
        <div>
          <span>산란 미개입</span>
          <div className="bar-track"><span style={{ width: pct(beforeSat) }} /></div>
          <strong>{pct(beforeSat)}</strong>
        </div>
        <div>
          <span>산란 개입</span>
          <div className="bar-track"><span style={{ width: pct(afterSat) }} /></div>
          <strong>{pct(afterSat)}</strong>
        </div>
        <div>
          <span>감소율</span>
          <strong>{pct(reduction)}</strong>
        </div>
      </div>
      <div className="vector-panel">
        <div className="compass">
          <span className="compass-arrow" style={{ transform: `translate(-50%, -50%) rotate(${bearing}deg)` }} />
          <b>전</b>
        </div>
        <div>
          <h3>방향성 조도 벡터</h3>
          <p>차량 전방 기준 {Math.round(bearing)}°. 가까운 유리 채널부터 목표 MI가 낮아집니다.</p>
        </div>
      </div>
      <div className="lux-row">
        {luxValues.map(([label, value]) => (
          <div key={label} className="lux-cell">
            <span>{label}</span>
            <strong>{value === null ? '결측' : Math.round(value)}</strong>
          </div>
        ))}
      </div>
    </section>
  );
}
