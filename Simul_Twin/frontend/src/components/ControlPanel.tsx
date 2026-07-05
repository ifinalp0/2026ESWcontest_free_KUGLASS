import { Download, RotateCcw, SlidersHorizontal } from 'lucide-react';
import type { ChannelState, ControlCommand, EnvironmentInput } from '../types';

interface Props {
  channels: ChannelState[];
  environment: EnvironmentInput;
  selectedChannel: number;
  onSelectChannel: (channel: number) => void;
  sendCommand: (command: ControlCommand) => void;
}

export function ControlPanel({ channels, environment, selectedChannel, onSelectChannel, sendCommand }: Props) {
  const selected = channels[selectedChannel];
  const frostStrength = Math.round((1 - selected.appliedMi) * 100);
  const manualRemaining = selected.manualUntil ? Math.max(0, Math.round(selected.manualUntil - Date.now() / 1000)) : null;

  const setManual = (value: number) => {
    const mi = Number((1 - value / 100).toFixed(3));
    sendCommand({ type: 'setManualChannel', channel: selected.channel, mi, ttlSeconds: 15 });
  };

  const setEnvironmentValue = (key: keyof EnvironmentInput, value: number) => {
    sendCommand({ type: 'setEnvironment', environment: { [key]: value } });
  };

  return (
    <section className="panel control-panel">
      <div className="panel-heading">
        <div>
          <h2>Control</h2>
          <p>{selected.name}</p>
        </div>
        <SlidersHorizontal size={20} />
      </div>

      <div className="channel-list">
        {channels.map((channel) => (
          <button
            key={channel.channel}
            type="button"
            className={channel.channel === selectedChannel ? 'channel-chip active' : 'channel-chip'}
            onClick={() => onSelectChannel(channel.channel)}
          >
            <span>CH{channel.channel}</span>
            <strong>{channel.opticalState}</strong>
          </button>
        ))}
      </div>

      <label className="range-label">
        <span>Clear</span>
        <strong>{frostStrength}% Frost</strong>
        <span>Frost</span>
      </label>
      <input
        type="range"
        min="0"
        max="100"
        value={frostStrength}
        onChange={(event) => setManual(Number(event.target.value))}
      />
      <button className="secondary-button" type="button" onClick={() => sendCommand({ type: 'returnAuto', channel: selected.channel })}>
        <RotateCcw size={17} />
        {manualRemaining === null ? 'Return Auto' : `Auto in ${manualRemaining}s`}
      </button>
      <button className="secondary-button" type="button" onClick={() => sendCommand({ type: 'saveReplay' })}>
        <Download size={17} />
        Save Replay Buffer
      </button>

      <div className="env-controls">
        <h3>Mock Environment</h3>
        <EnvSlider label="Weather Temp" min={15} max={45} value={environment.weatherTemp} onChange={(value) => setEnvironmentValue('weatherTemp', value)} />
        <EnvSlider label="Internal Temp" min={15} max={48} value={environment.internalTemp} onChange={(value) => setEnvironmentValue('internalTemp', value)} />
        <EnvSlider label="Front Lux" min={0} max={1300} value={environment.frontLux ?? 0} onChange={(value) => setEnvironmentValue('frontLux', value)} />
        <EnvSlider label="Top Lux" min={0} max={1300} value={environment.topLux ?? 0} onChange={(value) => setEnvironmentValue('topLux', value)} />
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
