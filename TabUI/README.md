# KUGLASS TabUI

TabUI는 KUGLASS의 태블릿 HMI와 ESP32_A USB gateway입니다.

```text
태블릿 브라우저
  -> HTTP /api/command
TabUI backend
  -> USB CDC JSON Lines
ESP32_A: 카메라·내부온도·정책·CH0~CH3 MI
  -> ESP32_B_Algo firmware: Logic Carrier를 통한 4채널 Power Stage 제어
```

TabUI는 화면 제공, 고수준 명령 검증·변환, ESP32_A telemetry 중계와 replay snapshot을 담당합니다. LIVE 자동 정책과 채널 목표 배열은 ESP32_A가 계산합니다.

## 화면과 동작

- IONIQ 5 3D 모형과 CH0~CH3 선택
- 기본, 열부하, 차박, 주차, 카메라 역광 시나리오
- 채널별 30초 수동 MI와 AUTO 복귀
- 카메라 좌/우 ROI 포화도, Edge Density와 내부온도 표시
- ESP32_A `targetMi`/`commandedMi`와 ESP32_B `appliedMi` 분리
- SERVER, ESP32_A와 ESP32_B 연결·stale·Fault 상태 구분

`LIVE` 연결이 끊기면 마지막 실제 값을 유지하고 명령을 막습니다. 브라우저 상태를 MOCK으로 자동 전환하지 않습니다. `MOCK`은 명시적으로 mock transport를 선택했을 때만 사용합니다.

포화 감소율은 원본 카메라 자극과 적용 후 값을 함께 제공하는 MOCK에서만 계산합니다. LIVE는 별도의 baseline capture가 없으면 현재 ROI 포화도만 표시합니다.

## ESP32_A 명령

TabUI backend는 `v=1`, `type=ui_command`, 단조 증가 `seq`를 갖는 compact JSON Lines를 전송합니다.

```json
{"v":1,"type":"ui_command","seq":1,"command":"set_mode","mode":"camping"}
{"v":1,"type":"ui_command","seq":2,"command":"set_demo","demo_mode":"camping"}
{"v":1,"type":"ui_command","seq":3,"command":"manual_channel","channel_id":0,"target_mi":0.42,"ttl_ms":30000,"enable":true}
{"v":1,"type":"ui_command","seq":4,"command":"return_auto","channel_id":0}
```

채널은 0~3만 허용합니다. 자동 모드에서는 저수준 `ch` 배열을 생성하지 않습니다.

HIL 환경 입력은 내부온도와 카메라 좌/우 포화도·Edge Density만 지원합니다. LIVE에서는 환경과 latched fault를 UI 값으로 덮어쓰지 않습니다.

## 상태 정규화

지원 장치 record:

- `type=boot`: ESP32_A 역할과 진단 허용 여부
- `type=ack`: 명령 sequence 수락 또는 거부
- `type=state`: ESP32_A 정책, 카메라·온도와 CH0~CH3 목표/명령 상태
- `type=status`, `controller_id=B`: ESP32_B 실제 적용 MI와 Fault
- `type=protocol_error`: A 또는 B link 오류

`appliedMi`는 유효한 ESP32_B 상태에서만 갱신합니다. A의 목표값을 실제 적용값으로 대신 표시하지 않습니다.

## 로컬 개발

Node.js 22+와 Python 3.11+를 권장합니다.

```bash
npm ci
python3 -m pip install -r requirements.txt
```

터미널 1:

```bash
npm run dev:api
```

터미널 2:

```bash
npm run dev
```

`http://localhost:5173/demo`을 엽니다.

빌드 산출물을 Python 서버 하나로 제공할 수도 있습니다.

```bash
npm run build
python3 server.py --transport mock
```

## Docker

물리 출력이 없는 MOCK:

```bash
docker compose up --build
```

Linux 실기 호스트:

```bash
TABUI_SERIAL_DEVICE=/dev/serial/by-id/<esp32-a-device> \
docker compose -f docker-compose.yml -f docker-compose.hardware.yml up --build -d
```

| 변수 | 기본값 | 설명 |
|---|---:|---|
| `TABUI_TRANSPORT` | `mock` | `mock` 또는 `serial` |
| `TABUI_SERIAL_PORT` | `/dev/ttyUSB0` | 컨테이너/호스트의 ESP32_A USB CDC |
| `TABUI_SERIAL_BAUD` | `115200` | 직렬 속도 |
| `TABUI_HIL_ENABLED` | `0` | TabUI HIL 명령 허용 |
| `TABUI_HTTP_PORT` | `8080` | 호스트 HTTP 포트 |
| `TABUI_DIALOUT_GID` | `20` | Linux 직렬 장치 그룹 GID |

컨테이너에는 ESP32_A 장치 하나만 전달하고 `--privileged`를 사용하지 않습니다.

## 안전 경계

- LIVE 장치가 연결되지 않았거나 telemetry가 stale이면 명령을 HTTP 503으로 거부합니다.
- TabUI와 ESP32_A가 모두 HIL을 허용한 격리 벤치에서만 환경·fault 진단 명령을 사용합니다.
- 채널 slider 연속 명령은 채널별 최신 값만 75 ms 간격으로 전달합니다.
- replay는 `/data/replays`의 상태 snapshot이며 하드웨어 명령을 만들지 않습니다.
- 현재 서버는 plain HTTP와 인증 없는 시연용 구성이므로 신뢰된 격리 LAN에서만 직접 사용합니다.

## API

- `GET /health`: 서버와 ESP32_A link 상태
- `GET /api/state`: 전체 UI 상태와 A/B link 상태
- `POST /api/command`: 고수준 UI 명령
- `GET /demo`: 태블릿 UI

## 검증

```bash
npm run check
npm run build
docker compose config
```

`npm run check`는 TypeScript 검사와 Python unit test를 실행합니다.
