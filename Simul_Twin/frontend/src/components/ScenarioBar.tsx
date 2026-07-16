import { Car, CircleOff, Flame, Moon, ParkingCircle, Rotate3d } from 'lucide-react';
import type { ControlCommand, DemoMode } from '../types';

interface Props {
  active: DemoMode;
  sendCommand: (command: ControlCommand) => void;
}

const scenarios: Array<{ mode: DemoMode; label: string; Icon: typeof Flame }> = [
  { mode: 'none', label: '기본', Icon: CircleOff },
  { mode: 'hot_summer', label: '열부하', Icon: Flame },
  { mode: 'camping', label: '차박', Icon: Moon },
  { mode: 'parked', label: '주차', Icon: ParkingCircle },
  { mode: 'camera_saturation', label: '역광', Icon: Car },
  { mode: 'flashlight_360', label: '360° 조도', Icon: Rotate3d }
];

export function ScenarioBar({ active, sendCommand }: Props) {
  return (
    <section className="scenario-bar" aria-label="시연 시나리오">
      <h2 className="scenario-title">시나리오</h2>
      <div className="scenario-options" role="group" aria-label="시연 모드 선택">
        {scenarios.map(({ mode, label, Icon }) => (
          <button
            key={mode}
            className={`scenario-option${mode === active ? ' active' : ''}`}
            type="button"
            aria-pressed={mode === active}
            onClick={() => sendCommand({ type: 'setScenario', demoMode: mode })}
          >
            <Icon size={16} />
            <strong>{label}</strong>
          </button>
        ))}
      </div>
    </section>
  );
}
