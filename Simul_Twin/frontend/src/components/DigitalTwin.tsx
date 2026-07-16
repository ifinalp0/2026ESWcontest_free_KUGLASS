import { Suspense, useEffect, useMemo, useRef, useState } from 'react';
import type { KeyboardEvent, PointerEvent } from 'react';
import { Canvas, useFrame, useLoader } from '@react-three/fiber';
import type { ThreeEvent } from '@react-three/fiber';
import { Flashlight, MousePointer2, RotateCw } from 'lucide-react';
import {
  BufferGeometry,
  Color,
  DoubleSide,
  Euler,
  Float32BufferAttribute,
  LineBasicMaterial,
  MathUtils,
  Mesh,
  MeshBasicMaterial,
  MeshPhysicalMaterial,
  MeshStandardMaterial,
  Object3D,
  Raycaster,
  RepeatWrapping,
  Shape,
  ShapeGeometry,
  SRGBColorSpace,
  TextureLoader,
  Vector3
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
  projectionDirection: [number, number, number];
  maxProjectionDistance?: number;
}

interface FittedWindowPart {
  channel: number;
  filmGeometry: BufferGeometry;
  outlineGeometry: BufferGeometry;
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
    points: [[-0.58, -0.16], [0.12, -0.16], [0.13, 0.4], [-0.44, 0.34], [-0.62, 0.08]],
    rotation: [-0.62, 0, 0],
    projectionDirection: [0, -0.58, -0.81]
  },
  {
    channel: 1,
    position: [0.13, 0.56, 1.14],
    points: [[-0.12, -0.16], [0.58, -0.16], [0.62, 0.08], [0.44, 0.34], [-0.13, 0.4]],
    rotation: [-0.62, 0, 0],
    projectionDirection: [0, -0.58, -0.81]
  },
  {
    channel: 2,
    position: [-1.012, 0.56, 0.44],
    points: [[-0.67, -0.08], [0.35, -0.08], [0.39, 0.1], [0.03, 0.25], [-0.63, 0.27], [-0.72, 0.12]],
    rotation: [0, -Math.PI / 2, 0.03],
    projectionDirection: [1, -0.3, 0]
  },
  {
    channel: 3,
    position: [1.012, 0.56, 0.44],
    points: [[-0.35, -0.08], [0.67, -0.08], [0.72, 0.12], [0.63, 0.27], [-0.03, 0.25], [-0.39, 0.1]],
    rotation: [0, Math.PI / 2, -0.03],
    projectionDirection: [-1, -0.3, 0]
  },
  {
    channel: 4,
    position: [-1.012, 0.55, -0.49],
    points: [[-0.57, -0.08], [0.21, -0.08], [0.25, 0.1], [0.08, 0.25], [-0.51, 0.27], [-0.62, 0.1]],
    rotation: [0, -Math.PI / 2, 0.02],
    projectionDirection: [1, -0.3, 0]
  },
  {
    channel: 5,
    position: [1.012, 0.55, -0.49],
    points: [[-0.21, -0.08], [0.57, -0.08], [0.62, 0.1], [0.51, 0.27], [-0.08, 0.25], [-0.25, 0.1]],
    rotation: [0, Math.PI / 2, -0.02],
    projectionDirection: [-1, -0.3, 0]
  },
  {
    channel: 6,
    position: [0, 0.58, -1.36],
    points: [[-0.52, -0.25], [0.52, -0.25], [0.5, -0.08], [0.28, -0.03], [-0.28, -0.03], [-0.5, -0.08]],
    rotation: [0.4, 0, 0],
    projectionDirection: [0, 0.4, -0.9]
  },
  {
    channel: 7,
    position: [0, 0.88, -0.12],
    points: [[-0.38, -0.48], [0.38, -0.48], [0.49, -0.35], [0.5, 0.57], [0.38, 0.72], [-0.38, 0.72], [-0.5, 0.57], [-0.49, -0.35]],
    rotation: [-Math.PI / 2, 0, 0],
    projectionDirection: [0, -1, 0]
  }
];

