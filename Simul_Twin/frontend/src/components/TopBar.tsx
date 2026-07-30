import type { KeyboardEvent } from 'react';
import { AlertTriangle, Download, Wifi, WifiOff } from 'lucide-react';
import type { SimulationState } from '../types';

interface Props {
  state: SimulationState;
  connected: boolean;
  onToggleTheme: () => void;
  onResetFault: () => void;
  onSaveReplay: () => void;
}

export function TopBar({ state, connected, onToggleTheme, onResetFault, onSaveReplay }: Props) {
  const hasFault = state.channels.some((channel) => channel.fault);

  const onTitleKeyDown = (event: KeyboardEvent<HTMLHeadingElement>) => {
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      onToggleTheme();
    }
  };

  return (
    <header className="topbar">
      <div className="brand">
        <div className="brand-copy">
          <div className="brand-title-row">
            <h1
              role="button"
              tabIndex={0}
              onDoubleClick={onToggleTheme}
              onKeyDown={onTitleKeyDown}
              title="더블클릭하여 브랜드 테마 전환"
              aria-label="KUGLASS SIMUL WIN. 더블클릭하여 색상 테마 전환"
            >
              KUGLASS <span>SIMUL TWIN</span>
            </h1>
            <span className="mock-badge">MOCK</span>
          </div>
        </div>
      </div>
      <div className="topbar-status">
        <div className={`status-block compact ${connected ? 'ok' : 'warn'}`} title={connected ? 'Mock 시뮬레이터 연결됨' : '오프라인 mock 데이터 사용 중'}>
          {connected ? <Wifi size={17} /> : <WifiOff size={17} />}
          <strong>{connected ? '연결됨' : '오프라인'}</strong>
        </div>
        <button className="topbar-action icon-only" type="button" onClick={onSaveReplay} title="리플레이 저장" aria-label="리플레이 저장">
          <Download size={17} />
        </button>
        {hasFault ? (
          <button className="topbar-action danger icon-only" type="button" onClick={onResetFault} title="고장 상태 초기화" aria-label="고장 상태 초기화">
            <AlertTriangle size={17} />
          </button>
        ) : null}
      </div>
    </header>
  );
}
