import { Suspense, useEffect, useMemo, useRef, useState } from 'react';
import type { KeyboardEvent, PointerEvent, RefObject } from 'react';
import { Canvas, useFrame, useLoader } from '@react-three/fiber';
import type { ThreeEvent } from '@react-three/fiber';
import { Flashlight, RotateCw } from 'lucide-react';
import {
  BufferGeometry,
  Color,
  DoubleSide,
  Float32BufferAttribute,
  LineBasicMaterial,
  MathUtils,
  Mesh,
  MeshBasicMaterial,
  MeshPhysicalMaterial,
  MeshStandardMaterial,
  RepeatWrapping,
  Shape,
  SRGBColorSpace,
  TextureLoader
} from 'three';
import type { Material, Texture } from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import type { ChannelState, ControlCommand, DemoMode, EnvironmentInput } from '../types';
import { luxBearing, opticalStateLabels } from '../lib/labels';

interface Props {
  channels: ChannelState[];
  demoMode: DemoMode;
  environment: EnvironmentInput;
  selectedChannel: number;
  onSelectChannel: (channel: number) => void;
  sendCommand: (command: ControlCommand) => void;
}

interface CarModelProps {
  channels: ChannelState[];
  onSelectChannel: (channel: number) => void;
  selectedChannel: number;
  rotationY: number;
}

interface WindowPart {
  channel: number;
  position: [number, number, number];
  points: Array<[number, number]>;
  rotation?: [number, number, number];
  bandWidth?: number;
  normalOffset?: number;
}

interface IoniqMaterialTextures {
  bodyMap: Texture;
  tireMap: Texture;
  tireNormalMap: Texture;
}

const IONIQ_MODEL_URL = '/models/hyundai_ioniq_5_lowpoly.glb';
const IONIQ_BODY_TEXTURE_URL = '/models/textures/M_Ioniq_baseColor.png';
const IONIQ_TIRE_TEXTURE_URL = '/models/textures/M_Tires_baseColor.png';
const IONIQ_TIRE_NORMAL_URL = '/models/textures/M_Tires_normal.png';
const IONIQ_MODEL_POSITION: [number, number, number] = [0, -0.58, -0.2];
const IONIQ_MODEL_SCALE = 0.9;

const windowParts: WindowPart[] = [
  {
    channel: 0,
    position: [-0.13, 0.56, 1.14],
    points: [[-0.35, -0.13], [-0.02, -0.13], [0, 0.15], [-0.31, 0.18], [-0.44, 0.04]],
    rotation: [-0.62, 0, 0],
    bandWidth: 0.28
  },
  {
    channel: 1,
    position: [0.13, 0.56, 1.14],
    points: [[0.02, -0.13], [0.35, -0.13], [0.44, 0.04], [0.31, 0.18], [0, 0.15]],
    rotation: [-0.62, 0, 0],
    bandWidth: 0.28
  },
  {
    channel: 2,
    position: [-1.012, 0.56, 0.44],
    points: [[-0.4, -0.15], [0.27, -0.15], [0.34, 0.09], [0.14, 0.19], [-0.36, 0.22], [-0.48, 0.05]],
    rotation: [0, -Math.PI / 2, 0.03],
    bandWidth: 0.46
  },
  {
    channel: 3,
    position: [1.012, 0.56, 0.44],
    points: [[-0.27, -0.15], [0.4, -0.15], [0.48, 0.05], [0.36, 0.22], [-0.14, 0.19], [-0.34, 0.09]],
    rotation: [0, Math.PI / 2, -0.03],
    bandWidth: 0.46
  },
  {
    channel: 4,
    position: [-1.012, 0.55, -0.49],
    points: [[-0.35, -0.14], [0.29, -0.14], [0.36, 0.05], [0.18, 0.18], [-0.31, 0.2], [-0.41, 0.05]],
    rotation: [0, -Math.PI / 2, 0.02],
    bandWidth: 0.42
  },
  {
    channel: 5,
    position: [1.012, 0.55, -0.49],
    points: [[-0.29, -0.14], [0.35, -0.14], [0.41, 0.05], [0.31, 0.2], [-0.18, 0.18], [-0.36, 0.05]],
    rotation: [0, Math.PI / 2, -0.02],
    bandWidth: 0.42
  },
  {
    channel: 6,
    position: [0, 0.58, -1.36],
    points: [[-0.52, -0.15], [0.52, -0.15], [0.42, 0.16], [0.2, 0.21], [-0.2, 0.21], [-0.42, 0.16]],
    rotation: [0.4, 0, 0],
    bandWidth: 0.66,
    normalOffset: -0.008
  },
  {
    channel: 7,
    position: [0, 0.88, -0.12],
    points: [[-0.36, -0.38], [0.36, -0.38], [0.46, -0.22], [0.42, 0.32], [0.28, 0.43], [-0.28, 0.43], [-0.42, 0.32], [-0.46, -0.22]],
    rotation: [-Math.PI / 2, 0, 0],
    bandWidth: 0.52
  }
];