const topWindowClasses = ['ch0', 'ch1', 'ch2', 'ch3', 'ch4', 'ch5', 'ch6', 'ch7'];
const clearFilmColor = new Color('#62b9ca');
const frostedFilmColor = new Color('#eff5f1');
const passiveEdgeColor = new Color('#7892a8');
const activeEdgeColor = new Color('#007a4d');
const faultEdgeColor = new Color('#d34848');
const FILM_SURFACE_GAP = 0.006;
const FILM_SUBDIVISIONS = 3;

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

function isWheelObject(object: Object3D) {
  let current: Object3D | null = object;
  while (current) {
    if (current.name.startsWith('SM_Wheel_')) {
      return true;
    }
    current = current.parent;
  }
  return false;
}

function projectToVehicle(
  seed: Vector3,
  direction: Vector3,
  vehicle: Object3D,
  raycaster: Raycaster,
  maxDistance: number
) {
  raycaster.set(seed, direction);
  raycaster.far = maxDistance;
  const hit = raycaster.intersectObject(vehicle, true).find((candidate) => !isWheelObject(candidate.object));
  if (!hit) {
    return seed;
  }
  return hit.point.clone().addScaledVector(direction, -FILM_SURFACE_GAP);
}

function pointInPartSpace(point: Vector3, part: WindowPart, rotation: Euler) {
  return point.applyEuler(rotation).add(new Vector3(...part.position));
}

function subdivideTriangle(a: Vector3, b: Vector3, c: Vector3, segments: number) {
  const pointAt = (i: number, j: number) => {
    const bWeight = i / segments;
    const cWeight = j / segments;
    return a.clone()
      .multiplyScalar(1 - bWeight - cWeight)
      .addScaledVector(b, bWeight)
      .addScaledVector(c, cWeight);
  };
  const triangles: Vector3[] = [];
  for (let i = 0; i < segments; i += 1) {
    for (let j = 0; j < segments - i; j += 1) {
      triangles.push(pointAt(i, j), pointAt(i + 1, j), pointAt(i, j + 1));
      if (i + j < segments - 1) {
        triangles.push(pointAt(i + 1, j), pointAt(i + 1, j + 1), pointAt(i, j + 1));
      }
    }
  }
  return triangles;
}

function fitWindowPartToVehicle(part: WindowPart, vehicle: Object3D): FittedWindowPart {
  const source = new ShapeGeometry(windowShape(part.points)).toNonIndexed();
  const position = source.getAttribute('position');
  const rotation = new Euler(...(part.rotation ?? [0, 0, 0]));
  const direction = new Vector3(...part.projectionDirection).normalize();
  const raycaster = new Raycaster();
  const maxDistance = part.maxProjectionDistance ?? 0.85;
  const projectedPositions: number[] = [];

  for (let index = 0; index < position.count; index += 3) {
    const triangle = [0, 1, 2].map((offset) => (
      new Vector3().fromBufferAttribute(position, index + offset)
    ));
    subdivideTriangle(triangle[0], triangle[1], triangle[2], FILM_SUBDIVISIONS)
      .map((point) => pointInPartSpace(point, part, rotation))
      .map((point) => projectToVehicle(point, direction, vehicle, raycaster, maxDistance))
      .forEach((point) => projectedPositions.push(point.x, point.y, point.z));
  }

  const filmGeometry = new BufferGeometry();
  filmGeometry.setAttribute('position', new Float32BufferAttribute(projectedPositions, 3));
  filmGeometry.computeVertexNormals();
  filmGeometry.computeBoundingSphere();

  const outlinePositions = part.points.flatMap(([x, y]) => {
    const seed = pointInPartSpace(new Vector3(x, y, 0), part, rotation);
    const point = projectToVehicle(seed, direction, vehicle, raycaster, maxDistance);
    return [point.x, point.y, point.z];
  });
  const outlineGeometry = new BufferGeometry();
  outlineGeometry.setAttribute('position', new Float32BufferAttribute(outlinePositions, 3));

  source.dispose();
  return { channel: part.channel, filmGeometry, outlineGeometry };
}

