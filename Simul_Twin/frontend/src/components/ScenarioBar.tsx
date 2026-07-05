import { Car, CircleOff, Flame, Moon, ParkingCircle, Rotate3d } from 'lucide-react';
import type { ControlCommand, DemoMode } from '../types';

interface Props {
  active: DemoMode;
  sendCommand: (command: ControlCommand) => void;
}

const scenarios: Array<{ mode: DemoMode; label: string; Icon: typeof Flame }> = [
  { mode: 'hot_summer', label: 'Hot Summer', Icon: Flame },
  { mode: 'camping', label: 'Camping', Icon: Moon },
  { mode: 'parked', label: 'Parked', Icon: ParkingCircle },
  { mode: 'camera_saturation', label: 'Front Glare', Icon: Car },
  { mode: 'flashlight_360', label: '360 Flashlight', Icon: Rotate3d }
];

export function ScenarioBar({ active, sendCommand }: Props) {
  return (
    <nav className="scenario-bar" aria-label="Demo scenarios">
      {scenarios.map(({ mode, label, Icon }) => (
        <button
          key={mode}
          type="button"
          className={active === mode ? 'scenario-button active' : 'scenario-button'}
          onClick={() => sendCommand({ type: 'setScenario', demoMode: mode })}
        >
          <Icon size={18} />
          {label}
        </button>
      ))}
      <button type="button" className={active === 'none' ? 'scenario-button active' : 'scenario-button'} onClick={() => sendCommand({ type: 'setScenario', demoMode: 'none' })}>
        <CircleOff size={18} />
        Baseline
      </button>
    </nav>
  );
}