const topWindowClasses = ['ch0', 'ch1', 'ch2', 'ch3', 'ch4', 'ch5', 'ch6', 'ch7'];
const clearFilmColor = new Color('#6dc9d0');
const frostedFilmColor = new Color('#edf7f4');
const passiveEdgeColor = new Color('#83aaa5');
const activeEdgeColor = new Color('#f2a12f');

function frostAmount(mi: number) {
  const frost = 1 - mi;
  return frost * frost * (3 - 2 * frost);
}

function windowShape(points: Array<[number, number]>) {
  const shape = new Shape();
  const [firstPoint, ...remainingPoints] = points;
  shape.moveTo(firstPoint[0], firstPoint[1]);
  remainingPoints.forEach(([x, y]) => shape.lineTo(x, y));
  shape.closePath();
  return shape;
}

function WindowFilmGeometry({ points }: { points: Array<[number, number]> }) {
  const shape = useMemo(() => windowShape(points), [points]);
  return <shapeGeometry args={[shape]} />;
}

function WindowFilmOutline({ points, materialRef }: {
  points: Array<[number, number]>;
  materialRef: RefObject<LineBasicMaterial | null>;
}) {
  const geometry = useMemo(() => {
    const outlineGeometry = new BufferGeometry();
    outlineGeometry.setAttribute(
      'position',
      new Float32BufferAttribute(points.flatMap(([x, y]) => [x, y, 0.004]), 3)
    );
    return outlineGeometry;
  }, [points]);

  return (
    <lineLoop geometry={geometry} renderOrder={14}>
      <lineBasicMaterial ref={materialRef} color="#83aaa5" transparent opacity={0.42} depthTest depthWrite={false} />
    </lineLoop>
  );
}

function applyFilmVisuals(
  mi: number,
  selected: boolean,
  filmMaterial: MeshPhysicalMaterial | null,
  hazeMaterial: MeshBasicMaterial | null,
  edgeMaterial: LineBasicMaterial | null
) {
  const frost = frostAmount(mi);

  if (filmMaterial) {
    filmMaterial.color.copy(clearFilmColor).lerp(frostedFilmColor, frost);
    filmMaterial.opacity = 0.08 + frost * 0.58 + (selected ? 0.08 : 0);
    filmMaterial.roughness = 0.18 + frost * 0.68;
    filmMaterial.transmission = MathUtils.lerp(0.58, 0.04, frost);
    filmMaterial.thickness = 0.028 + frost * 0.09;
    filmMaterial.clearcoat = MathUtils.lerp(0.78, 0.28, frost);
    filmMaterial.clearcoatRoughness = 0.14 + frost * 0.38;
    filmMaterial.emissive.copy(activeEdgeColor);
    filmMaterial.emissiveIntensity = selected ? 0.035 : 0;
  }

  if (hazeMaterial) {
    hazeMaterial.opacity = frost * 0.24 + (selected ? 0.035 : 0);
  }

  if (edgeMaterial) {
    edgeMaterial.color.copy(selected ? activeEdgeColor : passiveEdgeColor);
    edgeMaterial.opacity = selected ? 0.86 : 0.34 + frost * 0.2;
  }
}

