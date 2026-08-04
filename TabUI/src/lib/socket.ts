import { useCallback, useEffect, useRef, useState } from 'react';
import type { ControlCommand, SimulationState } from '../types';
import { defaultState } from './defaultState';

const backendUrl = (import.meta.env.VITE_BACKEND_URL ?? '').replace(/\/$/, '');
const pollIntervalMs = 250;

export interface TabUIClient {
  state: SimulationState;
  connected: boolean;
  sendCommand: (command: ControlCommand) => void;
}

function isFourChannelSnapshot(value: unknown): value is SimulationState['channels'] {
  return Array.isArray(value)
    && value.length === 4
    && value.every((channel, index) => (
      typeof channel === 'object'
      && channel !== null
      && (channel as { channel?: unknown }).channel === index
    ));
}

function mergeSnapshot(current: SimulationState, snapshot: Partial<SimulationState>): SimulationState {
  const diagnostics = snapshot.downstreamDiagnostics;
  return {
    ...current,
    ...snapshot,
    environment: { ...current.environment, ...snapshot.environment },
    cameraMetrics: { ...current.cameraMetrics, ...snapshot.cameraMetrics },
    downstreamDiagnostics: {
      ...current.downstreamDiagnostics,
      ...diagnostics,
      adc: {
        ...current.downstreamDiagnostics.adc,
        ...diagnostics?.adc,
        channels: Array.isArray(diagnostics?.adc?.channels) && diagnostics.adc.channels.length === 4
          ? diagnostics.adc.channels
          : current.downstreamDiagnostics.adc.channels
      }
    },
    link: { ...current.link, ...snapshot.link },
    channels: isFourChannelSnapshot(snapshot.channels)
      ? snapshot.channels
      : current.channels
  };
}

export function useTabUIClient(): TabUIClient {
  const [state, setState] = useState<SimulationState>(defaultState);
  const [connected, setConnected] = useState(false);
  const [commandError, setCommandError] = useState<string | null>(null);
  const mountedRef = useRef(true);
  const requestInFlightRef = useRef(false);

  const refreshState = useCallback(async () => {
    if (requestInFlightRef.current) {
      return;
    }
    requestInFlightRef.current = true;
    try {
      const response = await fetch(`${backendUrl}/api/state`, { cache: 'no-store' });
      if (!response.ok) {
        throw new Error(`state HTTP ${response.status}`);
      }
      const snapshot = await response.json() as Partial<SimulationState>;
      if (mountedRef.current) {
        setState((current) => mergeSnapshot(current, snapshot));
        setConnected(true);
      }
    } catch {
      if (mountedRef.current) {
        setConnected(false);
      }
    } finally {
      requestInFlightRef.current = false;
    }
  }, []);

  useEffect(() => {
    mountedRef.current = true;
    void refreshState();
    const intervalId = window.setInterval(() => void refreshState(), pollIntervalMs);
    return () => {
      mountedRef.current = false;
      window.clearInterval(intervalId);
    };
  }, [refreshState]);

  const sendCommand = (command: ControlCommand) => {
    void fetch(`${backendUrl}/api/command`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(command)
    })
      .then(async (response) => {
        const result = await response.json().catch(() => ({})) as {
          state?: Partial<SimulationState>;
          error?: string;
        };
        if (!response.ok) {
          if (mountedRef.current) {
            setConnected(true);
            setCommandError(result.error ?? `command HTTP ${response.status}`);
          }
          return;
        }
        if (result.state && mountedRef.current) {
          setState((current) => mergeSnapshot(current, result.state ?? {}));
        }
        setCommandError(null);
        setConnected(true);
        void refreshState();
      })
      .catch((error: unknown) => {
        if (!mountedRef.current) {
          return;
        }
        setConnected(false);
        setCommandError(error instanceof Error ? error.message : String(error));
      });
  };

  const visibleState = commandError
    ? { ...state, link: { ...state.link, error: commandError } }
    : state;
  return { state: visibleState, connected, sendCommand };
}
