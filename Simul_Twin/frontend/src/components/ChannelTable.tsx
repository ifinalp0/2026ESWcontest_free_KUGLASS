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
          <h2>Channel State</h2>
          <p>Target/applied MI and estimated optical state.</p>
        </div>
      </div>
      <div className="channel-table">
        {channels.map((channel) => (
          <button
            key={channel.channel}
            type="button"
            className={channel.channel === selectedChannel ? 'channel-row selected' : 'channel-row'}
            onClick={() => onSelectChannel(channel.channel)}
          >
            <span>CH{channel.channel}</span>
            <strong>{channel.opticalState}</strong>
            <span>{Math.round(channel.appliedMi * 100)} MI</span>
            <span>{Math.round(channel.estimatedTransmittance * 100)} T</span>
          </button>
        ))}
      </div>
    </section>
  );
}
