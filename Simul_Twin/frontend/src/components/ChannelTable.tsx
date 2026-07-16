import { opticalStateLabels } from '../lib/labels';
import type { ChannelState } from '../types';

interface Props {
  channels: ChannelState[];
  selectedChannel: number;
  onSelectChannel: (channel: number) => void;
}

export function ChannelTable({ channels, selectedChannel, onSelectChannel }: Props) {
  return (
    <section className="panel channel-panel">
      <div className="panel-heading">
        <div>
          <h2>채널 상태</h2>
          <p>목표 MI, 적용 MI, 추정 투과율.</p>
        </div>
      </div>
      <div className="channel-table">
        {channels.map((channel) => (
          <button
            key={channel.channel}
            type="button"
            className={`channel-row${channel.channel === selectedChannel ? ' selected' : ''}${channel.fault ? ' fault' : ''}`}
            onClick={() => onSelectChannel(channel.channel)}
          >
            <span>CH{channel.channel}</span>
            <strong>{channel.fault ? '구동기 고장' : opticalStateLabels[channel.opticalState]}</strong>
            <span>적용 MI {Math.round(channel.appliedMi * 100)}%</span>
            <span>투과율 {Math.round(channel.estimatedTransmittance * 100)}%</span>
          </button>
        ))}
      </div>
    </section>
  );
}
