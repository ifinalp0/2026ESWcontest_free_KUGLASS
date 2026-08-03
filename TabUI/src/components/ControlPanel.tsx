import { useEffect, useRef, useState } from 'react';
import { AlertTriangle, CheckCircle2, ChevronDown, RotateCcw, SlidersHorizontal } from 'lucide-react';
import { channelDisplayName } from '../lib/labels';
import type { ChannelState, ControlCommand, EnvironmentInput } from '../types';

interface Props {
  channels: ChannelState[];
  environment: EnvironmentInput;
  selectedChannel: number;
  sendCommand: (command: ControlCommand) => void;
  controlsEnabled: boolean;
  diagnosticsEnabled: boolean;
}

export function ControlPanel({ channels, environment, selectedChannel, sendCommand, controlsEnabled, diagnosticsEnabled }: Props) {
  const selected = channels[selectedChannel];
  const interactionActive = useRef(false);
  const pendingManual = useRef<{ channel: number; mi: number } | null>(null);
  const controlMi = Number.isFinite(selected.commandedMi) ? selected.commandedMi : selected.targetMi;
  const controllerFrostStrength = Math.round((1 - controlMi) * 100);
  const [manualDraft, setManualDraft] = useState({ channel: selected.channel, frostStrength: controllerFrostStrength });
  const frostStrength = manualDraft.channel === selected.channel ? manualDraft.frostStrength : controllerFrostStrength;
  const manualRemaining = selected.manualUntil ? Math.max(0, Math.round(selected.manualUntil - Date.now() / 1000)) : null;

  useEffect(() => {
    if (manualDraft.channel !== selected.channel) {
      interactionActive.current = false;
      pendingManual.current = null;
      setManualDraft({ channel: selected.channel, frostStrength: controllerFrostStrength });
      return;
    }

    if (!controlsEnabled) {
      interactionActive.current = false;
      pendingManual.current = null;
      if (manualDraft.frostStrength !== controllerFrostStrength) {
        setManualDraft({ channel: selected.channel, frostStrength: controllerFrostStrength });
      }
      return;
    }

    const pending = pendingManual.current;
    if (pending?.channel === selected.channel) {
      if (Math.abs(selected.commandedMi - pending.mi) > 0.005) {
        return;
      }
      pendingManual.current = null;
    }

    if (!interactionActive.current && manualDraft.frostStrength !== controllerFrostStrength) {
      setManualDraft({ channel: selected.channel, frostStrength: controllerFrostStrength });
    }
  }, [controllerFrostStrength, controlsEnabled, manualDraft, selected.channel, selected.commandedMi]);

  const setManual = (value: number) => {
    if (!controlsEnabled || selected.fault) {
      return;
    }
    const frost = Math.max(0, Math.min(100, Math.round(value)));
    const mi = Number((1 - frost / 100).toFixed(3));
    setManualDraft({ channel: selected.channel, frostStrength: frost });
    pendingManual.current = { channel: selected.channel, mi };
    sendCommand({ type: 'setManualChannel', channel: selected.channel, mi, ttlSeconds: 30 });
  };

  const finishManualInteraction = (value: number) => {
    interactionActive.current = false;
    setManual(value);
  };

  const returnToAuto = () => {
    interactionActive.current = false;
    pendingManual.current = null;
    sendCommand({ type: 'returnAuto', channel: selected.channel });
  };

  const setEnvironmentValue = (key: keyof EnvironmentInput, value: number | null) => {
    if (!diagnosticsEnabled) {
      return;
    }
    sendCommand({ type: 'setEnvironment', environment: { [key]: value } });
  };

  return (
    <section className="panel control-panel">
      <div className="panel-heading">
        <div className="panel-title-group">
          <span className="panel-index">03</span>
          <div>
            <h2>채널 제어</h2>
          </div>
        </div>
        <span className={`panel-state ${manualRemaining === null ? 'ok' : 'manual'}`}>
          <SlidersHorizontal size={14} />
          {manualRemaining === null ? 'AUTO' : `TTL ${manualRemaining}s`}
        </span>
      </div>

      <div className="selected-channel-summary">
        <div className="selected-channel-title">
          <span className="selected-channel-id">CH{selected.channel}</span>
          <span>
            <strong>{channelDisplayName(selected.name)}</strong>
          </span>
        </div>
        <div className="channel-source-grid" aria-label="채널 MI 출처별 상태">
          <span title="ESP32_A 정책 엔진이 계산한 목표 MI">
            <small>정책 TARGET</small>
            <strong>MI {selected.targetMi.toFixed(3)}</strong>
          </span>
          <span title="ESP32_A가 ESP32_B로 보낸 명령 MI">
            <small>A COMMANDED</small>
            <strong>MI {selected.commandedMi.toFixed(3)}</strong>
          </span>
          <span title="ESP32_B가 보고한 실제 적용 MI">
            <small>B APPLIED</small>
            <strong>{selected.appliedKnown ? `MI ${selected.appliedMi.toFixed(3)}` : 'STATUS 대기'}</strong>
          </span>
        </div>
      </div>

      <div className="manual-control">
        <div className="control-section-title">
          <span>수동 조절</span>
          <small>{!controlsEnabled ? '장치 연결 대기' : selected.fault ? '고장 중 비활성' : '30초 자동 복귀'}</small>
        </div>
        <div className="range-label">
          <span>투명</span>
          <strong>로컬 DRAFT · 산란 {frostStrength}%</strong>
          <span>강산란</span>
        </div>
        <input
          type="range"
          min="0"
          max="100"
          value={frostStrength}
          disabled={!controlsEnabled || selected.fault}
          aria-disabled={!controlsEnabled || selected.fault}
          onChange={(event) => setManual(Number(event.target.value))}
          onPointerDown={() => {
            interactionActive.current = true;
          }}
          onPointerUp={(event) => finishManualInteraction(Number(event.currentTarget.value))}
          onPointerCancel={(event) => finishManualInteraction(Number(event.currentTarget.value))}
          onBlur={(event) => finishManualInteraction(Number(event.currentTarget.value))}
        />
        {manualRemaining !== null ? (
          <button className="secondary-button" type="button" disabled={!controlsEnabled} onClick={returnToAuto}>
            <RotateCcw size={17} />
            수동 제어 해제 · {manualRemaining}초
          </button>
        ) : null}
      </div>

      <div className="env-controls">
        <div className="control-section-title">
          <span>환경 입력 · HIL</span>
          <small>{diagnosticsEnabled ? '시험 override 활성' : '실측 센서 보호'}</small>
        </div>
        <EnvSlider disabled={!diagnosticsEnabled} label="내부온도" unit="°C" min={15} max={48} value={environment.internalTemp ?? 27} onChange={(value) => setEnvironmentValue('internalTemp', value)} />
        <EnvSlider disabled={!diagnosticsEnabled} label="좌측 ROI 포화" unit="%" min={0} max={100} value={environment.frontLeftSaturation * 100} onChange={(value) => setEnvironmentValue('frontLeftSaturation', value / 100)} />
        <EnvSlider disabled={!diagnosticsEnabled} label="우측 ROI 포화" unit="%" min={0} max={100} value={environment.frontRightSaturation * 100} onChange={(value) => setEnvironmentValue('frontRightSaturation', value / 100)} />
        <EnvSlider disabled={!diagnosticsEnabled} label="Edge Density" unit="%" min={0} max={100} value={environment.edgeDensity * 100} onChange={(value) => setEnvironmentValue('edgeDensity', value / 100)} />
      </div>

      <details className="validation-tools">
        <summary>
          <span><AlertTriangle size={16} /> 고장·결측 검증</span>
          <ChevronDown size={16} className="details-chevron" />
        </summary>
        <div className="validation-content">
          <button
            className={`fault-toggle ${selected.fault ? 'active' : ''}`}
            type="button"
            disabled={!diagnosticsEnabled}
            onClick={() => sendCommand({ type: 'setChannelFault', channel: selected.channel, fault: !selected.fault })}
          >
            {selected.fault ? <CheckCircle2 size={17} /> : <AlertTriangle size={17} />}
            {selected.fault ? `CH${selected.channel} 고장 해제` : `CH${selected.channel} 고장 주입`}
          </button>
        </div>
      </details>
    </section>
  );
}

interface EnvSliderProps {
  label: string;
  unit: string;
  min: number;
  max: number;
  value: number;
  onChange: (value: number) => void;
  disabled?: boolean;
}

function EnvSlider({ label, unit, min, max, value, onChange, disabled = false }: EnvSliderProps) {
  return (
    <label className="env-slider">
      <span>{label}</span>
      <input disabled={disabled} type="range" min={min} max={max} value={value} onChange={(event) => onChange(Number(event.target.value))} />
      <strong>{Math.round(value)}<small>{unit}</small></strong>
    </label>
  );
}
