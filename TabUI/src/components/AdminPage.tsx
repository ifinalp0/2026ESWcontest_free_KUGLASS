import { useEffect, useMemo, useState } from 'react';
import {
  Activity,
  ArrowLeft,
  Camera,
  CheckCircle2,
  Clock3,
  Cpu,
  Database,
  Gauge,
  Power,
  Radio,
  RefreshCw,
  RotateCcw,
  Server,
  ShieldAlert,
  SlidersHorizontal,
  Thermometer,
  TriangleAlert
} from 'lucide-react';
import { channelDisplayName } from '../lib/labels';
import { MAX_MI } from '../lib/mi';
import { useTabUIClient } from '../lib/socket';
import { displayedInternalTemperature } from '../lib/temperature';
import type { ChannelState, DownstreamAdcChannel } from '../types';
import '../styles/admin.css';

const backendUrl = (import.meta.env.VITE_BACKEND_URL ?? '').replace(/\/$/, '');
const MAX_ADMIN_MI = MAX_MI;

interface CameraStatus {
  requested: boolean;
  frameReady: boolean;
  sequence: number | null;
  width: number | null;
  height: number | null;
  format: string | null;
  payloadBytes: number | null;
  frameAgeSeconds: number | null;
  goodFrames: number;
  badFrames: number;
  averageFps: number;
  hardwareConnected: boolean;
  transport: 'usb' | 'mock';
}

interface ManualDraft {
  mi: number;
  enable: boolean;
}

type Tone = 'ok' | 'warn' | 'danger' | 'muted';

function buildDrafts(channels: ChannelState[]): ManualDraft[] {
  return channels.map((channel) => ({
    mi: Math.min(MAX_ADMIN_MI, channel.commandedMi),
    enable: channel.commandedEnableKnown ? channel.commandedEnable : true
  }));
}

function boolLabel(value: boolean | null | undefined, yes = 'YES', no = 'NO'): string {
  return value === null || value === undefined ? 'UNKNOWN' : value ? yes : no;
}

function numeric(value: number | null | undefined, digits = 0): string {
  return value === null || value === undefined || !Number.isFinite(value)
    ? '—'
    : value.toFixed(digits);
}

function hexMask(value: number): string {
  return `0x${value.toString(16).padStart(2, '0').toUpperCase()}`;
}

function ageLabel(epochSeconds: number | null): string {
  if (epochSeconds === null) return '수신 기록 없음';
  const seconds = Math.max(0, Date.now() / 1000 - epochSeconds);
  if (seconds < 1) return `${Math.round(seconds * 1000)} ms 전`;
  if (seconds < 60) return `${seconds.toFixed(1)}초 전`;
  return `${Math.floor(seconds / 60)}분 ${Math.round(seconds % 60)}초 전`;
}

function uptimeLabel(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return '—';
  const whole = Math.floor(seconds);
  const hours = Math.floor(whole / 3600);
  const minutes = Math.floor((whole % 3600) / 60);
  const remainder = whole % 60;
  return hours > 0 ? `${hours}h ${minutes}m ${remainder}s` : `${minutes}m ${remainder}s`;
}

function deviceTimestampLabel(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return '—';
  return seconds > 1_000_000_000
    ? new Date(seconds * 1000).toLocaleTimeString('ko-KR', { hour12: false })
    : uptimeLabel(seconds);
}

function manualRemaining(channel: ChannelState): string {
  if (channel.manualPersistent) return 'MANUAL · 유지';
  if (channel.manualUntil === null) return 'AUTO';
  return `TTL ${Math.max(0, Math.ceil(channel.manualUntil - Date.now() / 1000))}s`;
}

function adcValue(mv: number | null, raw: number | null): string {
  if (mv !== null && raw !== null) return `${mv} mV · RAW ${raw}`;
  if (mv !== null) return `${mv} mV`;
  if (raw !== null) return `RAW ${raw}`;
  return 'VALID 데이터 없음';
}

