import { useEffect, useMemo, useState } from 'react';
import { io, Socket } from 'socket.io-client';
import type { CameraMetrics, ControlCommand, EnvironmentInput, FastState, SimulationState } from '../types';
import { defaultState } from './defaultState';
import { applyOfflineMockCommand, stepOfflineMock } from './offlineMock';

const backendUrl = import.meta.env.VITE_BACKEND_URL ?? 'http://localhost:5050';

export interface SimClient {
  state: SimulationState;
  connected: boolean;
  sendCommand: (command: ControlCommand) => void;
}

export function useSimulationSocket(): SimClient {
  const [state, setState] = useState<SimulationState>(defaultState);
  const [connected, setConnected] = useState(false);

  const socket = useMemo<Socket>(() => io(backendUrl, {
    transports: ['websocket', 'polling'],
    autoConnect: false
  }), []);

  const refreshState = () => {
    return fetch(`${backendUrl}/api/state`)
      .then((response) => response.ok ? response.json() : Promise.reject(new Error(response.statusText)))
      .then((snapshot: SimulationState) => setState((current) => ({ ...current, ...snapshot })));
  };

  useEffect(() => {
    refreshState().catch(() => undefined);

    const onConnect = () => {
      setConnected(true);
      refreshState().catch(() => undefined);
    };
    const onDisconnect = () => setConnected(false);
    const onFastState = (fast: FastState) => {
      setState((current) => ({
        ...current,
        schemaVersion: fast.schemaVersion,
        vehicleMode: fast.vehicleMode,
        demoMode: fast.demoMode,
        channels: fast.channels,
        timestamp: fast.timestamp
      }));
    };
    const onCameraMetrics = (cameraMetrics: CameraMetrics) => {
      setState((current) => ({ ...current, cameraMetrics }));
    };
    const onSensorUpdate = (environment: EnvironmentInput) => {
      setState((current) => ({ ...current, environment }));
    };
    const onDecision = (decisionReason: string) => {
      setState((current) => ({ ...current, decisionReason }));
    };

    socket.on('connect', onConnect);
    socket.on('disconnect', onDisconnect);
    socket.on('state:fast', onFastState);
    socket.on('camera:metrics', onCameraMetrics);
    socket.on('sensor:update', onSensorUpdate);
    socket.on('sim:decision', onDecision);
    socket.connect();

    return () => {
      socket.off('connect', onConnect);
      socket.off('disconnect', onDisconnect);
      socket.off('state:fast', onFastState);
      socket.off('camera:metrics', onCameraMetrics);
      socket.off('sensor:update', onSensorUpdate);
      socket.off('sim:decision', onDecision);
      socket.disconnect();
      setConnected(false);
    };
  }, [socket]);

  useEffect(() => {
    if (connected) {
      return undefined;
    }
    const intervalId = window.setInterval(() => {
      setState((current) => stepOfflineMock(current));
    }, 100);
    return () => window.clearInterval(intervalId);
  }, [connected]);

  const sendCommand = (command: ControlCommand) => {
    if (socket.connected) {
      socket.emit('command', command);
      return;
    }
    setState((current) => applyOfflineMockCommand(current, command));
  };

  return { state, connected, sendCommand };
}
