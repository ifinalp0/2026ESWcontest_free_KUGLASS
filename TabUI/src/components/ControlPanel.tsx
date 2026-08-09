import { useEffect, useRef, useState } from 'react';
import { AlertTriangle, CheckCircle2, ChevronDown, RotateCcw, SlidersHorizontal, Thermometer } from 'lucide-react';
import { channelDisplayName } from '../lib/labels';
import { MAX_MI, normalizedMi } from '../lib/mi';
import { displayedInternalTemperature, randomExternalDemoTemperature } from '../lib/temperature';
import type { ChannelState, ControlCommand, DemoMode, EnvironmentInput } from '../types';

type CameraEnvironmentKey = 'frontLeftSaturation' | 'frontRightSaturation' | 'edgeDensity';

interface CameraInput {
  frontLeftSaturation: number;
  frontRightSaturation: number;
  edgeDensity: number;
}

interface Props {
  channels: ChannelState[];
  environment: EnvironmentInput;
  cameraInput: CameraInput;
  demoMode: DemoMode;
  selectedChannel: number;
  sendCommand: (command: ControlCommand) => void;
  controlsEnabled: boolean;
  diagnosticsEnabled: boolean;
}

export function ControlPanel({ channels, environment, cameraInput, demoMode, selectedChannel, sendCommand, controlsEnabled, diagnosticsEnabled }: Props) {
  const selected = channels[selectedChannel];
  const interactionActive = useRef(false);
  const pendingManual = useRef<{ channel: number; mi: number } | null>(null);
  const controlMi = Number.isFinite(selected.commandedMi) ? selected.commandedMi : selected.targetMi;
  const controllerFrostStrength = Math.round((1 - normalizedMi(controlMi)) * 100);
  const [manualDraft, setManualDraft] = useState({ channel: selected.channel, frostStrength: controllerFrostStrength });
  const frostStrength = manualDraft.channel === selected.channel ? manualDraft.frostStrength : controllerFrostStrength;
  const manualRemaining = selected.manualUntil ? Math.max(0, Math.round(selected.manualUntil - Date.now() / 1000)) : null;
  const manualActive = selected.manualPersistent || manualRemaining !== null;
  const externalTemperatureDemo = environment.internalTempOverride;
  const [externalTemperature, setExternalTemperature] = useState(
    Math.round(environment.internalTemp ?? 39)
  );
  const [cameraDraft, setCameraDraft] = useState<CameraInput>(cameraInput);
  const pendingCamera = useRef<Partial<Record<CameraEnvironmentKey, number>>>({});

  useEffect(() => {
    if (manualDraft.channel !== selected.channel) {
      interactionActive.current = false;
      pendingManual.current = null;
      setManualDraft({ channel: selected.channel, frostStrength: controllerFrostStrength });
      return;
    }

    if (!controlsEnabled) {
      interactionActive.current = false;
      pendingManual.current = null;
      if (manualDraft.frostStrength !== controllerFrostStrength) {
        setManualDraft({ channel: selected.channel, frostStrength: controllerFrostStrength });
      }
      return;
    }

    const pending = pendingManual.current;
    if (pending?.channel === selected.channel) {
      if (Math.abs(selected.commandedMi - pending.mi) > 0.005) {
        return;
      }
      pendingManual.current = null;
    }

    if (!interactionActive.current && manualDraft.frostStrength !== controllerFrostStrength) {
      setManualDraft({ channel: selected.channel, frostStrength: controllerFrostStrength });
    }
  }, [controllerFrostStrength, controlsEnabled, manualDraft, selected.channel, selected.commandedMi]);

  useEffect(() => {
    if (!externalTemperatureDemo && environment.internalTemp !== null) {
      setExternalTemperature(Math.round(environment.internalTemp));
    }
  }, [environment.internalTemp, externalTemperatureDemo]);

  useEffect(() => {
    setCameraDraft((current) => {
      const next = { ...current };
      const keys: CameraEnvironmentKey[] = [
        'frontLeftSaturation',
        'frontRightSaturation',
        'edgeDensity'
      ];

      for (const key of keys) {
        const pending = pendingCamera.current[key];
        if (diagnosticsEnabled && pending !== undefined) {
          if (Math.abs(cameraInput[key] - pending) <= 0.005) {
            delete pendingCamera.current[key];
            next[key] = cameraInput[key];
          }
          continue;
        }
        delete pendingCamera.current[key];
        next[key] = cameraInput[key];
      }
      return next;
    });
  }, [
    cameraInput.edgeDensity,
    cameraInput.frontLeftSaturation,
    cameraInput.frontRightSaturation,
    diagnosticsEnabled
  ]);

  const setManual = (value: number) => {
    if (!controlsEnabled || selected.fault) {
      return;
    }
    const frost = Math.max(0, Math.min(100, Math.round(value)));
    const mi = Number((MAX_MI * (1 - frost / 100)).toFixed(3));
    setManualDraft({ channel: selected.channel, frostStrength: frost });
    pendingManual.current = { channel: selected.channel, mi };
    sendCommand({ type: 'setManualChannel', channel: selected.channel, mi, ttlSeconds: 15 });
  };

  const finishManualInteraction = (value: number) => {
    interactionActive.current = false;
    setManual(value);
  };

  const returnToAuto = () => {
    interactionActive.current = false;
    pendingManual.current = null;
    sendCommand({ type: 'returnAuto', channel: selected.channel });
  };

  const setEnvironmentValue = (
    key: keyof Omit<EnvironmentInput, 'internalTempOverride'>,
    value: number | null
  ) => {
    if (!diagnosticsEnabled) {
      return;
    }
    sendCommand({ type: 'setEnvironment', environment: { [key]: value } });
  };

  const toggleExternalTemperatureDemo = () => {
    if (!diagnosticsEnabled) {
      return;
    }
    if (externalTemperatureDemo) {
      sendCommand({ type: 'setEnvironment', environment: { internalTemp: null } });
      return;
    }
    const randomTemperature = randomExternalDemoTemperature();
    setExternalTemperature(randomTemperature);
    sendCommand({ type: 'setEnvironment', environment: { internalTemp: randomTemperature } });
  };

  const updateExternalTemperature = (value: number) => {
    const temperature = Math.max(15, Math.min(50, Math.round(value)));
    setExternalTemperature(temperature);
    setEnvironmentValue('internalTemp', temperature);
  };

  const updateCameraInput = (key: CameraEnvironmentKey, value: number) => {
    if (!diagnosticsEnabled) {
      return;
    }
    const normalized = Math.max(0, Math.min(1, value / 100));
    pendingCamera.current[key] = normalized;
    setCameraDraft((current) => ({ ...current, [key]: normalized }));
    setEnvironmentValue(key, normalized);
  };

  const temperatureText = `${displayedInternalTemperature(environment).toFixed(1)} °C`;

  return (
    <section className="panel control-panel">
      <div className="panel-heading">
        <div className="panel-title-group">
          <span className="panel-index">03</span>
          <div>
            <h2>채널 제어</h2>
          </div>
        </div>
        <span className={`panel-state ${manualActive ? 'manual' : 'ok'}`}>
          <SlidersHorizontal size={14} />
          {selected.manualPersistent ? 'MANUAL' : manualRemaining === null ? 'AUTO' : `TTL ${manualRemaining}s`}
        </span>
      </div>

      <div className="selected-channel-summary">
        <div className="selected-channel-title">
          <span className="selected-channel-id">CH{selected.channel}</span>
          <span>
            <strong>{channelDisplayName(selected.name)}</strong>
          </span>
        </div>
        <div className="channel-source-grid" aria-label="채널 MI 출처별 상태">
          <span title="ESP32_A 정책 엔진이 계산한 목표 MI">
            <small>정책 TARGET</small>
            <strong>MI {selected.targetMi.toFixed(3)}</strong>
          </span>
          <span title="ESP32_A가 ESP32_B로 보낸 명령 MI">
            <small>A COMMANDED {selected.commandedEnableKnown ? selected.commandedEnable ? '· ENABLE' : '· OFF' : ''}</small>
            <strong>MI {selected.commandedMi.toFixed(3)}</strong>
          </span>
          <span title="ESP32_B가 보고한 실제 적용 MI">
            <small>B APPLIED</small>
            <strong>{selected.appliedKnown ? `MI ${selected.appliedMi.toFixed(3)}` : 'STATUS 대기'}</strong>
          </span>
        </div>
      </div>

      <div className="manual-control">
        <div className="control-section-title">
          <span>수동 조절</span>
          <small>{!controlsEnabled ? '장치 연결 대기' : selected.fault ? '고장 중 비활성' : '15초 자동 복귀'}</small>
        </div>
        <div className="range-label">
          <span>투명 100%</span>
          <strong>로컬 DRAFT · 산란 {frostStrength}%</strong>
          <span>강산란 0%</span>
        </div>
        <input
          type="range"
          min="0"
          max="100"
          value={frostStrength}
          disabled={!controlsEnabled || selected.fault}
          aria-disabled={!controlsEnabled || selected.fault}
          onChange={(event) => setManual(Number(event.target.value))}
          onPointerDown={() => {
            interactionActive.current = true;
          }}
          onPointerUp={(event) => finishManualInteraction(Number(event.currentTarget.value))}
          onPointerCancel={(event) => finishManualInteraction(Number(event.currentTarget.value))}
          onBlur={(event) => finishManualInteraction(Number(event.currentTarget.value))}
        />
        {manualActive ? (
          <button className="secondary-button" type="button" disabled={!controlsEnabled} onClick={returnToAuto}>
            <RotateCcw size={17} />
            {selected.manualPersistent ? '관리자 수동 제어 해제' : `수동 제어 해제 · ${manualRemaining}초`}
          </button>
        ) : null}
      </div>

      {demoMode === 'none' ? (
        <div className="env-controls temperature-only">
          <div className="control-section-title">
            <span>현재 온도</span>
            <small>DS18B20</small>
          </div>
          <TemperatureReadout value={temperatureText} source="실측 센서" />
        </div>
      ) : null}

      {demoMode === 'hot_summer' ? (
        <div className="env-controls thermal-inputs">
          <div className="control-section-title">
            <span>열부하 온도 입력</span>
            <small>{externalTemperatureDemo ? '외부 시연값 적용 중' : 'DS18B20 센서 판단'}</small>
          </div>
          <TemperatureReadout
            value={temperatureText}
            source={externalTemperatureDemo ? '외부 시연 입력' : '실측 센서'}
          />
          <button
            className={`temperature-demo-toggle${externalTemperatureDemo ? ' active' : ''}`}
            type="button"
            disabled={!diagnosticsEnabled}
            aria-pressed={externalTemperatureDemo}
            onClick={toggleExternalTemperatureDemo}
            title={diagnosticsEnabled ? '30~50 °C 중 임의 온도로 외부 온도 시연을 시작합니다' : 'MOCK 또는 양쪽에서 허용한 HIL에서만 사용할 수 있습니다'}
          >
            <Thermometer size={16} />
            {externalTemperatureDemo ? '온도 센서 판단으로 복귀' : '외부 온도 시연'}
          </button>
          {externalTemperatureDemo ? (
            <EnvSlider
              label="임의 시연 온도"
              unit="°C"
              min={15}
              max={50}
              value={externalTemperature}
              onChange={updateExternalTemperature}
            />
          ) : null}
        </div>
      ) : null}

      {demoMode === 'camera_saturation' ? (
        <div className="env-controls">
          <div className="control-section-title">
            <span>카메라 입력 · HIL</span>
            <small>{diagnosticsEnabled ? '시험 override 활성' : '실측 센서 보호'}</small>
          </div>
          <EnvSlider disabled={!diagnosticsEnabled} label="운전석측 ROI 포화" unit="%" min={0} max={100} value={cameraDraft.frontLeftSaturation * 100} onChange={(value) => updateCameraInput('frontLeftSaturation', value)} />
          <EnvSlider disabled={!diagnosticsEnabled} label="조수석측 ROI 포화" unit="%" min={0} max={100} value={cameraDraft.frontRightSaturation * 100} onChange={(value) => updateCameraInput('frontRightSaturation', value)} />
          <EnvSlider disabled={!diagnosticsEnabled} label="Edge Density" unit="%" min={0} max={100} value={cameraDraft.edgeDensity * 100} onChange={(value) => updateCameraInput('edgeDensity', value)} />
        </div>
      ) : null}

      <details className="validation-tools">
        <summary>
          <span><AlertTriangle size={16} /> 고장·결측 검증</span>
          <ChevronDown size={16} className="details-chevron" />
        </summary>
        <div className="validation-content">
          <button
            className={`fault-toggle ${selected.fault ? 'active' : ''}`}
            type="button"
            disabled={!diagnosticsEnabled}
            onClick={() => sendCommand({ type: 'setChannelFault', channel: selected.channel, fault: !selected.fault })}
          >
            {selected.fault ? <CheckCircle2 size={17} /> : <AlertTriangle size={17} />}
            {selected.fault ? `CH${selected.channel} 고장 해제` : `CH${selected.channel} 고장 주입`}
          </button>
        </div>
      </details>
    </section>
  );
}

function TemperatureReadout({ value, source }: { value: string; source: string }) {
  return (
    <div className="temperature-readout" aria-label={`현재 온도 ${value}, ${source}`}>
      <Thermometer size={20} />
      <span><small>{source}</small><strong>{value}</strong></span>
    </div>
  );
}

interface EnvSliderProps {
  label: string;
  unit: string;
  min: number;
  max: number;
  value: number;
  onChange: (value: number) => void;
  disabled?: boolean;
}

function EnvSlider({ label, unit, min, max, value, onChange, disabled = false }: EnvSliderProps) {
  return (
    <label className="env-slider">
      <span>{label}</span>
      <input disabled={disabled} type="range" min={min} max={max} value={value} onChange={(event) => onChange(Number(event.target.value))} />
      <strong>{Math.round(value)}<small>{unit}</small></strong>
    </label>
  );
}
