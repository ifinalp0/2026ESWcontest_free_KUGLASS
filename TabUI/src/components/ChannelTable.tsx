import { channelDisplayName, opticalStateLabels } from '../lib/labels';
import { normalizedMi } from '../lib/mi';
import type { ChannelState, DownstreamAdc } from '../types';

interface Props {
  channels: ChannelState[];
  adc: DownstreamAdc;
  selectedChannel: number;
  onSelectChannel: (channel: number) => void;
}

function senseValue(mv: number | null, raw: number | null, calibrated: boolean): string {
  if (calibrated && mv !== null) {
    return `${mv} mV`;
  }
  return raw === null ? '—' : `RAW ${raw}`;
}

export function ChannelTable({ channels, adc, selectedChannel, onSelectChannel }: Props) {
  return (
    <section className="panel channel-panel">
      <div className="panel-heading">
        <div className="panel-title-group">
          <span className="panel-index">04</span>
          <div>
            <h2>4채널 상태</h2>
          </div>
        </div>
        <span className={`panel-state ${adc.initialized ? 'ok' : 'manual'}`}>
          ADC {adc.initialized ? 'ONLINE' : 'WAIT'}
        </span>
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
            : channel.appliedKnown
              ? opticalStateLabels[channel.opticalState]
              : 'ESP32_B 상태 대기';
          const displayName = channelDisplayName(channel.name);
          const sense = adc.channels[channel.channel];
          const currentSense = sense
            ? senseValue(sense.currentMv, sense.currentRaw, adc.currentCalibrated)
            : '—';
          const temperatureSense = sense
            ? senseValue(sense.temperatureMv, sense.temperatureRaw, adc.temperatureCalibrated)
            : '—';

          return (
            <button
              key={channel.channel}
              type="button"
              className={`channel-row${channel.channel === selectedChannel ? ' selected' : ''}${channel.fault ? ' fault' : ''}`}
              aria-pressed={channel.channel === selectedChannel}
              aria-label={`CH${channel.channel}, ${displayName}, ${stateLabel}, ${transparency === null ? '적용 상태 대기' : `추정 투명도 ${transparency}%, 적용 MI ${channel.appliedMi.toFixed(3)}`}, 전류 sense ${currentSense}, 온도 sense ${temperatureSense}`}
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
              <span className="channel-sense" aria-label="Power Stage ADC sense 전압 또는 원시값">
                <small>I SENSE<strong>{currentSense}</strong></small>
                <small>T SENSE<strong>{temperatureSense}</strong></small>
              </span>
            </button>
          );
        })}
      </div>
    </section>
  );
}
