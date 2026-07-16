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
        <span className="brand-mark">KG</span>
        <div>
          <h1>KUGLASS Simul Twin</h1>
          <p>PDLC 8채널 능동 제어 디지털 트윈</p>
        </div>
      </div>
      <div className="topbar-status">
        <span className={`status-pill ${connected ? 'ok' : 'warn'}`}>
          {connected ? <Wifi size={16} /> : <WifiOff size={16} />}
          {connected ? 'MOCK 연결' : 'MOCK 오프라인'}
        </span>
        <span className="status-pill">
          <Gauge size={16} />
          {vehicleModeLabels[state.vehicleMode]} / {demoModeLabels[state.demoMode]}
        </span>
        <span className={`status-pill ${hasManual ? 'manual' : ''}`}>
          <RadioTower size={16} />
          {hasManual ? '수동 TTL' : '자동 정책'}
        </span>
        <button className="icon-button" type="button" onClick={onSaveReplay} title="리플레이 저장">
          <Download size={18} />
        </button>
        {hasFault ? (
          <button className="icon-button danger" type="button" onClick={onResetFault} title="Fault 플래그 초기화">
            <AlertTriangle size={18} />
          </button>
        ) : null}
      </div>
    </header>
  );
}
