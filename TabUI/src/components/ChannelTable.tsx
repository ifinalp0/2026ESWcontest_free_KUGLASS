import { channelDisplayName, opticalStateLabels } from '../lib/labels';
import { normalizedMi } from '../lib/mi';
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
            <h2>4채널 상태</h2>
          </div>
        </div>
      </div>
      <div className="channel-table">
        {channels.map((channel) => {
          const transparency = channel.appliedKnown
            ? Math.round(channel.estimatedTransmittance * 100)
            : null;
          const appliedRangePercent = channel.appliedKnown
            ? Math.round(normalizedMi(channel.appliedMi) * 100)
            : 0;
          const stateLabel = channel.fault
            ? '구동기 고장'
            : channel.commandedEnableKnown && !channel.commandedEnable
              ? channel.appliedKnown && channel.appliedMi === 0
                ? '전원 OFF'
                : '전원 OFF 명령'
              : channel.appliedKnown
                ? opticalStateLabels[channel.opticalState]
                : 'ESP32_B 상태 대기';
          const displayName = channelDisplayName(channel.name);

          return (
            <button
              key={channel.channel}
              type="button"
              className={`channel-row${channel.channel === selectedChannel ? ' selected' : ''}${channel.fault ? ' fault' : ''}`}
              aria-pressed={channel.channel === selectedChannel}
              aria-label={`CH${channel.channel}, ${displayName}, ${stateLabel}, ${transparency === null ? '적용 상태 대기' : `추정 투명도 ${transparency}%, 적용 MI ${channel.appliedMi.toFixed(3)}`}`}
              title={`${displayName} · ${stateLabel}${channel.appliedKnown ? ` · MI ${channel.appliedMi.toFixed(3)}` : ''}`}
              onClick={() => onSelectChannel(channel.channel)}
            >
              <span className="channel-row-head">
                <b>CH{channel.channel}</b>
                <i className={channel.fault ? 'fault' : channel.appliedKnown ? channel.opticalState.toLowerCase() : 'unknown'} aria-hidden="true" />
              </span>
              <span className="channel-row-name">{displayName}</span>
              <strong className="channel-mi-value">{transparency === null ? '—' : <>{transparency}<small>% 추정</small></>}</strong>
              <span className="channel-mi-track" aria-hidden="true">
                <i style={{ width: `${appliedRangePercent}%` }} />
              </span>
            </button>
          );
        })}
      </div>
    </section>
  );
}
