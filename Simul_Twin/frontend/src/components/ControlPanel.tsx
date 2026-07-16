import { useRef } from 'react';
import { AlertTriangle, CheckCircle2, ChevronDown, RotateCcw, SlidersHorizontal, Unplug } from 'lucide-react';
import { channelDisplayName } from '../lib/labels';
import type { ChannelState, ControlCommand, EnvironmentInput } from '../types';

interface Props {
  channels: ChannelState[];
  environment: EnvironmentInput;
  selectedChannel: number;
  sendCommand: (command: ControlCommand) => void;
}

type LuxSensorKey = 'frontLux' | 'rightLux' | 'rearLux' | 'leftLux' | 'topLux';

const luxSensors: Array<{ key: LuxSensorKey; label: string; fallback: number }> = [
  { key: 'frontLux', label: '전방', fallback: 280 },
  { key: 'rightLux', label: '우측', fallback: 180 },
  { key: 'rearLux', label: '후방', fallback: 140 },
  { key: 'leftLux', label: '좌측', fallback: 170 },
  { key: 'topLux', label: '상부', fallback: 260 }
];

export function ControlPanel({ channels, environment, selectedChannel, sendCommand }: Props) {
  const selected = channels[selectedChannel];
  const sensorMemory = useRef<Partial<Record<LuxSensorKey, number>>>({});
  const frostStrength = Math.round((1 - selected.appliedMi) * 100);
  const manualRemaining = selected.manualUntil ? Math.max(0, Math.round(selected.manualUntil - Date.now() / 1000)) : null;

  const setManual = (value: number) => {
    if (selected.fault) {
      return;
    }
    const mi = Number((1 - value / 100).toFixed(3));
    sendCommand({ type: 'setManualChannel', channel: selected.channel, mi, ttlSeconds: 30 });
  };

  const setEnvironmentValue = (key: keyof EnvironmentInput, value: number | null) => {
    sendCommand({ type: 'setEnvironment', environment: { [key]: value } });
  };

  const toggleLuxSensor = (key: LuxSensorKey, fallback: number) => {
    const current = environment[key];
    if (current === null) {
      setEnvironmentValue(key, sensorMemory.current[key] ?? fallback);
      return;
    }
    sensorMemory.current[key] = current;
    setEnvironmentValue(key, null);
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
      </div>

      <div className="manual-control">
        <div className="control-section-title">
          <span>수동 조절</span>
          <small>{selected.fault ? '고장 중 비활성' : '30초 자동 복귀'}</small>
        </div>
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
          disabled={selected.fault}
          aria-disabled={selected.fault}
          onChange={(event) => setManual(Number(event.target.value))}
        />
        {manualRemaining !== null ? (
          <button className="secondary-button" type="button" onClick={() => sendCommand({ type: 'returnAuto', channel: selected.channel })}>
            <RotateCcw size={17} />
            수동 제어 해제 · {manualRemaining}초
          </button>
        ) : null}
      </div>

      <div className="env-controls">
        <div className="control-section-title">
          <span>환경 입력</span>
        </div>
        <EnvSlider label="외기온" unit="°C" min={15} max={45} value={environment.weatherTemp} onChange={(value) => setEnvironmentValue('weatherTemp', value)} />
        <EnvSlider label="내부온도" unit="°C" min={15} max={48} value={environment.internalTemp} onChange={(value) => setEnvironmentValue('internalTemp', value)} />
        <EnvSlider label="전방 조도" unit="lx" min={0} max={1300} value={environment.frontLux ?? 0} onChange={(value) => setEnvironmentValue('frontLux', value)} />
        <EnvSlider label="우측 조도" unit="lx" min={0} max={1300} value={environment.rightLux ?? 0} onChange={(value) => setEnvironmentValue('rightLux', value)} />
        <EnvSlider label="후방 조도" unit="lx" min={0} max={1300} value={environment.rearLux ?? 0} onChange={(value) => setEnvironmentValue('rearLux', value)} />
        <EnvSlider label="좌측 조도" unit="lx" min={0} max={1300} value={environment.leftLux ?? 0} onChange={(value) => setEnvironmentValue('leftLux', value)} />
        <EnvSlider label="상부 조도" unit="lx" min={0} max={1300} value={environment.topLux ?? 0} onChange={(value) => setEnvironmentValue('topLux', value)} />
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
            onClick={() => sendCommand({ type: 'setChannelFault', channel: selected.channel, fault: !selected.fault })}
          >
            {selected.fault ? <CheckCircle2 size={17} /> : <AlertTriangle size={17} />}
            {selected.fault ? `CH${selected.channel} 고장 해제` : `CH${selected.channel} 고장 주입`}
          </button>
          <div className="sensor-toggle-grid" aria-label="조도 센서 결측 주입">
            {luxSensors.map(({ key, label, fallback }) => {
              const missing = environment[key] === null;
              return (
                <button
                  key={key}
                  className={missing ? 'sensor-toggle missing' : 'sensor-toggle'}
                  type="button"
                  onClick={() => toggleLuxSensor(key, fallback)}
                  title={`${label} 조도 센서 ${missing ? '복구' : '결측 주입'}`}
                >
                  <Unplug size={14} />
                  <span>{label}</span>
                  <strong>{missing ? '결측' : '정상'}</strong>
                </button>
              );
            })}
          </div>
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
}

function EnvSlider({ label, unit, min, max, value, onChange }: EnvSliderProps) {
  return (
    <label className="env-slider">
      <span>{label}</span>
      <input type="range" min={min} max={max} value={value} onChange={(event) => onChange(Number(event.target.value))} />
      <strong>{Math.round(value)}<small>{unit}</small></strong>
    </label>
  );
}
