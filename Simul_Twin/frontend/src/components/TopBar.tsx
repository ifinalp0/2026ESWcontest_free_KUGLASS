import { AlertTriangle, Gauge, RadioTower, RotateCcw, Wifi, WifiOff } from 'lucide-react';
import type { SimulationState } from '../types';

interface Props {
  state: SimulationState;
  connected: boolean;
  onResetFault: () => void;
}

export function TopBar({ state, connected, onResetFault }: Props) {
  const hasFault = state.channels.some((channel) => channel.fault);
  const hasManual = state.channels.some((channel) => channel.manualUntil !== null);

  return (
    <header className="topbar">
      <div className="brand">
        <span className="brand-mark">KG</span>
        <div>
          <h1>KUGLASS Simul Twin</h1>
          <p>Mock-only digital twin for PDLC control planning</p>
        </div>
      </div>
      <div className="topbar-status">
        <span className={`status-pill ${connected ? 'ok' : 'warn'}`}>
          {connected ? <Wifi size={16} /> : <WifiOff size={16} />}
          {connected ? 'MOCK Connected' : 'MOCK Offline'}
        </span>
        <span className="status-pill">
          <Gauge size={16} />
          {state.vehicleMode.toUpperCase()} / {state.demoMode.replace('_', ' ').toUpperCase()}
        </span>
        <span className={`status-pill ${hasManual ? 'manual' : ''}`}>
          <RadioTower size={16} />
          {hasManual ? 'MANUAL TTL' : 'AUTO'}
        </span>
        <button className={`icon-button ${hasFault ? 'danger' : ''}`} type="button" onClick={onResetFault} title="Reset fault flags">
          {hasFault ? <AlertTriangle size={18} /> : <RotateCcw size={18} />}
        </button>
      </div>
    </header>
  );
}