function useCameraStatus(): CameraStatus | null {
  const [status, setStatus] = useState<CameraStatus | null>(null);

  useEffect(() => {
    const controller = new AbortController();
    const refresh = async () => {
      try {
        const response = await fetch(`${backendUrl}/api/camera/status`, {
          cache: 'no-store',
          signal: controller.signal
        });
        if (!response.ok) return;
        setStatus(await response.json() as CameraStatus);
      } catch {
        // The main SERVER status already reports connectivity. Keep the last
        // camera sample so an outage does not look like a fresh zero value.
      }
    };
    void refresh();
    const interval = window.setInterval(() => void refresh(), 1000);
    return () => {
      controller.abort();
      window.clearInterval(interval);
    };
  }, []);

  return status;
}

function StatusBadge({ tone, children }: { tone: Tone; children: React.ReactNode }) {
  return <span className={`admin-status-badge ${tone}`}><i />{children}</span>;
}

function DataRow({ label, value, mono = false }: { label: string; value: React.ReactNode; mono?: boolean }) {
  return (
    <div className="admin-data-row">
      <dt>{label}</dt>
      <dd className={mono ? 'mono' : undefined}>{value}</dd>
    </div>
  );
}

function ConnectionNode({
  label,
  detail,
  tone
}: {
  label: string;
  detail: string;
  tone: Tone;
}) {
  return (
    <div className={`connection-node ${tone}`}>
      <i />
      <span>{label}<small>{detail}</small></span>
    </div>
  );
}