function fitWindowPartsToVehicle(vehicle: Object3D) {
  const materialSides = new Map<Material, Material['side']>();
  vehicle.traverse((object) => {
    if (!(object instanceof Mesh)) {
      return;
    }
    const materials = Array.isArray(object.material) ? object.material : [object.material];
    materials.forEach((material) => {
      materialSides.set(material, material.side);
      material.side = DoubleSide;
    });
  });

  try {
    return windowParts.map((part) => fitWindowPartToVehicle(part, vehicle));
  } finally {
    materialSides.forEach((side, material) => {
      material.side = side;
    });
  }
}

function applyFilmVisuals(
  mi: number,
  selected: boolean,
  fault: boolean,
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
    filmMaterial.emissive.copy(fault ? faultEdgeColor : activeEdgeColor);
    filmMaterial.emissiveIntensity = fault ? 0.16 : selected ? 0.035 : 0;
  }

  if (hazeMaterial) {
    hazeMaterial.opacity = frost * 0.24 + (selected ? 0.035 : 0);
  }

  if (edgeMaterial) {
    edgeMaterial.color.copy(fault ? faultEdgeColor : selected ? activeEdgeColor : passiveEdgeColor);
    edgeMaterial.opacity = fault ? 0.96 : selected ? 0.86 : 0.34 + frost * 0.2;
  }
}

