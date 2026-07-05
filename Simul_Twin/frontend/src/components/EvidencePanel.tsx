import { Activity, Camera, SunMedium } from 'lucide-react';
import type { SimulationState } from '../types';

interface Props {
  state: SimulationState;
}

function pct(value: number) {
  return `${Math.round(value * 100)}%`;
}

export function EvidencePanel({ state }: Props) {
  const { cameraMetrics, environment } = state;
  const frontLux = environment.frontLux ?? 0;
  const rightLux = environment.rightLux ?? 0;
  const rearLux = environment.rearLux ?? 0;
  const leftLux = environment.leftLux ?? 0;
  const vectorX = rightLux - leftLux;
  const vectorY = frontLux - rearLux;
  const bearing = (Math.atan2(vectorX, vectorY) * 180 / Math.PI + 360) % 360;
  const beforeSat = Math.max(environment.frontLeftSaturation, environment.frontRightSaturation);
  const afterSat = Math.max(cameraMetrics.frontLeftSaturation, cameraMetrics.frontRightSaturation);
  const reduction = beforeSat > 0 ? Math.max(0, (beforeSat - afterSat) / beforeSat) : 0;
  const luxValues = [
    ['Front', environment.frontLux],
    ['Right', environment.rightLux],
    ['Rear', environment.rearLux],
    ['Left', environment.leftLux],
    ['Top', environment.topLux]
  ] as const;

  return (
    <section className="panel evidence-panel">
      <div className="panel-heading">
        <div>
          <h2>Evidence View</h2>
          <p>{state.decisionReason}</p>
        </div>
      </div>
      <div className="mock-camera">
        <div className="mock-camera-frame">
          <div className="road-line" />
          <div className="roi left">ROI L {pct(cameraMetrics.frontLeftSaturation)}</div>
          <div className="roi right">ROI R {pct(cameraMetrics.frontRightSaturation)}</div>
          <div className="glare-spot" style={{ opacity: Math.min(0.92, cameraMetrics.glare + 0.05) }} />
        </div>
      </div>
      <div className="metric-grid">
        <div className="metric">
          <Camera size={18} />
          <span>After Saturation</span>
          <strong>{pct(Math.max(cameraMetrics.frontLeftSaturation, cameraMetrics.frontRightSaturation))}</strong>
        </div>
        <div className="metric">
          <Activity size={18} />
          <span>Edge Retention</span>
          <strong>{pct(cameraMetrics.edgeDensity)}</strong>
        </div>
        <div className="metric">
          <SunMedium size={18} />
          <span>Glare Index</span>
          <strong>{pct(cameraMetrics.glare)}</strong>
        </div>
      </div>
      <div className="comparison-strip">
        <div>
          <span>Before</span>
          <div className="bar-track"><span style={{ width: pct(beforeSat) }} /></div>
          <strong>{pct(beforeSat)}</strong>
        </div>
        <div>
          <span>After</span>
          <div className="bar-track"><span style={{ width: pct(afterSat) }} /></div>
          <strong>{pct(afterSat)}</strong>
        </div>
        <div>
          <span>Reduction</span>
          <strong>{pct(reduction)}</strong>
        </div>
      </div>
      <div className="vector-panel">
        <div className="compass">
          <span className="compass-arrow" style={{ transform: `translate(-50%, -50%) rotate(${bearing}deg)` }} />
          <b>N</b>
        </div>
        <div>
          <h3>Directional Lux Vector</h3>
          <p>{Math.round(bearing)} deg from vehicle front. High confidence makes the nearest glass respond first.</p>
        </div>
      </div>
      <div className="lux-row">
        {luxValues.map(([label, value]) => (
          <div key={label} className="lux-cell">
            <span>{label}</span>
            <strong>{value === null ? 'MISS' : Math.round(value)}</strong>
          </div>
        ))}
      </div>
    </section>
  );
}
