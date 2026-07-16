import { lazy, Suspense, useState } from 'react';
import { ChannelTable } from './components/ChannelTable';
import { ControlPanel } from './components/ControlPanel';
import { EvidencePanel } from './components/EvidencePanel';
import { ScenarioBar } from './components/ScenarioBar';
import { TopBar } from './components/TopBar';
import { useSimulationSocket } from './lib/socket';

const DigitalTwin = lazy(() => import('./components/DigitalTwin').then((module) => ({ default: module.DigitalTwin })));

export default function App() {
  const { state, connected, sendCommand } = useSimulationSocket();
  const [selectedChannel, setSelectedChannel] = useState(0);

  return (
    <main className="app-shell">
      <TopBar
        state={state}
        connected={connected}
        onResetFault={() => sendCommand({ type: 'resetFault' })}
        onSaveReplay={() => sendCommand({ type: 'saveReplay' })}
      />
      <section className="dashboard-grid">
        <Suspense fallback={<section className="panel twin-panel loading-panel">3D 디지털 트윈 로딩 중...</section>}>
          <DigitalTwin
            channels={state.channels}
            demoMode={state.demoMode}
            environment={state.environment}
            selectedChannel={selectedChannel}
            onSelectChannel={setSelectedChannel}
            sendCommand={sendCommand}
          />
        </Suspense>
        <EvidencePanel state={state} />
        <ControlPanel
          channels={state.channels}
          environment={state.environment}
          selectedChannel={selectedChannel}
          sendCommand={sendCommand}
        />
        <ChannelTable channels={state.channels} selectedChannel={selectedChannel} onSelectChannel={setSelectedChannel} />
      </section>
      <ScenarioBar active={state.demoMode} sendCommand={sendCommand} />
    </main>
  );
}
