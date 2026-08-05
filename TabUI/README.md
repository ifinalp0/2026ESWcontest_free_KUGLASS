# KUGLASS TabUI

TabUI는 MacBook에서 직접 실행되는 KUGLASS 백엔드와 브라우저 HMI, ESP32_A USB gateway입니다.

```text
브라우저(MacBook 또는 신뢰된 격리 LAN의 태블릿)
  -> HTTP /api/command
MacBook TabUI backend
  -> micro-USB cable
  -> ESP32_A DevKit USB connector / USB Serial-JTAG CDC / JSON Lines
ESP32_A: 카메라·내부온도·정책·CH0~CH3 MI
  -> ESP32_B_Algo firmware: Logic Carrier를 통해 단일 채널 Power Stage PCB 4장 제어
```

TabUI는 화면 제공, 고수준 명령 검증·변환, ESP32_A telemetry 중계와 replay snapshot을 담당합니다. LIVE 자동 정책과 채널 목표 배열은 ESP32_A가 계산합니다. MacBook과 ESP32_A 사이에는 별도 GPIO UART 배선을 사용하지 않으며, ESP32_A DevKit의 USB 단자에 micro-USB 케이블을 직접 연결합니다.

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

ESP32_B status는 `v`, `seq`, `boot_id`, `reset_challenge`, `estop`,
`fault_code`, 정확히 네 채널과 ADC block을 함께 검증합니다. 잘못된 B status는
UI 상태나 장치 연결 freshness를 갱신하지 않습니다. B UART 통신 상태와 E-Stop/Fault
같은 operational 상태는 서로 분리해 표시합니다.

ADC의 current/temperature 항목은 Power Stage의 sense 입력입니다. calibration이
유효하면 mV, 그렇지 않으면 유효 mask가 설정된 raw 값만 표시합니다. 회로의 전류
환산계수와 NTC 곡선이 확정되기 전에는 이를 A 또는 °C 값으로 변환하지 않습니다.
`reset_fault` 결과는 B의 `control_result.seq`와 A가 중계한 최종 ACK sequence로
상관되며, 요청을 UART에 쓴 것만으로 성공으로 표시하지 않습니다.

## MacBook LIVE 실행

Node.js 22+와 Python 3.11+를 권장합니다.

1. ESP32_A DevKit의 USB 단자와 MacBook을 데이터 통신이 가능한 micro-USB 케이블로 연결합니다.
2. ESP32_A firmware는 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`로 빌드·플래시합니다.
3. TabUI 의존성을 설치하고 로컬 백엔드를 실행합니다.

```bash
python3 -m venv .venv
source .venv/bin/activate
npm ci
python3 -m pip install -r requirements.txt
npm run build
npm start
```

브라우저에서 `http://localhost:8080/demo`을 엽니다. `npm start`는 기본적으로 `usb` transport를 사용하며 macOS의 단일 `/dev/cu.usbmodem*` 장치를 자동으로 찾습니다. 둘 이상의 USB modem 장치가 연결되어 있으면 장치를 명시합니다.

```bash
python3 server.py --transport usb --usb-port /dev/cu.usbmodem1101
```

장치 이름은 다음 명령으로 확인할 수 있습니다.

```bash
python3 -m serial.tools.list_ports -v
```

`/dev/cu.SLAB_USBtoUART` 같은 USB-to-UART bridge 장치는 자동 선택하지 않습니다. 현재 제품 경로는 ESP32_A DevKit의 내장 USB Serial/JTAG 단자가 만드는 `/dev/cu.usbmodem*`입니다. pyserial은 macOS CDC/ACM 장치 파일을 여는 라이브러리로만 사용하며, MacBook과 ESP32_A 사이에 외부 UART TX/RX 배선을 뜻하지 않습니다.

## 프런트엔드 개발

두 터미널에서 다음을 실행합니다.

```bash
npm run dev:api
npm run dev
```

`http://localhost:5173/demo`을 엽니다. Vite가 `/api`와 `/health`를 MacBook의 8080 포트 백엔드로 proxy합니다.

## MOCK

ESP32_A 없이 화면만 개발할 때 명시적으로 MOCK을 선택합니다.

```bash
python3 server.py --transport mock
```

| 변수 | 기본값 | 설명 |
|---|---:|---|
| `TABUI_TRANSPORT` | `usb` | MacBook LIVE의 `usb` 또는 명시적 `mock` |
| `TABUI_USB_PORT` | `auto` | ESP32_A `/dev/cu.usbmodem*` 자동 탐색 또는 명시 경로 |
| `TABUI_HIL_ENABLED` | `0` | TabUI HIL 명령 허용 |
| `TABUI_PORT` | `8080` | 로컬 백엔드 HTTP 포트 |

## 안전 경계

- LIVE 장치가 연결되지 않았거나 telemetry가 stale이면 명령을 HTTP 503으로 거부합니다.
- TabUI와 ESP32_A가 모두 HIL을 허용한 격리 벤치에서만 환경·fault 진단 명령을 사용합니다.
- 채널 slider 연속 명령은 채널별 최신 값만 75 ms 간격으로 전달합니다.
- replay는 기본적으로 `TabUI/data/replays`의 상태 snapshot이며 하드웨어 명령을 만들지 않습니다.
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
```

`npm run check`는 TypeScript 검사와 Python unit test를 실행합니다.
