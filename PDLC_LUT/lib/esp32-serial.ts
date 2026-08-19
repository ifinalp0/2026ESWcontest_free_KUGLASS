export type CameraFrame = {
  sequence: number;
  width: number;
  height: number;
  jpeg: Uint8Array;
  mimeType?: string;
};

type SerialPortLike = {
  readable: ReadableStream<Uint8Array> | null;
  writable: WritableStream<Uint8Array> | null;
  open(options: { baudRate: number; bufferSize?: number }): Promise<void>;
  close(): Promise<void>;
};

type SerialNavigator = Navigator & {
  serial?: {
    requestPort(): Promise<SerialPortLike>;
  };
};

const CAMERA_MAGIC = new TextEncoder().encode("KUGLCAM1");
const CAMERA_HEADER_BYTES = 28;
const CAMERA_FORMAT_JPEG = 2;
const CAMERA_MAX_PAYLOAD = 512 * 1024;

function appendBytes(left: Uint8Array, right: Uint8Array) {
  const merged = new Uint8Array(left.length + right.length);
  merged.set(left);
  merged.set(right, left.length);
  return merged;
}

function findSequence(buffer: Uint8Array, sequence: Uint8Array) {
  outer: for (let index = 0; index <= buffer.length - sequence.length; index += 1) {
    for (let offset = 0; offset < sequence.length; offset += 1) {
      if (buffer[index + offset] !== sequence[offset]) continue outer;
    }
    return index;
  }
  return -1;
}

function findByte(buffer: Uint8Array, value: number) {
  for (let index = 0; index < buffer.length; index += 1) {
    if (buffer[index] === value) return index;
  }
  return -1;
}

export function fnv1a(payload: Uint8Array) {
  let hash = 2166136261;
  for (const byte of payload) {
    hash ^= byte;
    hash = Math.imul(hash, 16777619) >>> 0;
  }
  return hash >>> 0;
}

export function webSerialSupported() {
  return typeof navigator !== "undefined" && Boolean((navigator as SerialNavigator).serial);
}

export class Esp32SerialClient {
  private port: SerialPortLike | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private buffer = new Uint8Array(0);
  private active = false;
  private disconnecting = false;

  constructor(
    private readonly onFrame: (frame: CameraFrame) => void,
    private readonly onLine: (line: string) => void,
    private readonly onError: (message: string) => void,
  ) {}

  async connect() {
    const serial = (navigator as SerialNavigator).serial;
    if (!serial) throw new Error("이 브라우저는 Web Serial API를 지원하지 않습니다.");
    this.port = await serial.requestPort();
    await this.port.open({ baudRate: 115200, bufferSize: CAMERA_MAX_PAYLOAD + CAMERA_HEADER_BYTES });
    if (!this.port.readable || !this.port.writable) {
      throw new Error("ESP32_A USB stream을 열 수 없습니다.");
    }
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.buffer = new Uint8Array(0);
    this.active = true;
    void this.readLoop();
  }

  async send(record: Record<string, unknown>) {
    if (!this.writer) throw new Error("ESP32_A가 연결되지 않았습니다.");
    const bytes = new TextEncoder().encode(`${JSON.stringify(record)}\n`);
    await this.writer.write(bytes);
  }

  async disconnect() {
    if (this.disconnecting) return;
    this.disconnecting = true;
    this.active = false;
    try {
      await this.reader?.cancel();
    } catch {
      // The stream can already be closed by a physical USB disconnect.
    }
    try {
      this.reader?.releaseLock();
    } catch {
      // The reader can release itself after a device removal.
    }
    try {
      this.writer?.releaseLock();
    } catch {
      // The writer can release itself after a device removal.
    }
    this.reader = null;
    this.writer = null;
    try {
      await this.port?.close();
    } catch {
      // Closing an already detached serial device is a no-op for this client.
    }
    this.port = null;
    this.buffer = new Uint8Array(0);
    this.disconnecting = false;
  }

  private async readLoop() {
    try {
      while (this.active && this.reader) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value?.length) {
          this.buffer = appendBytes(this.buffer, value);
          this.parseBuffer();
        }
      }
    } catch (error) {
      if (this.active) {
        this.onError(error instanceof Error ? error.message : "USB stream 읽기 오류");
      }
    } finally {
      if (this.active) {
        this.active = false;
        this.onError("ESP32_A USB 연결이 종료되었습니다.");
      }
    }
  }

  private parseBuffer() {
    while (this.buffer.length > 0) {
      const marker = findSequence(this.buffer, CAMERA_MAGIC);
      const newline = findByte(this.buffer, 0x0a);

      if (marker === 0) {
        if (this.buffer.length < CAMERA_HEADER_BYTES) return;
        const view = new DataView(
          this.buffer.buffer,
          this.buffer.byteOffset,
          CAMERA_HEADER_BYTES,
        );
        const sequence = view.getUint32(8, true);
        const width = view.getUint16(12, true);
        const height = view.getUint16(14, true);
        const format = view.getUint8(16);
        const payloadSize = view.getUint32(20, true);
        const expectedHash = view.getUint32(24, true);
        const validHeader = width >= 1 && width <= 1024
          && height >= 1 && height <= 1024
          && format === CAMERA_FORMAT_JPEG
          && payloadSize >= 4 && payloadSize <= CAMERA_MAX_PAYLOAD;
        if (!validHeader) {
          this.buffer = this.buffer.slice(1);
          continue;
        }
        const frameSize = CAMERA_HEADER_BYTES + payloadSize;
        if (this.buffer.length < frameSize) return;
        const payload = this.buffer.slice(CAMERA_HEADER_BYTES, frameSize);
        this.buffer = this.buffer.slice(frameSize);
        const validJpeg = payload[0] === 0xff && payload[1] === 0xd8
          && payload[payload.length - 2] === 0xff
          && payload[payload.length - 1] === 0xd9;
        if (!validJpeg || fnv1a(payload) !== expectedHash) {
          this.onError("손상된 KUGLCAM1 frame을 건너뛰었습니다.");
          continue;
        }
        this.onFrame({ sequence, width, height, jpeg: payload });
        continue;
      }

      if (marker > 0 && (newline < 0 || marker < newline)) {
        this.buffer = this.buffer.slice(marker);
        continue;
      }

      if (newline < 0) {
        if (this.buffer.length > 8192) this.buffer = new Uint8Array(0);
        return;
      }
      const line = new TextDecoder()
        .decode(this.buffer.slice(0, newline))
        .replace(/\r$/, "")
        .trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (line) this.onLine(line);
    }
  }
}
