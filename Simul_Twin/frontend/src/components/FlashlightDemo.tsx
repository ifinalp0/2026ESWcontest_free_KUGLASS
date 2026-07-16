import { useEffect, useRef, useState } from 'react';
import type { CSSProperties, KeyboardEvent, PointerEvent } from 'react';
import { Flashlight } from 'lucide-react';
import { normalizeAngle, signedAngleDelta, unwrapAngle } from '../lib/flashlightMotion';
import { channelDisplayName, luxBearing, opticalStateLabels } from '../lib/labels';
import type { ChannelState, ControlCommand, EnvironmentInput } from '../types';

interface Props {
  channels: ChannelState[];
  environment: EnvironmentInput;
  sendCommand: (command: ControlCommand) => void;
}

const COMMAND_INTERVAL_MS = 50;
const POINTER_DEAD_ZONE_PX = 8;

function bearingLabel(angle: number) {
  const directions = ['전방', '우전방', '우측', '우후방', '후방', '좌후방', '좌측', '좌전방'];
  return directions[Math.round(normalizeAngle(angle) / 45) % directions.length];
}

export function FlashlightDemo({ channels, environment, sendCommand }: Props) {
  const bearing = luxBearing(environment);
  const [draftAngle, setDraftAngle] = useState(bearing);
  const draggingRef = useRef(false);
  const activePointerRef = useRef<number | null>(null);
  const mapCenterRef = useRef<{ x: number; y: number } | null>(null);
  const latestAngleRef = useRef(bearing);
  const visualFrameRef = useRef<number | null>(null);
  const commandTimerRef = useRef<number | null>(null);
  const queuedCommandAngleRef = useRef<number | null>(null);
  const pendingEchoAngleRef = useRef<number | null>(null);
  const lastCommandAtRef = useRef(Number.NEGATIVE_INFINITY);
  const sendCommandRef = useRef(sendCommand);

  useEffect(() => {
    sendCommandRef.current = sendCommand;
  }, [sendCommand]);

  useEffect(() => {
    if (draggingRef.current) {
      return;
    }

    const normalizedBearing = normalizeAngle(bearing);
    const pendingEcho = pendingEchoAngleRef.current;
    if (pendingEcho !== null) {
      if (Math.abs(signedAngleDelta(pendingEcho, normalizedBearing)) <= 0.2) {
        pendingEchoAngleRef.current = null;
      } else {
        return;
      }
    }

    const unwrappedBearing = unwrapAngle(latestAngleRef.current, normalizedBearing);
    latestAngleRef.current = unwrappedBearing;
    setDraftAngle(unwrappedBearing);
  }, [bearing]);

  useEffect(() => () => {
    if (visualFrameRef.current !== null) {
      window.cancelAnimationFrame(visualFrameRef.current);
      visualFrameRef.current = null;
    }
    if (commandTimerRef.current !== null) {
      window.clearTimeout(commandTimerRef.current);
      commandTimerRef.current = null;
    }
    queuedCommandAngleRef.current = null;
    activePointerRef.current = null;
    mapCenterRef.current = null;
    draggingRef.current = false;
  }, []);

  const scheduleVisualAngle = (angle: number) => {
    latestAngleRef.current = angle;
    if (visualFrameRef.current !== null) {
      return;
    }
    visualFrameRef.current = window.requestAnimationFrame(() => {
      visualFrameRef.current = null;
      setDraftAngle(latestAngleRef.current);
    });
  };

  const flushAngleCommand = () => {
    if (commandTimerRef.current !== null) {
      window.clearTimeout(commandTimerRef.current);
      commandTimerRef.current = null;
    }
    const angle = queuedCommandAngleRef.current;
    if (angle === null) {
      return;
    }
    queuedCommandAngleRef.current = null;
    lastCommandAtRef.current = window.performance.now();
    pendingEchoAngleRef.current = angle;
    sendCommandRef.current({ type: 'setFlashlightAngle', angleDeg: Number(angle.toFixed(1)) });
  };

  const queueAngleCommand = (angle: number, immediate = false) => {
    queuedCommandAngleRef.current = normalizeAngle(angle);
    const elapsed = window.performance.now() - lastCommandAtRef.current;
    if (immediate || elapsed >= COMMAND_INTERVAL_MS) {
      flushAngleCommand();
      return;
    }
    if (commandTimerRef.current === null) {
      commandTimerRef.current = window.setTimeout(flushAngleCommand, COMMAND_INTERVAL_MS - elapsed);
    }
  };

  const updateAngle = (angle: number, immediateCommand = false) => {
    const normalized = normalizeAngle(angle);
    const unwrapped = unwrapAngle(latestAngleRef.current, normalized);
    scheduleVisualAngle(unwrapped);
    queueAngleCommand(normalized, immediateCommand);
  };

  const angleFromPointer = (event: PointerEvent<HTMLDivElement>) => {
    const center = mapCenterRef.current;
    if (!center) {
      return null;
    }
    const dx = event.clientX - center.x;
    const dy = event.clientY - center.y;
    if (Math.hypot(dx, dy) < POINTER_DEAD_ZONE_PX) {
      return null;
    }
    return normalizeAngle(Math.atan2(dx, -dy) * 180 / Math.PI);
  };

  const updateFromPointer = (event: PointerEvent<HTMLDivElement>, immediateCommand = false) => {
    const angle = angleFromPointer(event);
    if (angle !== null) {
      updateAngle(angle, immediateCommand);
    }
  };

  const onPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    if (activePointerRef.current !== null || !event.isPrimary) {
      return;
    }
    event.preventDefault();
    const rect = event.currentTarget.getBoundingClientRect();
    mapCenterRef.current = { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
    draggingRef.current = true;
    activePointerRef.current = event.pointerId;
    event.currentTarget.setPointerCapture(event.pointerId);
    updateFromPointer(event, true);
  };

  const onPointerMove = (event: PointerEvent<HTMLDivElement>) => {
    if (draggingRef.current && activePointerRef.current === event.pointerId) {
      updateFromPointer(event);
    }
  };

  const finishPointerInteraction = (event: PointerEvent<HTMLDivElement>, useReleasePosition: boolean) => {
    if (activePointerRef.current !== event.pointerId) {
      return;
    }
    if (useReleasePosition) {
      const releaseAngle = angleFromPointer(event);
      if (releaseAngle !== null) {
        updateAngle(releaseAngle, true);
      } else {
        queueAngleCommand(normalizeAngle(latestAngleRef.current), true);
      }
    } else {
      queueAngleCommand(normalizeAngle(latestAngleRef.current), true);
    }
    draggingRef.current = false;
    activePointerRef.current = null;
    mapCenterRef.current = null;
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
  };

  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    const keyDelta = {
      ArrowLeft: -5,
      ArrowDown: -5,
      ArrowRight: 5,
      ArrowUp: 5,
      PageDown: -15,
      PageUp: 15
    }[event.key];
    if (keyDelta !== undefined) {
      event.preventDefault();
      updateAngle(latestAngleRef.current + keyDelta, true);
      return;
    }
    if (event.key === 'Home' || event.key === 'End') {
      event.preventDefault();
      updateAngle(event.key === 'Home' ? 0 : 359, true);
    }
  };

  const orbitStyle: CSSProperties = {
    transform: `rotate(${draftAngle}deg)`
  };
  const roundedAngle = Math.round(normalizeAngle(draftAngle)) % 360;
  const luxValues = [
    ['전방', environment.frontLux],
    ['우측', environment.rightLux],
    ['후방', environment.rearLux],
    ['좌측', environment.leftLux]
  ] as const;

  return (
    <section className="panel flashlight-panel">
      <div className="panel-heading">
        <div className="panel-title-group">
          <span className="panel-index">360</span>
          <div>
            <h2>360° 조도 시연</h2>
            <p>원형 궤도를 드래그해 광원 방향과 채널 반응을 확인하세요.</p>
          </div>
        </div>
        <span className="panel-state ok">
          <Flashlight size={14} />
          {roundedAngle}° · {bearingLabel(draftAngle)}
        </span>
      </div>
      <div className="flashlight-demo">
        <div className="top-view-grid">
          <div
            className="flashlight-map"
            role="slider"
            aria-label="손전등 방위각"
            aria-valuemin={0}
            aria-valuemax={359}
            aria-valuenow={roundedAngle}
            aria-valuetext={`${roundedAngle}도 ${bearingLabel(draftAngle)}`}
            tabIndex={0}
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={(event) => finishPointerInteraction(event, true)}
            onPointerCancel={(event) => finishPointerInteraction(event, false)}
            onLostPointerCapture={(event) => finishPointerInteraction(event, false)}
            onKeyDown={onKeyDown}
          >
            <span className="bearing-mark front">전</span>
            <span className="bearing-mark right">우</span>
            <span className="bearing-mark rear">후</span>
            <span className="bearing-mark left">좌</span>
            <span className="orbit-track" />
            <span className="flashlight-beam-orbit" style={orbitStyle}>
              <span className="flashlight-beam" />
            </span>
            <span className="flashlight-handle-orbit" style={orbitStyle}>
              <span className="flashlight-handle">
                <Flashlight size={17} />
              </span>
            </span>
            <div className="top-car" aria-label="차량 채널 상면도">
              <div className="top-car-body" />
              {channels.map((channel) => (
                <span
                  key={channel.channel}
                  className={`top-window ch${channel.channel}`}
                  style={{ opacity: 0.48 + (1 - channel.appliedMi) * 0.48 }}
                  title={`${channelDisplayName(channel.name)} · ${opticalStateLabels[channel.opticalState]}`}
                >
                  CH{channel.channel}
                </span>
              ))}
            </div>
          </div>
          <div className="flashlight-readout" aria-label="방향별 mock 조도">
            {luxValues.map(([label, value]) => (
              <div key={label}>
                <span>{label}</span>
                <strong>{value === null ? '결측' : Math.round(value)}{value === null ? '' : ' lx'}</strong>
              </div>
            ))}
          </div>
        </div>
      </div>
    </section>
  );
}
