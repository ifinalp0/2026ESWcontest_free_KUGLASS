import { opticalStateLabels } from '../lib/labels';
import type { ChannelState } from '../types';

interface Props {
  channels: ChannelState[];
  selectedChannel: number;
  onSelectChannel: (channel: number) => void;
}

export function ChannelTable({ channels, selectedChannel, onSelectChannel }: Props) {
  const selected = channels[selectedChannel];

  return (
    <section className="panel channel-panel">
      <div className="panel-heading">
        <div className="panel-title-group">
          <span className="panel-index">04</span>
          <div>
            <span className="panel-eyebrow">8-CHANNEL OVERVIEW</span>
            <h2>PDLC 채널 뱅크</h2>
            <p>목표 MI · 적용 MI · 추정 투과율을 실시간 비교합니다.</p>
          </div>
        </div>
        <span className="channel-selection-readout">SELECTED <strong>CH{selected.channel}</strong></span>
      </div>
      <div className="channel-table">
        {channels.map((channel) => (
          <button
            key={channel.channel}
            type="button"
            className={`channel-row${channel.channel === selectedChannel ? ' selected' : ''}${channel.fault ? ' fault' : ''}`}
            onClick={() => onSelectChannel(channel.channel)}
          >
            <span className="channel-row-head">
              <b>CH{String(channel.channel).padStart(2, '0')}</b>
              <i className={channel.fault ? 'fault' : ''} />
            </span>
            <span className="channel-row-name" title={channel.name}>{channel.name}</span>
            <strong className="channel-row-state">{channel.fault ? '구동기 고장' : opticalStateLabels[channel.opticalState]}</strong>
            <span className="channel-mi-readout">
              <small>APPLIED MI</small>
              <b>{Math.round(channel.appliedMi * 100)}%</b>
            </span>
            <span className="channel-mi-track" aria-hidden="true">
              <i style={{ width: `${Math.round(channel.appliedMi * 100)}%` }} />
              <em style={{ left: `${Math.round(channel.targetMi * 100)}%` }} />
            </span>
            <span className="channel-row-foot">
              <span>목표 {Math.round(channel.targetMi * 100)}%</span>
              <span>투과 {Math.round(channel.estimatedTransmittance * 100)}%</span>
            </span>
          </button>
        ))}
      </div>
    </section>
  );
}
