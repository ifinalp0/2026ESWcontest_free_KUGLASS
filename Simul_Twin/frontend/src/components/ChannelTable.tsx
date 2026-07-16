import { channelDisplayName, opticalStateLabels } from '../lib/labels';
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
        <div className="panel-title-group">
          <span className="panel-index">04</span>
          <div>
            <h2>8채널 상태</h2>
          </div>
        </div>
      </div>
      <div className="channel-table">
        {channels.map((channel) => {
          const appliedMi = Math.round(channel.appliedMi * 100);
          const stateLabel = channel.fault ? '구동기 고장' : opticalStateLabels[channel.opticalState];
          const displayName = channelDisplayName(channel.name);

          return (
            <button
              key={channel.channel}
              type="button"
              className={`channel-row${channel.channel === selectedChannel ? ' selected' : ''}${channel.fault ? ' fault' : ''}`}
              aria-pressed={channel.channel === selectedChannel}
              aria-label={`CH${channel.channel}, ${displayName}, ${stateLabel}, 적용 MI ${appliedMi}%`}
              title={`${displayName} · ${stateLabel}`}
              onClick={() => onSelectChannel(channel.channel)}
            >
              <span className="channel-row-head">
                <b>CH{channel.channel}</b>
                <i className={channel.fault ? 'fault' : channel.opticalState.toLowerCase()} aria-hidden="true" />
              </span>
              <span className="channel-row-name">{displayName}</span>
              <strong className="channel-mi-value">{appliedMi}<small>%</small></strong>
              <span className="channel-mi-track" aria-hidden="true">
                <i style={{ width: `${appliedMi}%` }} />
              </span>
            </button>
          );
        })}
      </div>
    </section>
  );
}