function PdlcFilmSurface({ part, channel, selected, onSelectChannel }: {
  part: WindowPart;
  channel: ChannelState;
  selected: boolean;
  onSelectChannel: (channel: number) => void;
}) {
  const visualMiRef = useRef(channel.appliedMi);
  const filmMaterialRef = useRef<MeshPhysicalMaterial>(null);
  const hazeMaterialRef = useRef<MeshBasicMaterial>(null);
  const edgeMaterialRef = useRef<LineBasicMaterial>(null);
  const normalOffset = part.normalOffset ?? 0.008;
  const handleClick = (event: ThreeEvent<MouseEvent>) => {
    event.stopPropagation();
    onSelectChannel(part.channel);
  };

  useFrame((_, delta) => {
    visualMiRef.current = MathUtils.damp(visualMiRef.current, channel.appliedMi, 5.4, delta);
    applyFilmVisuals(
      visualMiRef.current,
      selected,
      filmMaterialRef.current,
      hazeMaterialRef.current,
      edgeMaterialRef.current
    );
  });

  return (
    <group position={part.position} rotation={part.rotation}>
      <group position={[0, 0, normalOffset]}>
        <mesh onClick={handleClick} renderOrder={selected ? 12 : 8}>
          <WindowFilmGeometry points={part.points} />
          <meshPhysicalMaterial
            ref={filmMaterialRef}
            color="#6dc9d0"
            transparent
            opacity={0.08}
            depthTest
            depthWrite={false}
            roughness={0.18}
            metalness={0}
            transmission={0.58}
            thickness={0.028}
            ior={1.42}
            clearcoat={0.78}
            clearcoatRoughness={0.14}
            side={DoubleSide}
            polygonOffset
            polygonOffsetFactor={-1}
          />
        </mesh>
        <mesh position={[0, 0, 0.002]} onClick={handleClick} renderOrder={selected ? 13 : 9}>
          <WindowFilmGeometry points={part.points} />
          <meshBasicMaterial
            ref={hazeMaterialRef}
            color="#f7fffd"
            transparent
            opacity={0}
            depthTest
            depthWrite={false}
            side={DoubleSide}
            polygonOffset
            polygonOffsetFactor={-2}
          />
        </mesh>
        <WindowFilmOutline points={part.points} materialRef={edgeMaterialRef} />
      </group>
    </group>
  );
}

function prepareColorTexture(texture: Texture) {
  texture.colorSpace = SRGBColorSpace;
  texture.flipY = false;
  texture.anisotropy = 8;
  texture.needsUpdate = true;
  return texture;
}

function prepareTireTexture(texture: Texture) {
  texture.wrapS = RepeatWrapping;
  texture.wrapT = RepeatWrapping;
  texture.repeat.set(1, 6);
  texture.flipY = false;
  texture.anisotropy = 8;
  texture.needsUpdate = true;
  return texture;
}

function tuneIoniqMaterial(material: Material, textures: IoniqMaterialTextures) {
  const tuned = material.clone();
  if (tuned instanceof MeshStandardMaterial) {
    if (tuned.name === 'M_Ioniq') {
      tuned.map = textures.bodyMap;
      tuned.color.set('#ffffff');
      tuned.metalness = 0.22;
      tuned.roughness = 0.46;
      tuned.emissive.set('#45504d');
      tuned.emissiveIntensity = 0.16;
    }
    if (tuned.name === 'M_Black_Dark') {
      tuned.color.set('#46514e');
      tuned.metalness = 0.06;
      tuned.roughness = 0.54;
      tuned.emissive.set('#26302e');
      tuned.emissiveIntensity = 0.16;
    }
    if (tuned.name === 'M_Gravity_Gold_Matte') {
      tuned.color.set('#b9b1a8');
      tuned.roughness = 0.48;
    }
    if (tuned.name === 'M_Tires') {
      tuned.map = textures.tireMap;
      tuned.normalMap = textures.tireNormalMap;
      tuned.normalScale.set(0.75, 0.75);
      tuned.color.set('#ffffff');
      tuned.metalness = 0;
      tuned.roughness = 0.68;
    }
  }
  tuned.needsUpdate = true;
  return tuned;
}

