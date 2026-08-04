import type { KeyboardEvent } from 'react';
import { AlertTriangle, Cable, Cpu, Download, Server, WifiOff } from 'lucide-react';
import type { SimulationState } from '../types';

interface Props {
  state: SimulationState;
  connected: boolean;
  onToggleTheme: () => void;
  onResetFault: () => void;
  onSaveReplay: () => void;
}

type DownstreamStatus = 'ok' | 'waiting' | 'stale' | 'fault';

function downstreamStatus(connected: boolean, healthy: boolean | null, error: string | null): DownstreamStatus {
  if (!connected) {
    return 'waiting';
  }

  const normalizedError = (error ?? '').trim().toUpperCase();
  if (normalizedError.includes('WAITING_B')) {
    return 'waiting';
  }
  if (normalizedError === 'B_STATUS_TIMEOUT') {
    return 'stale';
  }
  if (normalizedError && normalizedError !== 'OK' && normalizedError !== 'NONE') {
    return 'fault';
  }
  if (healthy === true) {
    return 'ok';
  }
  return 'waiting';
}

export function TopBar({ state, connected, onToggleTheme, onResetFault, onSaveReplay }: Props) {
  const hasFault = state.channels.some((channel) => channel.fault);
  const diagnostics = state.downstreamDiagnostics;
  const hasOperationalFault = diagnostics.operationalFault || hasFault;
  const isMock = state.link.transport === 'mock';
  const deviceConnected = connected && state.link.hardwareConnected;
  const hasDeviceError = connected && Boolean(state.link.error);
  const downstream = state.link.downstreamHealthy === true && hasOperationalFault
    ? 'fault'
    : downstreamStatus(deviceConnected, state.link.downstreamHealthy, state.link.downstreamError);
  const downstreamFault = downstream === 'fault';
  const systemError = hasDeviceError || downstreamFault;
  const deviceLabel = !connected ? 'OFFLINE' : systemError ? 'ERROR' : isMock ? 'MOCK' : deviceConnected ? 'LIVE' : 'OFFLINE';
  const downstreamLabel = diagnostics.estopActive
    ? 'E-STOP'
    : downstream === 'ok'
      ? 'LINK OK'
      : downstream === 'stale'
        ? 'STALE'
        : downstream === 'fault'
          ? 'FAULT'
          : 'WAITING';
  const diagnosticTitle = [
    state.link.downstreamError,
    diagnostics.faultCode ? `fault=${diagnostics.faultCode}` : null,
    diagnostics.diagnostic ? `diagnostic=${diagnostics.diagnostic}` : null,
    diagnostics.bootId !== null ? `boot=${diagnostics.bootId}` : null,
    diagnostics.statusSeq !== null ? `status seq=${diagnostics.statusSeq}` : null,
  ].filter(Boolean).join(' · ') || 'ESP32_A → ESP32_B link';
  const resetResult = diagnostics.controlResult;
  const visibleDiagnostic = diagnostics.diagnostic
    && !(resetResult && diagnostics.diagnostic.startsWith('RESET_'))
    ? diagnostics.diagnostic
    : null;
  const diagnosticOk = visibleDiagnostic === 'BOOT' || visibleDiagnostic === 'MOCK';

  const onTitleKeyDown = (event: KeyboardEvent<HTMLHeadingElement>) => {
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      onToggleTheme();
    }
  };

  return (
    <header className="topbar">
      <div className="brand">
        <div className="brand-copy">
          <div className="brand-title-row">
            <h1
              role="button"
              tabIndex={0}
              onDoubleClick={onToggleTheme}
              onKeyDown={onTitleKeyDown}
              title="더블클릭하여 브랜드 테마 전환"
              aria-label="KUGLASS CONTROL. 더블클릭하여 색상 테마 전환"
            >
              KUGLASS <span>CONTROL</span>
            </h1>
            <span className={`mock-badge${systemError || !deviceConnected ? ' offline' : isMock ? '' : ' live'}`}>{deviceLabel}</span>
          </div>
        </div>
      </div>
      <div className="topbar-status">
        <div className={`status-block compact ${connected ? 'ok' : 'warn'}`} title={connected ? 'TabUI 백엔드 연결됨' : 'TabUI 백엔드 연결 끊김'}>
          {connected ? <Server size={17} /> : <WifiOff size={17} />}
          <span>SERVER<strong>{connected ? 'ONLINE' : 'OFFLINE'}</strong></span>
        </div>
        <div className={`status-block compact ${hasDeviceError ? 'warn' : deviceConnected ? 'ok' : 'warn'}`} title={state.link.error ?? `ESP32_A ${deviceLabel}`}>
          <Cpu size={17} />
          <span>CONTROLLER<strong>{hasDeviceError ? 'REJECTED' : !connected ? 'WAITING' : isMock ? 'MOCK' : deviceConnected ? 'ESP32_A' : 'WAITING'}</strong></span>
        </div>
        <div
          className={`status-block compact ${downstream === 'ok' ? 'ok' : 'warn'}`}
          title={diagnosticTitle}
        >
          <Cable size={17} />
          <span>ESP32_B<strong>{downstreamLabel}</strong></span>
        </div>
        {resetResult ? (
          <span
            className={`reset-result ${resetResult.ok ? 'ok' : 'warn'}`}
            title={`reset_fault seq=${resetResult.seq} · ${resetResult.error ?? 'NONE'}`}
          >
            RESET {resetResult.ok ? 'OK' : 'FAIL'}
          </span>
        ) : null}
        {visibleDiagnostic ? (
          <span
            className={`reset-result ${diagnosticOk ? 'ok' : 'warn'}`}
            title={`ESP32_B diagnostic: ${visibleDiagnostic}`}
          >
            {visibleDiagnostic.replace(/_/g, ' ')}
          </span>
        ) : null}
        <button className="topbar-action icon-only" type="button" onClick={onSaveReplay} title="리플레이 저장" aria-label="리플레이 저장">
          <Download size={17} />
        </button>
        {hasOperationalFault ? (
          <button className="topbar-action danger icon-only" type="button" disabled={!deviceConnected} onClick={onResetFault} title="고장 상태 초기화" aria-label="고장 상태 초기화">
            <AlertTriangle size={17} />
          </button>
        ) : null}
      </div>
    </header>
  );
}
