# KUGLASS TabUI

TabUI는 MacBook에서 직접 실행되는 로컬 백엔드, 브라우저 HMI와 ESP32_A USB
gateway입니다. 화면과 고수준 명령, 상태 중계, replay snapshot을 담당하며
LIVE 자동 정책이나 CH0~CH3 목표 배열은 계산하지 않습니다.

## 구성

```text
브라우저(MacBook 또는 신뢰된 격리 LAN의 태블릿)
  -> HTTP
MacBook TabUI frontend/backend
  -> 데이터 micro-USB
ESP32_A DevKit USB Serial/JTAG CDC
  -> 센서·정책·목표 MI / ESP32_B 상태
```

MacBook↔ESP32_A에는 별도 GPIO UART를 사용하지 않습니다. ESP32_A DevKit의 USB
단자에 데이터 micro-USB 케이블을 직접 연결하고 macOS의 `/dev/cu.usbmodem*`
장치를 엽니다.

## 제공 기능

- IONIQ 5 3D 모형과 CH0~CH3 선택
- 기본, 열부하, 차박, 주차, 카메라 역광 시나리오
- 채널별 30초 수동 MI와 AUTO 복귀
- 카메라 좌/우 ROI 포화, Edge Density와 내부온도 표시
- on-demand ESP32_A OV2640 영상
- A의 target/commanded MI와 B의 applied MI 분리
- SERVER, ESP32_A, ESP32_B의 online·stale·Fault 분리
- LIVE, MOCK, REPLAY의 명시적 구분

LIVE가 끊기면 마지막 실제 값을 stale로 유지하고 명령을 막습니다. MOCK으로
자동 전환하지 않습니다. 포화 감소율은 전·후 자극 값이 함께 있는 MOCK에서만
계산하며, LIVE에서는 baseline 없이 임의의 개선율을 만들지 않습니다.

## 빠른 시작

Node.js 22+와 Python 3.11+를 권장합니다.

```bash
python3 -m venv .venv
source .venv/bin/activate
npm ci
python3 -m pip install -r requirements.txt
npm run build
npm start
```

브라우저에서 `http://localhost:8080/demo`을 엽니다. `npm start`의 기본값은
`--transport usb --usb-port auto`이며 단일 `/dev/cu.usbmodem*`를 자동 선택합니다.

USB modem이 여러 개면 ESP32_A 장치를 지정합니다.

```bash
python3 -m serial.tools.list_ports -v
python3 server.py --transport usb --usb-port /dev/cu.usbmodem1101
```

`/dev/cu.SLAB_USBtoUART` 같은 USB-UART bridge는 자동 선택하지 않습니다.

## 프런트엔드 개발

두 터미널에서 backend와 Vite를 실행합니다.

```bash
npm run dev:api
```

```bash
npm run dev
```

`http://localhost:5173/demo`을 엽니다. Vite는 `/api`와 `/health`를 8080 포트의
로컬 backend로 proxy합니다.

## MOCK과 설정

ESP32_A 없이 화면만 개발할 때 MOCK을 명시합니다.

```bash
python3 server.py --transport mock
```

| 환경 변수 | 기본값 | 의미 |
| --- | --- | --- |
| `TABUI_TRANSPORT` | `usb` | LIVE `usb` 또는 명시적 `mock` |
| `TABUI_USB_PORT` | `auto` | ESP32_A `/dev/cu.usbmodem*` 자동 탐색 또는 명시 경로 |
| `TABUI_HIL_ENABLED` | `0` | 격리 HIL 명령 허용 |
| `TABUI_PORT` | `8080` | backend HTTP 포트 |

MOCK/REPLAY는 ESP32_A USB 장치를 열거나 하드웨어 출력 명령을 만들지 않습니다.

## 명령과 상태

TabUI↔A 명령, ACK, A state, B status와 Fault reset은
[`../docs/PROTOCOL.md`](../docs/PROTOCOL.md)를 따릅니다.

- backend는 `v=1`, `type=ui_command`, 단조 증가 `seq`를 가진 compact JSON
  Lines를 전송합니다.
- 채널은 0~3만 허용하며 LIVE 자동 모드에서 저수준 `ch` 배열을 만들지 않습니다.
- 환경·Fault 주입은 TabUI와 ESP32_A 양쪽이 허용한 격리 HIL에서만 사용합니다.
- `appliedMi`는 strict 검증을 통과한 B status에서만 갱신합니다.
- 잘못된 B status는 UI 상태나 link freshness를 갱신하지 않습니다.
- A/B 통신 상태와 E-Stop/Fault operational 상태를 분리합니다.
- ADC는 validity가 있는 raw 또는 mV만 표시하고 A/°C로 임의 변환하지 않습니다.
- Fault reset은 B `control_result`와 최종 A ACK가 일치할 때만 성공으로 표시합니다.

## 카메라 영상

`영상 보기`를 열면 ESP32_A가 VGA 640×480 RGB565를 품질 90 JPEG로 변환하여
최대 약 5 fps로 전송합니다. JSON Lines와 `KUGLCAM1` JPEG는 기존 USB
Serial/JTAG stream 하나에서 다중화됩니다. 별도 UART, 두 번째 USB 케이블이나
외부 viewer process는 사용하지 않습니다.

backend는 header, JPEG marker와 FNV-1a 검사를 통과한 최신 frame만 동일 출처
HTTP API로 제공합니다. 요청은 15초 lease이고 열린 viewer가 10초마다 갱신합니다.
종료·이탈·갱신 중단 시 off 명령 또는 lease 만료로 중지됩니다. 영상 실패는
카메라 scalar metric, A의 정책이나 20 Hz A→B heartbeat를 멈추지 않습니다.

MOCK에는 실제 frame이 없으므로 viewer 대기 상태만 확인할 수 있습니다.

## API

| endpoint | 역할 |
| --- | --- |
| `GET /health` | 서버와 ESP32_A link 상태 |
| `GET /api/state` | UI 전체 상태와 A/B link 상태 |
| `GET /api/camera/status` | 영상 lease, 최신 frame, FPS와 검사 오류 |
| `GET /api/camera/frame?after=<seq>` | 새 frame이 있을 때 JPEG 반환 |
| `POST /api/command` | 고수준 UI 명령 |
| `GET /demo` | 태블릿 HMI |

## 안전과 네트워크

- 장치가 없거나 LIVE telemetry가 stale이면 명령을 HTTP 503으로 거부합니다.
- 채널 slider는 채널별 최신 값만 75 ms 간격으로 전달합니다.
- replay는 `TabUI/data/replays`의 상태 snapshot이며 감사 로그나 제어 완료 증거가
  아닙니다.
- 서버는 인증 없는 plain HTTP 시연 구성입니다. localhost 또는 신뢰된 격리
  LAN에서만 직접 사용합니다.

## 검증

```bash
npm run check
npm run build
```

`npm run check`는 TypeScript typecheck와 Python unit test를 실행합니다. 전체
변경별 검증 범위는 [`../docs/VALIDATION.md`](../docs/VALIDATION.md)를 참고하세요.
