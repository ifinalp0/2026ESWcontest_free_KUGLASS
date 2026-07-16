import { RotateCcw, SlidersHorizontal } from 'lucide-react';
import { opticalStateLabels } from '../lib/labels';
import type { ChannelState, ControlCommand, EnvironmentInput } from '../types';

interface Props {
  channels: ChannelState[];
  environment: EnvironmentInput;
  selectedChannel: number;
  sendCommand: (command: ControlCommand) => void;
}

export function ControlPanel({ channels, environment, selectedChannel, sendCommand }: Props) {
  const selected = channels[selectedChannel];
  const frostStrength = Math.round((1 - selected.appliedMi) * 100);
  const manualRemaining = selected.manualUntil ? Math.max(0, Math.round(selected.manualUntil - Date.now() / 1000)) : null;

  const setManual = (value: number) => {
    const mi = Number((1 - value / 100).toFixed(3));
    sendCommand({ type: 'setManualChannel', channel: selected.channel, mi, ttlSeconds: 30 });
  };

  const setEnvironmentValue = (key: keyof EnvironmentInput, value: number) => {
    sendCommand({ type: 'setEnvironment', environment: { [key]: value } });
  };

  return (
    <section className="panel control-panel">
      <div className="panel-heading">
        <div>
          <h2>제어</h2>
          <p>{selected.name}</p>
        </div>
        <SlidersHorizontal size={20} />
      </div>

      <div className="selected-channel-summary">
        <div className="selected-channel-title">
          <span>선택 채널</span>
          <strong>{selected.name}</strong>
        </div>
        <dl className="channel-kpis">
          <div>
            <dt>상태</dt>
            <dd>{opticalStateLabels[selected.opticalState]}</dd>
          </div>
          <div>
            <dt>목표 MI</dt>
            <dd>{Math.round(selected.targetMi * 100)}%</dd>
          </div>
          <div>
            <dt>적용 MI</dt>
            <dd>{Math.round(selected.appliedMi * 100)}%</dd>
          </div>
        </dl>
      </div>

      <div className="manual-control">
        <div className="range-label">
          <span>투명</span>
          <strong>산란 {frostStrength}%</strong>
          <span>강산란</span>
        </div>
        <input
          type="range"
          min="0"
          max="100"
          value={frostStrength}
          onChange={(event) => setManual(Number(event.target.value))}
        />
        {manualRemaining === null ? (
          <p className="control-note">자동 정책 제어 중</p>
        ) : (
          <button className="secondary-button" type="button" onClick={() => sendCommand({ type: 'returnAuto', channel: selected.channel })}>
            <RotateCcw size={17} />
            수동 제어 해제 · {manualRemaining}초
          </button>
        )}
      </div>

      <div className="env-controls">
        <h3>환경 입력</h3>
        <EnvSlider label="외기온" min={15} max={45} value={environment.weatherTemp} onChange={(value) => setEnvironmentValue('weatherTemp', value)} />
        <EnvSlider label="내부온도" min={15} max={48} value={environment.internalTemp} onChange={(value) => setEnvironmentValue('internalTemp', value)} />
        <EnvSlider label="전방 조도" min={0} max={1300} value={environment.frontLux ?? 0} onChange={(value) => setEnvironmentValue('frontLux', value)} />
        <EnvSlider label="우측 조도" min={0} max={1300} value={environment.rightLux ?? 0} onChange={(value) => setEnvironmentValue('rightLux', value)} />
        <EnvSlider label="후방 조도" min={0} max={1300} value={environment.rearLux ?? 0} onChange={(value) => setEnvironmentValue('rearLux', value)} />
        <EnvSlider label="좌측 조도" min={0} max={1300} value={environment.leftLux ?? 0} onChange={(value) => setEnvironmentValue('leftLux', value)} />
        <EnvSlider label="상부 조도" min={0} max={1300} value={environment.topLux ?? 0} onChange={(value) => setEnvironmentValue('topLux', value)} />
      </div>
    </section>
  );
}

interface EnvSliderProps {
  label: string;
  min: number;
  max: number;
  value: number;
  onChange: (value: number) => void;
}

function EnvSlider({ label, min, max, value, onChange }: EnvSliderProps) {
  return (
    <label className="env-slider">
      <span>{label}</span>
      <input type="range" min={min} max={max} value={value} onChange={(event) => onChange(Number(event.target.value))} />
      <strong>{Math.round(value)}</strong>
    </label>
  );
}
