import { useRef } from 'react';
import { Canvas, useFrame } from '@react-three/fiber';
import type { ThreeElements } from '@react-three/fiber';
import type { Group } from 'three';
import type { ChannelState } from '../types';

interface Props {
  channels: ChannelState[];
  selectedChannel: number;
  onSelectChannel: (channel: number) => void;
}

interface WindowPart {
  channel: number;
  position: [number, number, number];
  scale: [number, number, number];
  rotation?: [number, number, number];
}

const windowParts: WindowPart[] = [
  { channel: 0, position: [-0.55, 0.62, 1.49], scale: [0.72, 0.04, 0.42], rotation: [-0.18, 0, 0] },
  { channel: 1, position: [0.55, 0.62, 1.49], scale: [0.72, 0.04, 0.42], rotation: [-0.18, 0, 0] },
  { channel: 2, position: [-1.13, 0.53, 0.55], scale: [0.04, 0.44, 0.78] },
  { channel: 3, position: [1.13, 0.53, 0.55], scale: [0.04, 0.44, 0.78] },
  { channel: 4, position: [-1.13, 0.52, -0.55], scale: [0.04, 0.42, 0.72] },
  { channel: 5, position: [1.13, 0.52, -0.55], scale: [0.04, 0.42, 0.72] },
  { channel: 6, position: [0, 0.58, -1.48], scale: [1.18, 0.04, 0.38], rotation: [0.16, 0, 0] },
  { channel: 7, position: [0, 0.93, -0.1], scale: [1.05, 0.04, 1.02] }
];

function glassColor(mi: number, selected: boolean) {
  const frost = 1 - mi;
  const r = 0.45 + 0.45 * frost;
  const g = 0.76 + 0.13 * frost;
  const b = 0.83 + 0.12 * frost;
  return selected ? '#ffb34d' : `rgb(${Math.round(r * 255)}, ${Math.round(g * 255)}, ${Math.round(b * 255)})`;
}

function Box({ color, opacity = 1, ...props }: ThreeElements['mesh'] & { color: string; opacity?: number }) {
  return (
    <mesh {...props}>
      <boxGeometry args={[1, 1, 1]} />
      <meshStandardMaterial color={color} transparent={opacity < 1} opacity={opacity} roughness={0.48} metalness={0.08} />
    </mesh>
  );
}

function CarModel({ channels, selectedChannel, onSelectChannel }: Props) {
  const groupRef = useRef<Group>(null);
  useFrame(({ clock }) => {
    if (groupRef.current) {
      groupRef.current.rotation.y = -0.48 + Math.sin(clock.getElapsedTime() * 0.45) * 0.08;
    }
  });

  return (
    <group ref={groupRef} rotation={[0, -0.48, 0]}>
      <Box color="#d8e1df" position={[0, 0.18, 0]} scale={[2.35, 0.48, 3.25]} />
      <Box color="#eef2ef" position={[0, 0.55, -0.06]} scale={[1.85, 0.64, 1.82]} />
      <Box color="#303737" position={[-0.72, -0.18, 1.05]} scale={[0.38, 0.38, 0.38]} />
      <Box color="#303737" position={[0.72, -0.18, 1.05]} scale={[0.38, 0.38, 0.38]} />
      <Box color="#303737" position={[-0.72, -0.18, -1.05]} scale={[0.38, 0.38, 0.38]} />
      <Box color="#303737" position={[0.72, -0.18, -1.05]} scale={[0.38, 0.38, 0.38]} />
      <Box color="#ffca68" position={[-0.5, 0.18, 1.66]} scale={[0.38, 0.08, 0.04]} />
      <Box color="#ffca68" position={[0.5, 0.18, 1.66]} scale={[0.38, 0.08, 0.04]} />
      {windowParts.map((part) => {
        const channel = channels[part.channel];
        const selected = selectedChannel === part.channel;
        const opacity = selected ? 0.96 : 0.36 + (1 - channel.appliedMi) * 0.55;
        return (
          <mesh
            key={part.channel}
            position={part.position}
            scale={part.scale}
            rotation={part.rotation}
            onClick={(event) => {
              event.stopPropagation();
              onSelectChannel(part.channel);
            }}
          >
            <boxGeometry args={[1, 1, 1]} />
            <meshStandardMaterial
              color={glassColor(channel.appliedMi, selected)}
              transparent
              opacity={opacity}
              roughness={0.2}
              metalness={0.02}
            />
          </mesh>
        );
      })}
    </group>
  );
}

export function DigitalTwin(props: Props) {
  const selected = props.channels[props.selectedChannel];

  return (
    <section className="panel twin-panel">
      <div className="panel-heading">
        <div>
          <h2>3D Digital Twin</h2>
          <p>{selected.name}: {selected.opticalState}, applied MI {Math.round(selected.appliedMi * 100)}%.</p>
        </div>
      </div>
      <div className="canvas-shell">
        <Canvas camera={{ position: [3.2, 2.2, 4.2], fov: 43 }} dpr={[1, 1.6]}>
          <color attach="background" args={['#f4f0e7']} />
          <ambientLight intensity={0.7} />
          <directionalLight position={[4, 6, 5]} intensity={1.35} />
          <CarModel {...props} />
          <gridHelper args={[6, 6, '#8aa19a', '#d7d0c3']} position={[0, -0.43, 0]} />
        </Canvas>
      </div>
      <div className="twin-legend">
        {props.channels.map((channel) => (
          <button
            key={channel.channel}
            type="button"
            className={channel.channel === props.selectedChannel ? 'legend-dot active' : 'legend-dot'}
            onClick={() => props.onSelectChannel(channel.channel)}
            title={channel.name}
          >
            CH{channel.channel}
          </button>
        ))}
      </div>
    </section>
  );
}
