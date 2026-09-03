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
장치를 엽니다. ESP32_A↔ESP32_B는 이 USB 링크와 별개이며, 다음 외부 3선 UART
harness를 사용합니다.

```text
ESP32_A GPIO39 TX -> ESP32_B GPIO44 RX
ESP32_A GPIO40 RX <- ESP32_B GPIO43 TX
ESP32_A GND       --- ESP32_B GND
```

## 제공 기능

- IONIQ 5 3D 모형과 CH0 운전석 창문, CH1 조수석 창문·선루프,
  CH2 운전석 옆 창문, CH3 조수석 옆 창문 선택
- 기본, 열부하, 차박, 주차, 카메라 역광 시나리오
- 차박·주차에서 전 채널 `ENABLE OFF`, MI 0.0과 무전원 강산란 상태 표시
- 기본 화면의 읽기 전용 현재 온도와 열부하 화면의 MOCK/HIL 전용 외부 온도 시연
- 일반 운용 화면의 채널별 15초 수동 MI와 AUTO 복귀
- 운용 MI 0.0~0.60 검증과 MI 0.60을 완전 투명·추정 투과도 100%·산란 0%로
  정규화한 표시
- 카메라 운전석측/조수석측 ROI 포화, ROI 비중을 따라 이동하는 강광 위치,
  Edge Density와 내부온도 표시
- 실제 분석 경계를 겹쳐 보여주는 on-demand ESP32_A OV2640 영상
- A의 target/commanded MI와 B의 applied MI 분리
- SERVER, ESP32_A, ESP32_B의 online·stale·Fault 분리
- Topbar `CONTROLLER` 버튼의 ESP32_A USB 포트 재탐색·재연결
- Topbar `BACKEND` 전원 버튼의 ESP32_A gateway 런타임 시작·종료
- `/admin` 관리자 콘솔의 전체 A/B link·sequence·ACK·boot·sensor·ADC 진단 보기
- Power Stage `I_sense`·`T_sense` ADC 정보는 일반 운영 화면에서 숨기고 `/admin`에서만
  raw/mV와 TH1 데이터시트 기반 명목 °C를 표시
- `관리자 수동` 잠금 안에서 CH0~CH3 Enable·MI 변경 즉시 지속 제어와 전체 AUTO 복귀
- LIVE, MOCK, REPLAY의 명시적 구분

LIVE가 끊기면 마지막 실제 값을 stale로 유지하고 명령을 막습니다. MOCK으로
자동 전환하지 않습니다. 포화 감소율은 전·후 자극 값이 함께 있는 MOCK에서만
계산하며, LIVE에서는 baseline 없이 임의의 개선율을 만들지 않습니다.

## 빠른 시작

Node.js 22+와 Python 3.11+를 권장합니다.

```bash
npm ci
npm run setup:python
npm run build
npm start
```

브라우저에서 `http://localhost:8080/demo`을 엽니다. `npm start`의 기본값은
`--transport usb --usb-port auto`이며 단일 `/dev/cu.usbmodem*`를 자동 선택합니다.
`npm start`, `npm run dev:api`, `npm run check:python`은 활성 shell과 무관하게
항상 `.venv/bin/python`을 사용하므로 `pyserial`을 시스템 Python에 설치하지
않습니다.

운영 화면 우측 상단의 `관리자` 버튼 또는 `http://localhost:8080/admin`에서
관리자 콘솔을 엽니다. 관리자 수동은 TTL 없이 ESP32_A에서 유지되며, 잠금을 다시
닫으면 전체 `return_auto`를 요청합니다. 연결 단절 중에는 수동 상태가 계속되므로
연결 복구 후 명시적으로 AUTO 복귀를 요청해야 합니다. ESP32_B status는
적용 MI와 Fault를 회신하지만 적용 Enable을 직접 회신하지 않으므로 관리자 화면은
이를 추정값으로 표시하지 않습니다. 관리자 수동이 열린 동안 채널 슬라이더를
움직이거나 Enable 스위치를 전환하면 별도의 적용 버튼 없이 즉시 지속 수동 명령을
요청합니다.

Topbar의 `BACKEND` 전원 버튼은 HTTP 화면을 제공하는 최소 제어 셸은 유지한 채
ESP32_A USB gateway, 명령 queue와 카메라 lease를 함께 시작·종료합니다. 브라우저는
완전히 종료된 로컬 프로세스를 다시 실행할 수 없으므로 최초 한 번은
`npm run start:open`으로 격리 HIL용 TabUI를 실행해야 합니다. 이 명령은 `--hil`을
명시적으로 적용합니다. `STOPPED`에서 버튼을 다시 누르면
같은 실행 설정으로 gateway를 재시동하고 새 ESP32_A telemetry가 올 때까지 명령을
차단합니다. 이 동작은 ESP32_A의 AUTO 정책이나 A→B heartbeat를 중단하지 않습니다.

### 백엔드·화면·ESP32_A 통합 실행

ESP32_A DevKit의 USB 단자와 MacBook을 데이터 micro-USB 케이블로 연결한 뒤
다음 명령을 실행합니다.

```bash
npm run start:open
```

이 명령은 `.venv`의 Python으로 HIL 허용 LIVE 백엔드를 시작하고, macOS의 단일
`/dev/cu.usbmodem*` ESP32_A를 USB gateway에 연결한 다음 기본 브라우저에서
`http://127.0.0.1:8080/demo`을 엽니다. 최초 실행이거나 `.venv`가 없으면 먼저
`npm run setup:python`을 실행합니다. ESP32_A도
`KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS=1`로 빌드된 격리 HIL 펌웨어여야 환경 주입이
허용됩니다. 종료할 때는 명령을 실행한 터미널에서
`Ctrl+C`를 누릅니다.

