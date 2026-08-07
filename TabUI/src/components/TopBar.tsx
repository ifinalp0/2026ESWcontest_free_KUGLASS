import type { KeyboardEvent } from 'react';
import { AlertTriangle, Cable, Cpu, Download, Power, RefreshCw, ShieldAlert } from 'lucide-react';
import type { SimulationState } from '../types';

interface Props {
  state: SimulationState;
  connected: boolean;
  onToggleTheme: () => void;
  onResetFault: () => void;
  onSaveReplay: () => void;
  onRefreshController: () => void;
  onToggleBackend: () => void;
  controllerRefreshing: boolean;
  backendPowerChanging: boolean;
  controllerActionError: string | null;
  backendPowerError: string | null;
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

export function TopBar({
  state,
  connected,
  onToggleTheme,
  onResetFault,
  onSaveReplay,
  onRefreshController,
  onToggleBackend,
  controllerRefreshing,
  backendPowerChanging,
  controllerActionError,
  backendPowerError
}: Props) {
  const hasFault = state.channels.some((channel) => channel.fault);
  const diagnostics = state.downstreamDiagnostics;
  const hasOperationalFault = diagnostics.operationalFault || hasFault;
  const isMock = state.link.transport === 'mock';
  const backendRunning = connected && state.link.backendRunning;
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
        <button
          className={`status-block compact interactive ${backendRunning && !backendPowerError ? 'ok' : 'warn'}`}
          type="button"
          disabled={!connected || controllerRefreshing || backendPowerChanging}
          onClick={onToggleBackend}
          title={backendPowerError ?? (!connected ? 'TabUI HTTP 제어 셸 연결 끊김' : backendRunning ? 'ESP32_A gateway 백엔드 종료' : 'ESP32_A gateway 백엔드 시동')}
          aria-label={backendRunning ? 'TabUI 백엔드 종료' : 'TabUI 백엔드 시동'}
        >
          {backendPowerChanging ? <RefreshCw className="spin" size={17} /> : <Power size={17} />}
          <span>BACKEND<strong>{backendPowerChanging ? backendRunning ? 'STOPPING' : 'STARTING' : backendPowerError ? 'FAILED' : !connected ? 'OFFLINE' : backendRunning ? 'RUNNING' : 'STOPPED'}</strong></span>
        </button>
        <button
          className={`status-block compact interactive ${hasDeviceError || controllerActionError ? 'warn' : deviceConnected ? 'ok' : 'warn'}`}
          type="button"
          disabled={!connected || !state.link.backendRunning || isMock || controllerRefreshing || backendPowerChanging}
          onClick={onRefreshController}
          title={controllerActionError ?? (isMock ? 'MOCK 모드에는 실제 ESP32_A 연결이 없습니다' : state.link.error ?? 'ESP32_A USB 연결 다시 탐색')}
          aria-label="ESP32_A USB 연결 갱신"
        >
          {controllerRefreshing ? <RefreshCw className="spin" size={17} /> : <Cpu size={17} />}
          <span>CONTROLLER<strong>{controllerRefreshing ? 'REFRESHING' : controllerActionError ? 'FAILED' : hasDeviceError ? 'REJECTED' : !connected ? 'WAITING' : isMock ? 'MOCK' : deviceConnected ? 'ESP32_A' : 'WAITING'}</strong></span>
        </button>
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
        <button
          className="topbar-action admin-entry"
          type="button"
          onClick={() => window.location.assign('/admin')}
          title="관리자 페이지 열기"
          aria-label="관리자 페이지 열기"
        >
          <ShieldAlert size={17} />
          <span>관리자</span>
        </button>
      </div>
    </header>
  );
}
