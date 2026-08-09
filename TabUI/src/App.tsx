import { lazy, Suspense, useLayoutEffect, useState } from 'react';
import { ChannelTable } from './components/ChannelTable';
import { ControlPanel } from './components/ControlPanel';
import { EvidencePanel } from './components/EvidencePanel';
import { ScenarioBar } from './components/ScenarioBar';
import { TopBar } from './components/TopBar';
import { useTabUIClient } from './lib/socket';
import type { BrandTheme } from './types';

const DigitalTwin = lazy(() => import('./components/DigitalTwin').then((module) => ({ default: module.DigitalTwin })));
const THEME_STORAGE_KEY = 'kuglass-brand-theme';

function getInitialTheme(): BrandTheme {
  const savedTheme = window.localStorage.getItem(THEME_STORAGE_KEY);
  return savedTheme === 'konkuk' ? 'konkuk' : 'hyundai';
}

export default function App() {
  const {
    state,
    connected,
    sendCommand,
    refreshController,
    toggleBackend,
    controllerRefreshing,
    backendPowerChanging,
    controllerActionError,
    backendPowerError
  } = useTabUIClient();
  const [selectedChannel, setSelectedChannel] = useState(0);
  const [theme, setTheme] = useState<BrandTheme>(getInitialTheme);
  const controllerAvailable = connected && state.link.hardwareConnected;
  const diagnosticsEnabled = controllerAvailable && (
    state.link.transport === 'mock'
    || (state.link.hilEnabled && state.controllerDiagnostics.firmwareDiagnosticsEnabled === true)
  );

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
        onRefreshController={refreshController}
        onToggleBackend={toggleBackend}
        controllerRefreshing={controllerRefreshing}
        backendPowerChanging={backendPowerChanging}
        controllerActionError={controllerActionError}
        backendPowerError={backendPowerError}
      />
      <ScenarioBar active={state.demoMode} sendCommand={sendCommand} enabled={controllerAvailable} />
      <section className="dashboard-grid">
        <Suspense fallback={<section className="panel twin-panel loading-panel">3D 디지털 트윈 로딩 중...</section>}>
          <DigitalTwin
            channels={state.channels}
            selectedChannel={selectedChannel}
            onSelectChannel={setSelectedChannel}
          />
        </Suspense>
        <EvidencePanel state={state} connected={connected && state.link.hardwareConnected} />
        <ControlPanel
          key={state.demoMode}
          channels={state.channels}
          environment={state.environment}
          demoMode={state.demoMode}
          selectedChannel={selectedChannel}
          sendCommand={sendCommand}
          controlsEnabled={controllerAvailable}
          diagnosticsEnabled={diagnosticsEnabled}
        />
        <ChannelTable
          channels={state.channels}
          adc={state.downstreamDiagnostics.adc}
          selectedChannel={selectedChannel}
          onSelectChannel={setSelectedChannel}
        />
      </section>
    </main>
  );
}