function GroundShadow() {
  return (
    <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, -0.44, 0]} scale={[1.38, 2.3, 1]}>
      <circleGeometry args={[1, 64]} />
      <meshBasicMaterial color="#39504d" transparent opacity={0.13} depthWrite={false} />
    </mesh>
  );
}

function IoniqModel() {
  const gltf = useLoader(GLTFLoader, IONIQ_MODEL_URL);
  const [bodyTexture, tireTexture, tireNormalTexture] = useLoader(TextureLoader, [
    IONIQ_BODY_TEXTURE_URL,
    IONIQ_TIRE_TEXTURE_URL,
    IONIQ_TIRE_NORMAL_URL
  ]);
  const model = useMemo(() => gltf.scene.clone(true), [gltf.scene]);
  const textures = useMemo<IoniqMaterialTextures>(() => ({
    bodyMap: prepareColorTexture(bodyTexture),
    tireMap: prepareTireTexture(prepareColorTexture(tireTexture)),
    tireNormalMap: prepareTireTexture(tireNormalTexture)
  }), [bodyTexture, tireNormalTexture, tireTexture]);

  useEffect(() => {
    model.traverse((object) => {
      object.castShadow = true;
      object.receiveShadow = true;
      if (object instanceof Mesh) {
        object.material = Array.isArray(object.material)
          ? object.material.map((material) => tuneIoniqMaterial(material, textures))
          : tuneIoniqMaterial(object.material, textures);
      }
    });
  }, [model, textures]);

  return <primitive object={model} position={IONIQ_MODEL_POSITION} scale={IONIQ_MODEL_SCALE} />;
}

function CarModel({ channels, onSelectChannel, selectedChannel, rotationY }: CarModelProps) {
  return (
    <group rotation={[0, rotationY, 0]}>
      <GroundShadow />
      <Suspense fallback={null}>
        <IoniqModel />
      </Suspense>
      {windowParts.map((part) => {
        const channel = channels[part.channel];
        return (
          <PdlcFilmSurface
            key={part.channel}
            part={part}
            channel={channel}
            selected={selectedChannel === part.channel}
            onSelectChannel={onSelectChannel}
          />
        );
      })}
    </group>
  );
}

function bearingLabel(angle: number) {
  const directions = ['전방', '우전방', '우측', '우후방', '후방', '좌후방', '좌측', '좌전방'];
  return directions[Math.round(angle / 45) % directions.length];
}