USB modem이 여러 개면 ESP32_A 장치를 지정합니다.

```bash
.venv/bin/python -m serial.tools.list_ports -v
.venv/bin/python server.py --transport usb --usb-port /dev/cu.usbmodem1101
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
.venv/bin/python server.py --transport mock
```

| 환경 변수 | 기본값 | 의미 |
| --- | --- | --- |
| `TABUI_TRANSPORT` | `usb` | LIVE `usb` 또는 명시적 `mock` |
| `TABUI_USB_PORT` | `auto` | ESP32_A `/dev/cu.usbmodem*` 자동 탐색 또는 명시 경로 |
| `TABUI_HIL_ENABLED` | `0` | 격리 HIL 명령 허용 |
| `TABUI_PORT` | `8080` | backend HTTP 포트 |

MOCK/REPLAY는 ESP32_A USB 장치를 열거나 하드웨어 출력 명령을 만들지 않습니다.

기본 시나리오의 환경 영역은 DS18B20 현재 온도만 읽기 전용으로 표시합니다.
표시값은 ESP32_A telemetry의 유효한 `internal_temp_c`를 그대로 사용하며,
센서가 무효하거나 아직 수신되지 않았으면 임의 기본값 대신 결측으로 표시합니다.
열부하 시나리오는 기본적으로 같은 센서 값을 ESP32_A 정책 입력으로 사용하며,
`외부 온도 시연`은 MOCK 또는 TabUI와 ESP32_A 양쪽에서 진단 명령을 허용한 격리
HIL에서만 활성화됩니다. 버튼을 누르면 30~50 °C 범위에서 임의 온도를 자동으로
선택하고, 표시되는 조절 바로 시연값을 이어서 변경할 수 있습니다. 버튼을 해제하거나
다른 시나리오를 선택하면 합성 온도 override를 지우고 물리 DS18B20 입력으로
복귀합니다. 이 값은 제품에 외부 온도
센서가 추가되었다는 뜻이 아닙니다. 카메라 HIL 조절 바는 역광 시나리오에서만
표시합니다. 조절 중에는 로컬 입력값을 즉시 표시하고, MOCK에서는 합성 입력 상태,
LIVE HIL에서는 ESP32_A가 회신한 유효 카메라 metric으로 적용 결과를 동기화합니다.

## 명령과 상태

TabUI↔A 명령, ACK, A state, B status와 Fault reset은
[`../docs/PROTOCOL.md`](../docs/PROTOCOL.md)를 따릅니다.

- backend는 `v=1`, `type=ui_command`, 단조 증가 `seq`를 가진 compact JSON
  Lines를 전송합니다.
- 채널은 0~3만 허용하며 LIVE 자동 모드에서 저수준 `ch` 배열을 만들지 않습니다.
- 환경·Fault 주입은 TabUI와 ESP32_A 양쪽이 허용한 격리 HIL에서만 사용합니다.
- `appliedMi`는 strict 검증을 통과한 B status에서만 갱신합니다.
- 잘못된 B status는 UI 상태나 link freshness를 갱신하지 않습니다.
- B `protocol_error`는 즉시 표시하지만, 다음 A `state.downstream`이 현재 link
  error를 갱신합니다. 과거 오류 한 건을 현재 상태처럼 영구 latch하지 않습니다.
- A/B 통신 상태와 E-Stop/Fault operational 상태를 분리합니다.
- `I_sense`·`T_sense` ADC는 관리자 화면에서만 표시합니다. 전류는 raw/mV만
  제공하며, 온도는 ESP32_B의 유효 ADC→mV 값이 있을 때만 TH1의 3.3 V·R14 10 kΩ·
  `NTCG203NH103JT1` B25/85=3650 K 명목 모델로 `T °C (명목)`을 계산합니다.
  RAW count에 선형식을 직접 적용하지 않으며, 이 값은 보드별 실측 보정이나
  software protection 기준이 아닙니다. 상세 식과 허용오차는
  [`../hardware/Power_stage/README.md`](../hardware/Power_stage/README.md)를 따릅니다.
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
| `POST /api/controller/reconnect` | 현재 CDC handle을 닫고 ESP32_A USB 포트를 즉시 다시 탐색·연결 |
| `POST /api/backend/start` | HTTP 제어 셸 안에서 ESP32_A gateway 런타임 시작 |
| `POST /api/backend/stop` | HTTP 제어 셸은 유지하고 ESP32_A gateway 런타임 종료 |
| `POST /api/server/restart` | 응답 후 현재 인자를 유지해 TabUI 백엔드 프로세스 재시작 |
| `GET /demo` | 태블릿 HMI |
| `GET /admin` | 통신·센서·출력 진단과 잠금형 관리자 수동 제어 화면 |

## 안전과 네트워크

- 장치가 없거나 LIVE telemetry가 stale이면 명령을 HTTP 503으로 거부합니다.
- `CONTROLLER` 갱신 직후에는 새 ESP32_A telemetry가 올 때까지 LIVE 명령을
  차단합니다. MOCK에서는 실제 USB 장치가 없으므로 이 버튼을 비활성화합니다.
- `SERVER` 재시작 동안 브라우저에는 잠시 OFFLINE이 표시될 수 있습니다.
  재시작은 MacBook↔ESP32_A gateway만 끊으며 ESP32_A의 AUTO와 A→B heartbeat를
  재계산하거나 대신하지 않습니다.
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
