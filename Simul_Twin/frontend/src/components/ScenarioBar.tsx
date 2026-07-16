import { Car, CircleOff, Flame, Moon, ParkingCircle, Rotate3d } from 'lucide-react';
import type { ControlCommand, DemoMode } from '../types';

interface Props {
  active: DemoMode;
  sendCommand: (command: ControlCommand) => void;
}

const scenarios: Array<{ mode: DemoMode; label: string; meta: string; Icon: typeof Flame }> = [
  { mode: 'none', label: '기본 상태', meta: '투명 기준', Icon: CircleOff },
  { mode: 'hot_summer', label: '열부하 경감', meta: '1순위 시연', Icon: Flame },
  { mode: 'camping', label: '차박 프라이버시', meta: '전 채널 산란', Icon: Moon },
  { mode: 'parked', label: '주차 도난방지', meta: '불투명 유지', Icon: ParkingCircle },
  { mode: 'camera_saturation', label: '강한 역광', meta: 'AE 보조', Icon: Car },
  { mode: 'flashlight_360', label: '360° 손전등', meta: '방향성 조도', Icon: Rotate3d }
];

export function ScenarioBar({ active, sendCommand }: Props) {
  const activeScenario = scenarios.find((scenario) => scenario.mode === active) ?? scenarios[0];
  const ActiveIcon = activeScenario.Icon;

  return (
    <section className="scenario-bar" aria-label="시연 시나리오">
      <div className="scenario-context">
        <span className="scenario-icon">
          <ActiveIcon size={18} />
        </span>
        <div>
          <h2>시연 모드</h2>
          <p>{activeScenario.label} · {activeScenario.meta}</p>
        </div>
      </div>
      <label className="scenario-select">
        <span>모드</span>
        <select
          value={active}
          onChange={(event) => sendCommand({ type: 'setScenario', demoMode: event.target.value as DemoMode })}
        >
          {scenarios.map(({ mode, label }) => (
            <option key={mode} value={mode}>
              {label}
            </option>
          ))}
        </select>
      </label>
    </section>
  );
}
