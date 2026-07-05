import { useEffect, useMemo, useState } from 'react';
import { io, Socket } from 'socket.io-client';
import type { CameraMetrics, ControlCommand, EnvironmentInput, FastState, SimulationState } from '../types';
import { defaultState } from './defaultState';

const backendUrl = import.meta.env.VITE_BACKEND_URL ?? 'http://localhost:5050';

export interface SimClient {
  state: SimulationState;
  connected: boolean;
  sendCommand: (command: ControlCommand) => void;
}

export function useSimulationSocket(): SimClient {
  const [state, setState] = useState<SimulationState>(defaultState);
  const [connected, setConnected] = useState(false);

  const socket = useMemo<Socket>(() => io(backendUrl, { transports: ['websocket', 'polling'] }), []);

  useEffect(() => {
    fetch(`${backendUrl}/api/state`)
      .then((response) => response.ok ? response.json() : Promise.reject(new Error(response.statusText)))
      .then((snapshot: SimulationState) => setState((current) => ({ ...current, ...snapshot })))
      .catch(() => undefined);

    socket.on('connect', () => setConnected(true));
    socket.on('disconnect', () => setConnected(false));
    socket.on('state:fast', (fast: FastState) => {
      setState((current) => ({
        ...current,
        schemaVersion: fast.schemaVersion,
        vehicleMode: fast.vehicleMode,
        demoMode: fast.demoMode,
        channels: fast.channels,
        timestamp: fast.timestamp
      }));
    });
    socket.on('camera:metrics', (cameraMetrics: CameraMetrics) => {
      setState((current) => ({ ...current, cameraMetrics }));
    });
    socket.on('sensor:update', (environment: EnvironmentInput) => {
      setState((current) => ({ ...current, environment }));
    });
    socket.on('sim:decision', (decisionReason: string) => {
      setState((current) => ({ ...current, decisionReason }));
    });

    return () => {
      socket.off('connect');
      socket.off('disconnect');
      socket.off('state:fast');
      socket.off('camera:metrics');
      socket.off('sensor:update');
      socket.off('sim:decision');
      socket.disconnect();
    };
  }, [socket]);

  const sendCommand = (command: ControlCommand) => {
    if (socket.connected) {
      socket.emit('command', command);
      return;
    }
    fetch(`${backendUrl}/api/command`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(command)
    }).catch(() => undefined);
  };

  return { state, connected, sendCommand };
}