export function AdminPage() {
  const {
    state,
    connected,
    sendCommand,
    refreshController,
    restartServer,
    controllerRefreshing,
    serverRestarting,
    controllerActionError,
    serverActionError
  } = useTabUIClient();
  const cameraStatus = useCameraStatus();
  const [manualMode, setManualMode] = useState(false);
  const [drafts, setDrafts] = useState<ManualDraft[]>(() => buildDrafts(state.channels));
  const [notice, setNotice] = useState('관리자 수동은 잠겨 있습니다. ESP32_A 자동 정책이 제어 권한을 가집니다.');

  const aOnline = connected && state.link.hardwareConnected;
  const bOnline = aOnline && state.link.downstreamHealthy === true;
  const diagnostics = state.downstreamDiagnostics;
  const controllerWideBlock = diagnostics.estopActive === true
    || Boolean(diagnostics.faultCode && !['NONE', 'POWER_STAGE_FAULT'].includes(diagnostics.faultCode));
  const manualSendReady = aOnline && bOnline && !controllerWideBlock;
  const anyChannelFault = state.channels.some((channel) => channel.fault);
  const systemTone: Tone = !connected || !aOnline || !bOnline
    ? 'warn'
    : controllerWideBlock || anyChannelFault
      ? 'danger'
      : 'ok';

  useEffect(() => {
    document.title = 'KUGLASS 관리자';
  }, []);

  useEffect(() => {
    if (!manualMode) setDrafts(buildDrafts(state.channels));
  }, [manualMode, state.channels]);

  const applyManualChange = (channel: number, nextDraft: ManualDraft) => {
    const channelState = state.channels[channel];
    if (!manualMode || !manualSendReady || channelState.fault) return;
    setDrafts((current) => current.map((draft, index) => (
      index === channel ? nextDraft : draft
    )));
    sendCommand({
      type: 'setManualChannel',
      channel,
      mi: nextDraft.mi,
      enable: nextDraft.enable,
      persistent: true
    });
    setNotice(`CH${channel} ${nextDraft.enable ? `ENABLE ON · MI ${nextDraft.mi.toFixed(3)}` : 'ENABLE OFF'} 변경을 즉시 요청했습니다.`);
  };

  const toggleManualMode = () => {
    if (manualMode) {
      if (aOnline) {
        sendCommand({ type: 'returnAuto' });
        setNotice('전체 채널의 관리자 수동을 해제하고 ESP32_A AUTO 정책으로 복귀를 요청했습니다.');
      } else {
        setNotice('연결이 없어 복귀 명령을 보낼 수 없습니다. 관리자 수동은 ESP32_A에서 계속 유지되므로 연결 복구 후 AUTO 복귀를 요청하세요.');
      }
      setManualMode(false);
      return;
    }
    if (!aOnline) return;
    setDrafts(buildDrafts(state.channels));
    setManualMode(true);
    setNotice('관리자 수동이 열렸습니다. 슬라이더와 Enable 변경은 즉시 전송되며 AUTO 복귀 전까지 유지됩니다.');
  };

  const linkSummary = useMemo(() => {
    if (!connected) return 'TabUI 백엔드 응답 없음';
    if (!aOnline) return 'ESP32_A 텔레메트리 대기';
    if (!bOnline) return state.link.downstreamError ?? 'ESP32_B 상태 대기';
    if (controllerWideBlock) return diagnostics.estopActive ? 'E-STOP 활성' : diagnostics.faultCode ?? '출력 차단';
    if (anyChannelFault) return '채널 Fault 감지';
    return '전체 통신 경로 정상';
  }, [aOnline, anyChannelFault, bOnline, connected, controllerWideBlock, diagnostics.estopActive, diagnostics.faultCode, state.link.downstreamError]);

  const cameraValid = state.cameraMetrics.valid === true;
  const adc = diagnostics.adc;
  const controlResult = diagnostics.controlResult;

  return (
    <main className="admin-shell">
      <header className="admin-header">
        <div className="admin-brand">
          <span className="admin-brand-mark"><ShieldAlert size={20} /></span>
          <div>
            <small>KUGLASS · CONTROL PLANE</small>
            <h1>관리자 콘솔</h1>
          </div>
          <StatusBadge tone={systemTone}>{linkSummary}</StatusBadge>
        </div>
        <div className="admin-header-actions">
          <button
            type="button"
            className={`admin-manual-toggle${manualMode ? ' active' : ''}`}
            aria-pressed={manualMode}
            disabled={!manualMode && !aOnline}
            onClick={toggleManualMode}
          >
            <Power size={17} />
            <span>관리자 수동<small>{manualMode ? '활성 · 다시 눌러 AUTO 복귀' : '잠김 · 눌러 제어 열기'}</small></span>
          </button>
          <a className="admin-back-link" href="/demo">
            <ArrowLeft size={16} /> 운영 화면
          </a>
        </div>
      </header>

      <section className="admin-overview" aria-label="전체 시스템 상태">
        <ConnectionNode label="BROWSER" detail="관리자 UI" tone={connected ? 'ok' : 'danger'} />
        <span className="connection-line" aria-hidden="true" />
        <ConnectionNode label="TABUI" detail={connected ? 'HTTP ONLINE' : 'OFFLINE'} tone={connected ? 'ok' : 'danger'} />
        <span className="connection-line" aria-hidden="true" />
        <ConnectionNode label="ESP32_A" detail={aOnline ? (state.link.transport === 'mock' ? 'MOCK' : 'USB LIVE') : 'STALE / OFFLINE'} tone={aOnline ? 'ok' : 'warn'} />
        <span className="connection-line" aria-hidden="true" />
        <ConnectionNode label="ESP32_B" detail={bOnline ? 'UART LINK OK' : 'WAITING / STALE'} tone={bOnline ? 'ok' : 'warn'} />
        <span className="connection-line" aria-hidden="true" />
        <ConnectionNode
          label="OUTPUT"
          detail={controllerWideBlock || anyChannelFault ? 'INHIBITED / PARTIAL' : bOnline ? 'READY' : 'UNKNOWN'}
          tone={controllerWideBlock || anyChannelFault ? 'danger' : bOnline ? 'ok' : 'muted'}
        />
      </section>

      <section className="admin-command-strip" aria-live="polite">
        <div>
          <SlidersHorizontal size={17} />
          <span><strong>{manualMode ? '관리자 수동 즉시 제어' : 'ESP32_A AUTO 제어 유지'}</strong>{notice}</span>
        </div>
        <StatusBadge tone={manualMode && manualSendReady ? 'ok' : manualMode ? 'warn' : 'muted'}>
          {manualMode ? manualSendReady ? 'LIVE CONTROL' : 'CONTROL BLOCKED' : 'AUTO'}
        </StatusBadge>
      </section>

      <section className="admin-grid admin-grid-top">
        <article className="admin-card admin-card-span-2">
          <header className="admin-card-header">
            <div><Radio size={17} /><span><small>LINK MAP</small><h2>연결 및 런타임</h2></span></div>
            <StatusBadge tone={systemTone}>{state.link.transport.toUpperCase()}</StatusBadge>
          </header>
          <div className="admin-card-body split-data">
            <dl>
              <DataRow label="TabUI server" value={connected ? 'ONLINE' : 'OFFLINE'} />
              <DataRow label="Backend runtime" value={state.link.backendRunning ? 'RUNNING' : 'STOPPED'} />
              <DataRow label="Runtime transport" value={state.link.transport.toUpperCase()} mono />
              <DataRow label="USB port" value={state.link.port ?? (state.link.transport === 'mock' ? 'N/A · MOCK' : 'AUTO / 미탐색')} mono />
              <DataRow label="A last telemetry" value={ageLabel(state.link.lastTelemetryAt)} />
              <DataRow label="HIL command" value={state.link.hilEnabled ? 'ENABLED' : 'DISABLED'} />
            </dl>
            <dl>
              <DataRow label="Vehicle mode" value={state.vehicleMode.toUpperCase()} mono />
              <DataRow label="Demo mode" value={state.demoMode.toUpperCase()} mono />
              <DataRow label="State schema" value={`v${state.schemaVersion}`} mono />
              <DataRow label="A state seq" value={numeric(state.controllerDiagnostics.stateSeq)} mono />
              <DataRow label="A timestamp" value={deviceTimestampLabel(state.timestamp)} mono />
            </dl>
          </div>
          <footer className="admin-card-actions">
            <button type="button" disabled={!connected || state.link.transport === 'mock' || controllerRefreshing || serverRestarting} onClick={refreshController}>
              <RefreshCw className={controllerRefreshing ? 'spin' : undefined} size={14} />
              {controllerRefreshing ? 'ESP32_A 갱신 중' : 'ESP32_A 재연결'}
            </button>
            <button type="button" disabled={!connected || serverRestarting} onClick={restartServer}>
              <Server size={14} />{serverRestarting ? '서버 재시작 중' : 'TabUI 재시작'}
            </button>
            {controllerActionError || serverActionError ? <span className="admin-inline-error">{controllerActionError ?? serverActionError}</span> : null}
          </footer>
        </article>

        <article className="admin-card">
          <header className="admin-card-header">
            <div><Cpu size={17} /><span><small>ESP32_A</small><h2>Master 진단</h2></span></div>
            <StatusBadge tone={aOnline ? 'ok' : 'warn'}>{aOnline ? 'FRESH' : 'STALE'}</StatusBadge>
          </header>
          <dl className="admin-card-body">
            <DataRow label="Protocol" value={state.controllerDiagnostics.protocolVersion === null ? '—' : `v${state.controllerDiagnostics.protocolVersion}`} mono />
            <DataRow label="Role" value={state.controllerDiagnostics.role ?? '—'} mono />
            <DataRow label="Source session" value={numeric(state.controllerDiagnostics.sourceSessionId)} mono />
            <DataRow label="Downstream ready" value={boolLabel(state.controllerDiagnostics.downstreamReady, 'READY', 'NOT READY')} />
            <DataRow label="Firmware diagnostics" value={boolLabel(state.controllerDiagnostics.firmwareDiagnosticsEnabled, 'ENABLED', 'DISABLED')} />
            <DataRow label="Thermal risk" value={state.controllerDiagnostics.thermalRisk === null ? '—' : `${(state.controllerDiagnostics.thermalRisk * 100).toFixed(1)}%`} mono />
            <DataRow label="Decision" value={state.decisionReason} />
          </dl>
        </article>

        <article className="admin-card">
          <header className="admin-card-header">
            <div><Activity size={17} /><span><small>PROTOCOL</small><h2>Sequence · ACK</h2></span></div>
            <StatusBadge tone={state.link.lastAckOk === false ? 'danger' : state.link.lastAckOk === true ? 'ok' : 'muted'}>
              {state.link.lastAckOk === null ? 'NO ACK' : state.link.lastAckOk ? 'ACK OK' : 'REJECTED'}
            </StatusBadge>
          </header>
          <dl className="admin-card-body">
            <DataRow label="Last command seq" value={numeric(state.link.lastCommandSeq)} mono />
            <DataRow label="Last ACK seq" value={numeric(state.link.lastAckSeq)} mono />
            <DataRow label="Last ACK command" value={state.link.lastAckCommand ?? '—'} mono />
            <DataRow label="ACK result" value={boolLabel(state.link.lastAckOk, 'OK', 'FAIL')} />
            <DataRow label="ACK error" value={state.link.lastAckError ?? 'NONE'} mono />
            <DataRow label="Gateway error" value={state.link.error ?? 'NONE'} mono />
          </dl>
        </article>
      </section>

      <section className="admin-section-heading">
        <div><small>ACTUATOR COMMAND / STATUS</small><h2>CH0–CH3 제어 상태</h2></div>
        <p>A가 B로 보내는 Enable·commanded MI와 B가 회신한 applied MI를 분리합니다. B status에는 Enable 직접 피드백이 없습니다.</p>
      </section>

      <section className="admin-channel-grid">
        {state.channels.map((channel) => {
          const draft = drafts[channel.channel];
          const sense = adc.channels[channel.channel];
          const locked = !manualMode || !manualSendReady || channel.fault;
          return (
            <article className={`admin-channel-card${channel.fault ? ' fault' : ''}`} key={channel.channel}>
              <header>
                <div className="admin-channel-identity">
                  <span>CH{channel.channel}</span>
                  <div><h3>{channelDisplayName(channel.name)}</h3><small>{manualRemaining(channel)}</small></div>
                </div>
                <StatusBadge tone={channel.fault ? 'danger' : channel.commandedEnableKnown && !channel.commandedEnable ? 'warn' : 'ok'}>
                  {channel.fault ? 'FAULT' : channel.commandedEnableKnown ? `A→B EN ${channel.commandedEnable ? 'ON' : 'OFF'}` : 'EN UNKNOWN'}
                </StatusBadge>
              </header>

              <div className="admin-mi-grid">
                <div><small>A POLICY TARGET</small><strong>{channel.targetMi.toFixed(3)}</strong><span>MI</span></div>
                <div><small>A→B COMMANDED</small><strong>{channel.commandedMi.toFixed(3)}</strong><span>MI</span></div>
                <div className={channel.appliedKnown ? '' : 'unknown'}><small>B APPLIED</small><strong>{channel.appliedKnown ? channel.appliedMi.toFixed(3) : '—'}</strong><span>{channel.appliedKnown ? 'MI' : '대기'}</span></div>
              </div>

              <div className="admin-channel-meta">
                <span><small>A master fault</small><b>{channel.masterFault ? 'TRUE' : 'FALSE'}</b></span>
                <span><small>B channel fault</small><b>{channel.downstreamFault ? 'TRUE' : 'FALSE'}</b></span>
                <span title="ESP32_B status는 적용 Enable을 직접 회신하지 않습니다."><small>B applied enable</small><b>미제공</b></span>
                <span><small>Policy optical</small><b>{channel.policyOpticalState ?? '—'}</b></span>
              </div>

              <div className="admin-adc-inline">
                <span><Gauge size={14} /><small>Current sense</small><b>{adcChannelValue(sense, 'current')}</b></span>
                <span><Thermometer size={14} /><small>Temperature sense</small><b>{adcChannelValue(sense, 'temperature')}</b></span>
              </div>

              <div className={`admin-channel-controls${locked ? ' locked' : ''}`}>
                <div className="admin-control-row">
                  <span><small>수동 Enable</small><b>{draft.enable ? 'ON' : 'OFF'}</b></span>
                  <button
                    type="button"
                    className={`admin-switch${draft.enable ? ' on' : ''}`}
                    role="switch"
                    aria-checked={draft.enable}
                    aria-label={`CH${channel.channel} 수동 Enable`}
                    disabled={locked}
                    onClick={() => applyManualChange(channel.channel, { ...draft, enable: !draft.enable })}
                  ><i /></button>
                </div>
                <label className="admin-mi-control">
                  <span><small>수동 MI</small><b>{draft.mi.toFixed(3)}</b></span>
                  <input
                    type="range"
                    min="0"
                    max={MAX_ADMIN_MI}
                    step="0.01"
                    value={draft.mi}
                    disabled={locked || !draft.enable}
                    onChange={(event) => applyManualChange(channel.channel, { ...draft, mi: Number(event.target.value) })}
                  />
                </label>
                <div className="admin-persistent-control">
                  <span>관리자 수동 유지</span>
                  <b>AUTO 복귀 전까지</b>
                </div>
              </div>
            </article>
          );
        })}
      </section>

      <section className="admin-grid admin-diagnostics-grid">
        <article className="admin-card">
          <header className="admin-card-header">
            <div><Cpu size={17} /><span><small>ESP32_B</small><h2>Boot · Safety</h2></span></div>
            <StatusBadge tone={bOnline ? diagnostics.operationalFault ? 'danger' : 'ok' : 'warn'}>{bOnline ? diagnostics.operationalFault ? 'FAULT' : 'ONLINE' : 'STALE'}</StatusBadge>
          </header>
          <dl className="admin-card-body">
            <DataRow label="Boot ID" value={numeric(diagnostics.bootId)} mono />
            <DataRow label="Status seq" value={numeric(diagnostics.statusSeq)} mono />
            <DataRow label="Reset challenge" value={numeric(diagnostics.resetChallenge)} mono />
            <DataRow label="E-Stop" value={boolLabel(diagnostics.estopActive, 'ACTIVE', 'CLEAR')} />
            <DataRow label="Fault code" value={diagnostics.faultCode ?? '—'} mono />
            <DataRow label="Diagnostic" value={diagnostics.diagnostic ?? '—'} mono />
            <DataRow label="A↔B link error" value={state.link.downstreamError ?? '—'} mono />
          </dl>
          {diagnostics.operationalFault || anyChannelFault ? (
            <footer className="admin-card-actions">
              <button type="button" className="danger" disabled={!aOnline} onClick={() => sendCommand({ type: 'resetFault' })}>
                <TriangleAlert size={14} /> Fault reset 요청
              </button>
            </footer>
          ) : null}
        </article>

        <article className="admin-card admin-card-span-2">
          <header className="admin-card-header">
            <div><Database size={17} /><span><small>ESP32_B ADC</small><h2>8채널 원시 진단</h2></span></div>
            <StatusBadge tone={adc.initialized ? 'ok' : 'warn'}>{adc.initialized ? 'INITIALIZED' : 'WAITING'}</StatusBadge>
          </header>
          <div className="admin-adc-summary">
            <span>RAW VALID <b>{hexMask(adc.rawValidMask)}</b></span>
            <span>mV VALID <b>{hexMask(adc.mvValidMask)}</b></span>
            <span>I ADC→mV <b>{adc.currentCalibrated ? 'YES' : 'NO'}</b></span>
            <span>T ADC→mV <b>{adc.temperatureCalibrated ? 'YES' : 'NO'}</b></span>
          </div>
          <div className="admin-table-wrap">
            <table className="admin-adc-table">
              <thead><tr><th>Channel</th><th>I RAW</th><th>I mV</th><th>T RAW</th><th>T mV</th></tr></thead>
              <tbody>
                {adc.channels.map((channel) => (
                  <tr key={channel.channel}>
                    <th>CH{channel.channel}</th>
                    <td>{numeric(channel.currentRaw)}</td>
                    <td>{numeric(channel.currentMv)}</td>
                    <td>{numeric(channel.temperatureRaw)}</td>
                    <td>{numeric(channel.temperatureMv)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <p className="admin-footnote">표시는 ADC raw/mV 진단값입니다. 보드별 보정 전에는 전류 A 또는 온도 °C로 해석하지 않습니다.</p>
        </article>

        <article className="admin-card">
          <header className="admin-card-header">
            <div><Camera size={17} /><span><small>ESP32_A CAMERA</small><h2>센서 · 영상 경로</h2></span></div>
            <StatusBadge tone={cameraValid ? 'ok' : 'warn'}>{cameraValid ? 'VALID' : state.cameraMetrics.valid === false ? 'INVALID' : 'UNKNOWN'}</StatusBadge>
          </header>
          <dl className="admin-card-body">
            <DataRow label="Internal temp" value={`${displayedInternalTemperature(state.environment).toFixed(2)} °C`} mono />
            <DataRow label="AE metadata" value={boolLabel(state.cameraMetrics.aeMetadataValid, 'VALID', 'EXCLUDED')} />
            <DataRow label="Driver-left saturation" value={`${(state.cameraMetrics.frontLeftSaturation * 100).toFixed(1)}%`} mono />
            <DataRow label="Passenger-right saturation" value={`${(state.cameraMetrics.frontRightSaturation * 100).toFixed(1)}%`} mono />
            <DataRow label="Edge density" value={`${(state.cameraMetrics.edgeDensity * 100).toFixed(1)}%`} mono />
            <DataRow label="Glare" value={state.cameraMetrics.glare.toFixed(4)} mono />
            <DataRow label="Frame ID" value={numeric(state.cameraMetrics.frameId)} mono />
            <DataRow label="Camera timestamp" value={deviceTimestampLabel(state.cameraMetrics.timestamp)} mono />
          </dl>
          <div className="admin-camera-strip">
            <span><small>STREAM</small><b>{cameraStatus?.requested ? 'REQUESTED' : 'IDLE'}</b></span>
            <span><small>JPEG</small><b>{cameraStatus?.frameReady ? `${cameraStatus.width}×${cameraStatus.height}` : 'NO FRAME'}</b></span>
            <span><small>FPS</small><b>{numeric(cameraStatus?.averageFps, 1)}</b></span>
            <span><small>GOOD / BAD</small><b>{cameraStatus ? `${cameraStatus.goodFrames} / ${cameraStatus.badFrames}` : '—'}</b></span>
          </div>
        </article>

        <article className="admin-card">
          <header className="admin-card-header">
            <div><CheckCircle2 size={17} /><span><small>CONTROL RESULT</small><h2>Fault reset 결과</h2></span></div>
            <StatusBadge tone={!controlResult ? 'muted' : controlResult.ok ? 'ok' : 'danger'}>{!controlResult ? 'NO RESULT' : controlResult.ok ? 'SUCCESS' : 'FAILED'}</StatusBadge>
          </header>
          <dl className="admin-card-body">
            <DataRow label="Command" value={controlResult?.command ?? '—'} mono />
            <DataRow label="Request seq" value={numeric(controlResult?.seq)} mono />
            <DataRow label="Source session" value={numeric(controlResult?.sourceSessionId)} mono />
            <DataRow label="Result" value={!controlResult ? '—' : controlResult.ok ? 'OK' : 'FAIL'} />
            <DataRow label="Error" value={controlResult?.error ?? '—'} mono />
          </dl>
          <div className="admin-result-note">
            <Clock3 size={15} /> B boot · A session · request seq가 일치한 최종 결과만 표시합니다.
          </div>
        </article>
      </section>

      <details className="admin-raw-state">
        <summary><Database size={15} /> 수신 상태 원본 보기</summary>
        <pre>{JSON.stringify({ state, cameraStatus }, null, 2)}</pre>
      </details>

      <footer className="admin-footer">
        <span><ShieldAlert size={14} /> 제어 우선순위: E-Stop &gt; latched Fault &gt; 관리자 수동/일반 수동 TTL &gt; Demo/Auto</span>
        <span>최대 관리자 MI {MAX_ADMIN_MI.toFixed(2)} · A→B heartbeat TTL은 ESP32_A가 소유</span>
      </footer>
    </main>
  );
}

function adcChannelValue(
  channel: DownstreamAdcChannel | undefined,
  source: 'current' | 'temperature'
): string {
  if (!channel) return '—';
  return source === 'current'
    ? adcValue(channel.currentMv, channel.currentRaw)
    : adcValue(channel.temperatureMv, channel.temperatureRaw);
}
