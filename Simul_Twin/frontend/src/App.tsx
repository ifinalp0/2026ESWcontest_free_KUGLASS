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
      <TopBar state={state} connected={connected} onResetFault={() => sendCommand({ type: 'resetFault' })} />
      <section className="dashboard-grid">
        <Suspense fallback={<section className="panel twin-panel loading-panel">Loading 3D twin...</section>}>
          <DigitalTwin channels={state.channels} selectedChannel={selectedChannel} onSelectChannel={setSelectedChannel} />
        </Suspense>
        <EvidencePanel state={state} />
        <ControlPanel
          channels={state.channels}
          environment={state.environment}
          selectedChannel={selectedChannel}
          onSelectChannel={setSelectedChannel}
          sendCommand={sendCommand}
        />
        <ChannelTable channels={state.channels} selectedChannel={selectedChannel} onSelectChannel={setSelectedChannel} />
      </section>
      <ScenarioBar active={state.demoMode} sendCommand={sendCommand} />
    </main>
  );
}
