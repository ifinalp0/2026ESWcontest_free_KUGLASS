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
        <span className="scenario-index">DEMO</span>
        <span className="scenario-icon" aria-hidden="true">
          <ActiveIcon size={18} />
        </span>
        <div>
          <span>현재 시나리오</span>
          <h2>{activeScenario.label}</h2>
          <p>{activeScenario.meta}</p>
        </div>
      </div>
      <div className="scenario-options" role="group" aria-label="시연 모드 선택">
        {scenarios.map(({ mode, label, meta, Icon }) => (
          <button
            key={mode}
            className={`scenario-option${mode === active ? ' active' : ''}`}
            type="button"
            aria-pressed={mode === active}
            onClick={() => sendCommand({ type: 'setScenario', demoMode: mode })}
          >
            <Icon size={16} />
            <span>
              <strong>{label}</strong>
              <small>{meta}</small>
            </span>
          </button>
        ))}
      </div>
    </section>
  );
}
