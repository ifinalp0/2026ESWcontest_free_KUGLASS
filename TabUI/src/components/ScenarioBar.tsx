import { Car, CircleOff, Flame, Moon, ParkingCircle } from 'lucide-react';
import type { ControlCommand, DemoMode } from '../types';

interface Props {
  active: DemoMode;
  sendCommand: (command: ControlCommand) => void;
  enabled: boolean;
}

const scenarios: Array<{ mode: DemoMode; label: string; detail: string; Icon: typeof Flame }> = [
  { mode: 'none', label: '기본', detail: '자동 주행', Icon: CircleOff },
  { mode: 'hot_summer', label: '열부하', detail: '온도 기반', Icon: Flame },
  { mode: 'camping', label: '차박', detail: '전 채널 전원 OFF', Icon: Moon },
  { mode: 'parked', label: '주차', detail: '전 채널 전원 OFF', Icon: ParkingCircle },
  { mode: 'camera_saturation', label: '역광', detail: '카메라 기반', Icon: Car }
];

export function ScenarioBar({ active, sendCommand, enabled }: Props) {
  return (
    <section className="scenario-bar" aria-label="시연 시나리오">
      <h2 className="scenario-title">시나리오</h2>
      <div className="scenario-options" role="group" aria-label="시연 모드 선택">
        {scenarios.map(({ mode, label, detail, Icon }) => (
          <button
            key={mode}
            className={`scenario-option${mode === active ? ' active' : ''}`}
            type="button"
            disabled={!enabled}
            aria-pressed={mode === active}
            aria-label={`${label}: ${detail}`}
            onClick={() => sendCommand({ type: 'setScenario', demoMode: mode })}
          >
            <Icon size={16} />
            <span>
              <strong>{label}</strong>
              <small>{detail}</small>
            </span>
          </button>
        ))}
      </div>
    </section>
  );
}
