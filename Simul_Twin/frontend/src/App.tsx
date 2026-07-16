import { lazy, Suspense, useLayoutEffect, useState } from 'react';
import { ChannelTable } from './components/ChannelTable';
import { ControlPanel } from './components/ControlPanel';
import { EvidencePanel } from './components/EvidencePanel';
import { FlashlightDemo } from './components/FlashlightDemo';
import { ScenarioBar } from './components/ScenarioBar';
import { TopBar } from './components/TopBar';
import { useSimulationSocket } from './lib/socket';
import type { BrandTheme } from './types';

const DigitalTwin = lazy(() => import('./components/DigitalTwin').then((module) => ({ default: module.DigitalTwin })));
const THEME_STORAGE_KEY = 'kuglass-brand-theme';

function getInitialTheme(): BrandTheme {
  const savedTheme = window.localStorage.getItem(THEME_STORAGE_KEY);
  return savedTheme === 'konkuk' ? 'konkuk' : 'hyundai';
}

export default function App() {
  const { state, connected, sendCommand } = useSimulationSocket();
  const [selectedChannel, setSelectedChannel] = useState(0);
  const [theme, setTheme] = useState<BrandTheme>(getInitialTheme);
  const isFlashlight360 = state.demoMode === 'flashlight_360';

  useLayoutEffect(() => {
    document.documentElement.dataset.theme = theme;
    window.localStorage.setItem(THEME_STORAGE_KEY, theme);
  }, [theme]);

  const toggleTheme = () => {
    setTheme((current) => current === 'hyundai' ? 'konkuk' : 'hyundai');
  };

  return (
    <main className="app-shell" data-theme={theme}>
      <TopBar
        state={state}
        connected={connected}
        onToggleTheme={toggleTheme}
        onResetFault={() => sendCommand({ type: 'resetFault' })}
        onSaveReplay={() => sendCommand({ type: 'saveReplay' })}
      />
      <ScenarioBar active={state.demoMode} sendCommand={sendCommand} />
      <section className={`dashboard-grid${isFlashlight360 ? ' flashlight-layout' : ''}`}>
        <Suspense fallback={<section className="panel twin-panel loading-panel">3D 디지털 트윈 로딩 중...</section>}>
          <DigitalTwin
            channels={state.channels}
            selectedChannel={isFlashlight360 ? null : selectedChannel}
            onSelectChannel={isFlashlight360 ? undefined : setSelectedChannel}
          />
        </Suspense>
        {isFlashlight360 ? (
          <FlashlightDemo
            channels={state.channels}
            environment={state.environment}
            sendCommand={sendCommand}
          />
        ) : (
          <>
            <EvidencePanel state={state} connected={connected} />
            <ControlPanel
              channels={state.channels}
              environment={state.environment}
              selectedChannel={selectedChannel}
              sendCommand={sendCommand}
            />
            <ChannelTable channels={state.channels} selectedChannel={selectedChannel} onSelectChannel={setSelectedChannel} />
          </>
        )}
      </section>
    </main>
  );
}
