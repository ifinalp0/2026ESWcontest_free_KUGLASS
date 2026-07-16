import { AlertTriangle, Download, Gauge, RadioTower, Wifi, WifiOff } from 'lucide-react';
import { demoModeLabels, vehicleModeLabels } from '../lib/labels';
import type { SimulationState } from '../types';

interface Props {
  state: SimulationState;
  connected: boolean;
  onResetFault: () => void;
  onSaveReplay: () => void;
}

export function TopBar({ state, connected, onResetFault, onSaveReplay }: Props) {
  const hasFault = state.channels.some((channel) => channel.fault);
  const hasManual = state.channels.some((channel) => channel.manualUntil !== null);

  return (
    <header className="topbar">
      <div className="brand">
        <span className="brand-mark" aria-hidden="true">
          <i />
          <i />
          <i />
          <i />
        </span>
        <div className="brand-copy">
          <span className="brand-overline">ACTIVE GLASS CONTROL LAB</span>
          <div className="brand-title-row">
            <h1>KUGLASS <span>SIMUL TWIN</span></h1>
            <span className="mock-badge">MOCK ONLY</span>
          </div>
          <p>IONIQ 5 · PDLC 8채널 정책 검증 콘솔</p>
        </div>
      </div>
      <div className="topbar-status">
        <div className={`status-block ${connected ? 'ok' : 'warn'}`}>
          {connected ? <Wifi size={17} /> : <WifiOff size={17} />}
          <span>
            SIM LINK
            <strong>{connected ? 'MOCK 연결' : '오프라인 모드'}</strong>
          </span>
        </div>
        <div className="status-block">
          <Gauge size={16} />
          <span>
            VEHICLE
            <strong>{vehicleModeLabels[state.vehicleMode]} · {demoModeLabels[state.demoMode]}</strong>
          </span>
        </div>
        <div className={`status-block ${hasManual ? 'manual' : 'ok'}`}>
          <RadioTower size={16} />
          <span>
            CONTROL
            <strong>{hasManual ? '수동 TTL 활성' : '자동 정책 운전'}</strong>
          </span>
        </div>
        <button className="topbar-action" type="button" onClick={onSaveReplay} title="리플레이 저장">
          <Download size={17} />
          <span>리플레이 저장</span>
        </button>
        {hasFault ? (
          <button className="topbar-action danger" type="button" onClick={onResetFault} title="Fault 플래그 초기화">
            <AlertTriangle size={17} />
            <span>고장 초기화</span>
          </button>
        ) : null}
      </div>
    </header>
  );
}