function PdlcFilmSurface({ part, channel, selected, onSelectChannel }: {
  part: FittedWindowPart;
  channel: ChannelState;
  selected: boolean;
  onSelectChannel: (channel: number) => void;
}) {
  const visualMiRef = useRef(channel.appliedMi);
  const filmMaterialRef = useRef<MeshPhysicalMaterial>(null);
  const hazeMaterialRef = useRef<MeshBasicMaterial>(null);
  const edgeMaterialRef = useRef<LineBasicMaterial>(null);
  const handleClick = (event: ThreeEvent<MouseEvent>) => {
    event.stopPropagation();
    onSelectChannel(part.channel);
  };

  useFrame((_, delta) => {
    visualMiRef.current = MathUtils.damp(visualMiRef.current, channel.appliedMi, 5.4, delta);
    applyFilmVisuals(
      visualMiRef.current,
      selected,
      channel.fault,
      filmMaterialRef.current,
      hazeMaterialRef.current,
      edgeMaterialRef.current
    );
  });

  return (
    <group>
        <mesh geometry={part.filmGeometry} onClick={handleClick} renderOrder={selected ? 12 : 8}>
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
        <mesh geometry={part.filmGeometry} onClick={handleClick} renderOrder={selected ? 13 : 9}>
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
        <lineLoop geometry={part.outlineGeometry} renderOrder={14}>
          <lineBasicMaterial ref={edgeMaterialRef} color="#83aaa5" transparent opacity={0.42} depthTest depthWrite={false} />
        </lineLoop>
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

function useIoniqModel() {
  const gltf = useLoader(GLTFLoader, IONIQ_MODEL_URL);
  const [bodyTexture, tireTexture, tireNormalTexture] = useLoader(TextureLoader, [
    IONIQ_BODY_TEXTURE_URL,
    IONIQ_TIRE_TEXTURE_URL,
    IONIQ_TIRE_NORMAL_URL
  ]);
  const textures = useMemo<IoniqMaterialTextures>(() => ({
    bodyMap: prepareColorTexture(bodyTexture),
    tireMap: prepareTireTexture(prepareColorTexture(tireTexture)),
    tireNormalMap: prepareTireTexture(tireNormalTexture)
  }), [bodyTexture, tireNormalTexture, tireTexture]);
  return useMemo(() => {
    const model = gltf.scene.clone(true);
    model.position.set(...IONIQ_MODEL_POSITION);
    model.scale.setScalar(IONIQ_MODEL_SCALE);
    model.traverse((object) => {
      object.castShadow = true;
      object.receiveShadow = true;
      if (object instanceof Mesh) {
        object.material = Array.isArray(object.material)
          ? object.material.map((material) => tuneIoniqMaterial(material, textures))
          : tuneIoniqMaterial(object.material, textures);
      }
    });
    model.updateMatrixWorld(true);
    return model;
  }, [gltf.scene, textures]);
}

function VehicleAssembly({ channels, onSelectChannel, selectedChannel }: Omit<CarModelProps, 'rotationY'>) {
  const model = useIoniqModel();
  const fittedParts = useMemo(
    () => fitWindowPartsToVehicle(model),
    [model]
  );

  useEffect(() => () => {
    fittedParts.forEach((part) => {
      part.filmGeometry.dispose();
      part.outlineGeometry.dispose();
    });
  }, [fittedParts]);

  return (
    <>
      <primitive object={model} />
      {fittedParts.map((part) => {
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
    </>
  );
}

function CarModel({ channels, onSelectChannel, selectedChannel, rotationY }: CarModelProps) {
  return (
    <group rotation={[0, rotationY, 0]}>
      <GroundShadow />
      <Suspense fallback={null}>
        <VehicleAssembly
          channels={channels}
          selectedChannel={selectedChannel}
          onSelectChannel={onSelectChannel}
        />
      </Suspense>
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
        <div className="panel-title-group">
          <span className="panel-index">01</span>
          <div>
            <span className="panel-eyebrow">VEHICLE RESPONSE</span>
            <h2>3D 디지털 트윈</h2>
            <p>{selected.name} · {opticalStateLabels[selected.opticalState]} · 적용 MI {Math.round(selected.appliedMi * 100)}%</p>
          </div>
        </div>
        <span className="panel-state"><RotateCw size={14} /> DRAG TO ROTATE</span>
      </div>
      <div
        className="canvas-shell"
        onPointerDown={onCanvasPointerDown}
        onPointerMove={onCanvasPointerMove}
        onPointerUp={onCanvasPointerUp}
        onPointerCancel={onCanvasPointerUp}
      >
        <div className="canvas-channel-readout">
          <span>SELECTED WINDOW</span>
          <strong>CH{String(selected.channel).padStart(2, '0')}</strong>
          <small>{selected.name}</small>
        </div>
        <div className="canvas-telemetry">
          <span><small>TARGET MI</small><strong>{Math.round(selected.targetMi * 100)}%</strong></span>
          <span><small>APPLIED MI</small><strong>{Math.round(selected.appliedMi * 100)}%</strong></span>
          <span><small>TRANSMITTANCE</small><strong>{Math.round(selected.estimatedTransmittance * 100)}%</strong></span>
        </div>
        <div className="canvas-interaction-hint"><MousePointer2 size={13} /> 유리를 선택하고 차체를 회전하세요</div>
        <Canvas camera={{ position: [3.35, 2.25, 4.1], fov: 42 }} dpr={[1, 1.6]}>
          <color attach="background" args={['#e8edf1']} />
          <hemisphereLight args={['#f8fbff', '#677784', 0.74]} />
          <directionalLight position={[4.5, 6, 4.5]} intensity={1.38} />
          <directionalLight position={[-3, 2.4, -4]} intensity={0.42} />
          <CarModel {...props} rotationY={rotationY} />
          <gridHelper args={[6, 6, '#7f98ad', '#cad4dc']} position={[0, -0.43, 0]} />
        </Canvas>
      </div>
      {props.demoMode === 'flashlight_360' ? <FlashlightTopView {...props} /> : null}
    </section>
  );
}