function FlashlightTopView({ channels, environment, sendCommand }: Props) {
  const bearing = luxBearing(environment);
  const [draftAngle, setDraftAngle] = useState(bearing);
  const draggingRef = useRef(false);

  useEffect(() => {
    setDraftAngle(bearing);
  }, [bearing]);

  const emitAngle = (angle: number) => {
    const normalized = (angle + 360) % 360;
    setDraftAngle(normalized);
    sendCommand({ type: 'setFlashlightAngle', angleDeg: Number(normalized.toFixed(1)) });
  };

  const updateFromPointer = (event: PointerEvent<HTMLDivElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    const dx = event.clientX - (rect.left + rect.width / 2);
    const dy = event.clientY - (rect.top + rect.height / 2);
    emitAngle((Math.atan2(dx, -dy) * 180 / Math.PI + 360) % 360);
  };

  const onPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    draggingRef.current = true;
    event.currentTarget.setPointerCapture(event.pointerId);
    updateFromPointer(event);
  };

  const onPointerMove = (event: PointerEvent<HTMLDivElement>) => {
    if (draggingRef.current) {
      updateFromPointer(event);
    }
  };

  const onPointerUp = (event: PointerEvent<HTMLDivElement>) => {
    draggingRef.current = false;
    event.currentTarget.releasePointerCapture(event.pointerId);
  };

  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.key === 'ArrowLeft') {
      event.preventDefault();
      emitAngle(draftAngle - 5);
    }
    if (event.key === 'ArrowRight') {
      event.preventDefault();
      emitAngle(draftAngle + 5);
    }
  };

  const rad = draftAngle * Math.PI / 180;
  const handleStyle = {
    left: `${50 + Math.sin(rad) * 42}%`,
    top: `${50 - Math.cos(rad) * 42}%`
  };

  const luxValues = [
    ['전방', environment.frontLux],
    ['우측', environment.rightLux],
    ['후방', environment.rearLux],
    ['좌측', environment.leftLux]
  ] as const;

  return (
    <div className="flashlight-demo">
      <div className="flashlight-demo-heading">
        <h3>360° 손전등 시연</h3>
        <span>{Math.round(draftAngle)}° · {bearingLabel(draftAngle)}</span>
      </div>
      <div className="top-view-grid">
        <div
          className="flashlight-map"
          role="slider"
          aria-label="손전등 방위각"
          aria-valuemin={0}
          aria-valuemax={359}
          aria-valuenow={Math.round(draftAngle)}
          tabIndex={0}
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={onPointerUp}
          onPointerCancel={onPointerUp}
          onKeyDown={onKeyDown}
        >
          <span className="bearing-mark front">전</span>
          <span className="bearing-mark right">우</span>
          <span className="bearing-mark rear">후</span>
          <span className="bearing-mark left">좌</span>
          <span className="orbit-track" />
          <span className="flashlight-handle" style={handleStyle}>
            <Flashlight size={16} />
          </span>
          <div className="top-car">
            <div className="top-car-body" />
            {channels.map((channel) => (
              <span
                key={channel.channel}
                className={`top-window ${topWindowClasses[channel.channel]}`}
                style={{ opacity: 0.48 + (1 - channel.appliedMi) * 0.48 }}
                title={`${channel.name} · ${opticalStateLabels[channel.opticalState]}`}
              >
                CH{channel.channel}
              </span>
            ))}
          </div>
        </div>
        <div className="flashlight-readout">
          {luxValues.map(([label, value]) => (
            <div key={label}>
              <span>{label}</span>
              <strong>{value === null ? '결측' : Math.round(value)}</strong>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

export function DigitalTwin(props: Props) {
  const selected = props.channels[props.selectedChannel];
  const [rotationY, setRotationY] = useState(-0.5);
  const dragRef = useRef<{ x: number; rotationY: number } | null>(null);

  const onCanvasPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    dragRef.current = { x: event.clientX, rotationY };
    event.currentTarget.setPointerCapture(event.pointerId);
  };

  const onCanvasPointerMove = (event: PointerEvent<HTMLDivElement>) => {
    if (!dragRef.current) {
      return;
    }
    const delta = event.clientX - dragRef.current.x;
    setRotationY(dragRef.current.rotationY + delta * 0.01);
  };

  const onCanvasPointerUp = (event: PointerEvent<HTMLDivElement>) => {
    dragRef.current = null;
    event.currentTarget.releasePointerCapture(event.pointerId);
  };

  return (
    <section className="panel twin-panel">
      <div className="panel-heading">
        <div>
          <h2>3D 디지털 트윈</h2>
          <p>{selected.name}: {opticalStateLabels[selected.opticalState]}, 적용 MI {Math.round(selected.appliedMi * 100)}%.</p>
        </div>
        <RotateCw size={20} />
      </div>
      <div
        className="canvas-shell"
        onPointerDown={onCanvasPointerDown}
        onPointerMove={onCanvasPointerMove}
        onPointerUp={onCanvasPointerUp}
        onPointerCancel={onCanvasPointerUp}
      >
        <Canvas camera={{ position: [3.35, 2.25, 4.1], fov: 42 }} dpr={[1, 1.6]}>
          <color attach="background" args={['#eef3f2']} />
          <hemisphereLight args={['#f8fbff', '#6e7e79', 0.72]} />
          <directionalLight position={[4.5, 6, 4.5]} intensity={1.38} />
          <directionalLight position={[-3, 2.4, -4]} intensity={0.42} />
          <CarModel {...props} rotationY={rotationY} />
          <gridHelper args={[6, 6, '#8ca7a0', '#d2ddda']} position={[0, -0.43, 0]} />
        </Canvas>
      </div>
      {props.demoMode === 'flashlight_360' ? <FlashlightTopView {...props} /> : null}
    </section>
  );
}
