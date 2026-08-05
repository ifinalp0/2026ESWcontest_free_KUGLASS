# KUGLASS 개발 에이전트 지침

## 기준 문서

- 제품 구조와 개발 범위의 기준은 루트의 `개발 계획서.md`이다.
- **ESP32_B의 물리 GPIO, 신호 극성, 전원과 커넥터의 최우선 기준은 `hardware/Logic carrier.pdf`이다.** `hardware/README.md`는 이 회로도를 코드 개발용으로 해설한 문서다. 제품 계획과 회로가 충돌하면 하드웨어 연결은 회로도를 따르고 계획서·README·코드·테스트를 함께 정정한다.
- 현재 분석 기준 `Logic carrier.pdf` SHA-256은 `c6e7c129e7d5cd66f2e6cc850b9797e58e25d15bd59cb817267279b90fc0fa92`이다. PDF가 바뀌면 핀맵을 추측해 이어서 개발하지 말고 회로를 다시 분석한 뒤 해시와 관련 문서를 갱신한다.
- 작품은 1:10 차량 모형용 PDLC 4채널 시연 프로토타입이다. 실차 안전 장치나 자율주행 인지 성능 향상 장치로 표현하지 않는다.
- 입력 센서는 카메라 1대와 DS18B20 내부온도센서 1개만 사용한다.
- 채널 범위는 CH0~CH3이다. 코드, 프로토콜, UI, 테스트와 문서에서 이 범위를 동일하게 유지한다.

## 런타임 구조

```text
카메라 + DS18B20 내부온도센서
  -> ESP32_A: 입력 처리, 정책, LUT, MI servo, CH0~CH3 목표 MI
  -> UART 또는 RS-485 JSON Lines, 20 Hz full frame + 250 ms TTL
ESP32_B: 명령 검증, 4채널 SPWM, 로컬 Fault/timeout 차단
  -> Logic Carrier: EN_GLOBAL 하드웨어 게이팅, Fault/ADC, 전원·J7 인터페이스
  -> Power Stage PCB
  -> PDLC CH0~CH3
```

```text
브라우저(MacBook 또는 신뢰된 격리 LAN의 태블릿)
  -> MacBook TabUI의 동일 출처 HTTP
MacBook에서 직접 실행하는 TabUI frontend + backend
  -> micro-USB cable -> ESP32_A DevKit USB 단자
  -> USB Serial/JTAG CDC JSON Lines
ESP32_A
  -> 상태·ACK·ESP32_B 적용 MI/Fault
MacBook TabUI -> 브라우저
```

- 자동 판단과 권위 있는 목표 MI는 ESP32_A가 소유한다.
- ESP32_B는 목표를 다시 계산하지 않고 검증·구동·로컬 차단과 적용 상태 회신을 담당한다.
- ESP32_B는 Logic Carrier U3에 장착되어 `PWM_MAG/DIR/ENABLE`을 생성한다. Logic Carrier가 `EN_GLOBAL AND ENABLE_CHx`를 하드웨어 게이팅하고 J7을 통해 Power Stage와 `FAULT_N/ADC`를 주고받는다. 별도 제어 MCU는 없지만 **물리적으로 Logic Carrier를 생략하거나 Power Stage에 임의 직결하지 않는다.**
- TabUI는 UI 정적 파일, API, 명령 검증, 상태 집계, replay snapshot과 장치 gateway를 담당한다. LIVE 모드에서 자동 MI를 계산하지 않는다.

## 디렉터리 역할

- `개발 계획서.md`: 제품 구조, 개발 범위, 검증 목표의 기준 문서.
- `TabUI/`: MacBook에서 직접 실행하는 production 백엔드, 브라우저 HMI와 ESP32_A USB gateway.
- `ESP32_A_Algo/`: 카메라·내부온도 입력, 정책/MI master, A→B 통신 펌웨어.
- `ESP_Camera/`(선택): 독립 카메라 시험을 보존할 때만 두는 standalone 레퍼런스. `ESP32_A_Algo/`가 제품 카메라 서비스·핀 계약·드라이버 의존성을 자체 소유하므로 이 폴더는 저장소에 없어도 되며 A의 빌드·플래시에 사용하지 않는다. 폴더가 남아 있다면 별도의 명시적인 사용자 지시 없이 내부 기능 코드를 수정하지 않는다.
- `ESP32_B_Algo/`: ESP32_B에 빌드·플래시할 유일한 canonical 펌웨어. CH0~CH3 SPWM, Logic Carrier/Power Stage 제어, Fault와 A→B TTL 처리를 소유한다.
- `ESP32_A_TESTT/`: `type=set`, `mi[]` frame을 전송하는 독립 UART 시험 프로젝트. 현재 `ESP32_B_Algo/`의 `actuator_command` 계약과 비호환이므로 B 제품 검증에 사용하지 않는다.
- `ESP32_TEST/`: Power Stage/HV를 분리한 상태에서 ESP32_B–Logic Carrier GPIO 배선을 확인하는 독립 시험 프로젝트.
- `hardware/`: `Logic carrier.pdf`와 `Power_stage.pdf` 회로 기준 및 `hardware/README.md`의 개발용 핀맵·검증 규칙.
- `Simul_Twin/`: 독립 디지털 트윈 시뮬레이터. **사용자 지시에 따라 절대 수정하지 않는다.** production 하드웨어 명령 경로에도 연결하지 않는다.

## 명령과 상태 계약

- TabUI→A: `v=1`, `type=ui_command`, 단조 증가 `seq`를 사용한다.
- A→B: `v=1`, `type=actuator_command`, CH0~CH3의 full/unique set, `ttl_ms`, 단조 증가 `seq`를 사용한다.
- B→A: `v=1`, `type=status`, `controller_id=B`, CH0~CH3의 full/unique set과 실제 `mi`·`fault`, `estop`, `fault_code`를 사용한다. 모든 status의 nonzero u32 `boot_id`/`reset_challenge`는 필수이며 `diagnostic`, `adc`, `control_result`는 각 용도에 따라 추가한다.
- A는 부팅마다 nonzero u32 `source_session_id`를 생성한다. A→B fault reset은 top-level fields `v,type,seq,source_session_id,target_boot_id,reset_challenge,command`를 모두 가진 `type=control`, `command=reset_fault` frame으로만 보낸다.
- B는 reset 결과를 optional `control_result` object의 `command,seq,source_session_id,ok,error`로 보고한다. A는 pending request의 B `boot_id`, A `source_session_id`, request `seq`가 모두 일치하는 결과만 ACK하고, UART write 완료를 성공으로 간주하지 않으며 설정된 timeout에서 실패 처리한다.
- B status의 top-level `seq`는 B가 소유하는 독립 sequence이다. A는 같은 `boot_id` 안에서만 status sequence를 비교하고 새 B `boot_id`를 받으면 즉시 sequence 기준을 재설정한다.
- 채널 ID는 0~3, MI는 유한한 0.0~1.0, enable/fault는 JSON boolean이어야 한다. 누락·중복·범위 이탈 frame은 전체를 거부한다.
- backend→A 명령 lease와 A→B heartbeat TTL을 분리한다. TabUI 장애는 A의 AUTO 판단을 멈추지 않지만, B가 A heartbeat를 잃으면 출력을 safe-off한다.
- B는 활성 lease 동안 duplicate/stale sequence를 heartbeat로 인정하지 않는다. timeout과 safe-off 뒤 첫 유효 full frame에서만 sequence 기준을 다시 설정한다.
- `target_mi`, A가 보낸 `commanded_mi`, B가 보고한 `applied_mi`를 구분한다.
- MI는 1.0에 가까울수록 CLEAR, 0.0 또는 disable은 전원 차단·강산란 방향이다.

## 센서와 진단 경계

- production 입력은 카메라 ROI/AE metadata와 DS18B20 내부온도뿐이다.
- LIVE에서 태블릿 값으로 카메라, 온도 또는 latched fault를 덮어쓰지 않는다.
- `set_environment`와 `set_channel_fault`는 TabUI의 명시적 MOCK 또는 양쪽에서 허용된 HIL 빌드에서만 사용한다.
- 카메라 AE exposure/gain의 단위가 검증되지 않았으면 `ae_metadata_valid=false`로 보고하고 정책 계산에서 해당 항을 제외한다.
- 카메라나 온도 값이 stale이면 결측으로 표시하며 임의 기본값을 실제 측정값처럼 사용하지 않는다.
- ESP32_B의 ADC1 8개 raw/mV 수집, filter, calibration validity telemetry는 구현되었다. board-specific 전류 A·온도 °C 환산계수, clamp·단선·포화 보호 임계와 HIL이 확정되기 전에는 raw/mV를 물리량이나 보호 판단으로 승격하지 않는다.

## 안전 및 상태 표시

- 우선순위는 `E-Stop > latched Fault > Manual(TTL) > Demo/Auto`이다.
- ESP32_B는 부팅, 잘못된 frame, heartbeat timeout, E-Stop과 Fault에서 출력을 차단한다.
- Fault clear는 ESP32_B가 reset request의 target B boot, one-time challenge와 실제 안전 조건을 확인한 뒤에만 수행한다. 안전 조건이 충족되지 않은 matching request도 challenge를 소비하여 나중에 replay로 clear되지 않게 한다.
- LIVE 연결이 끊기면 마지막 실제 상태를 `STALE/OFFLINE`으로 표시하고 명령을 막는다. MOCK으로 자동 전환하지 않는다.
- MOCK/REPLAY는 ESP32_A USB 장치를 열거나 ESP32_B 출력 명령을 만들면 안 된다.
- UI는 runtime mode, A/B online·stale, last seen, control source, manual TTL, target/commanded/applied MI, 카메라·온도 품질, E-Stop/Fault를 구분한다.

## 하드웨어 경계

- ESP32_A와 TabUI의 production 물리 경로는 `MacBook -> 데이터 micro-USB cable -> ESP32_A DevKit USB connector -> ESP32-S3 USB Serial/JTAG(GPIO19/20)`이다. MacBook과 A 사이에 외부 GPIO UART TX/RX 배선을 추가하지 않는다. macOS에서는 `/dev/cu.usbmodem*` CDC/ACM 장치로 열며 TabUI `usb` transport가 단일 장치를 자동 탐색한다.
- ESP32_A 보드 기준 핀은 TabUI USB Serial/JTAG(GPIO19/20), B-link UART1 TX/RX(GPIO39/40), 외부전원 DS18B20(GPIO41)이다. TabUI USB 링크와 A↔B UART 링크를 혼동하지 않는다. 이는 아래의 ESP32_B 보드 핀과 별개다. 실제 DevKit header와 카메라 GPIO 충돌을 HIL 전에 다시 확인한다.
- DS18B20 data에는 외부 4.7 kΩ pull-up과 공통 3.3 V/GND가 필요하다. parasite power는 사용하지 않는다.
- ESP32_B/Logic Carrier의 확정 제어 핀은 아래 표와 같다. `ENABLE_CHx`는 MCU→74HC08 입력이고 J7의 `CHx_ENABLE`은 `EN_GLOBAL AND ENABLE_CHx` 결과다.

| 채널 | `PWM_MAG` | `DIR` | MCU `ENABLE` | `FAULT_N` | Current ADC | Temperature ADC |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CH0 | GPIO10 | GPIO11 | GPIO12 | GPIO13 | GPIO1 | GPIO2 |
| CH1 | GPIO14 | GPIO15 | GPIO16 | GPIO17 | GPIO4 | GPIO5 |
| CH2 | GPIO18 | GPIO21 | GPIO38 | GPIO39 | GPIO6 | GPIO7 |
| CH3 | GPIO40 | GPIO41 | GPIO42 | GPIO47 | GPIO8 | GPIO3 |

- 공통 `EN_GLOBAL`은 GPIO19 **input-only**다. J5 NC E-Stop은 정상 시 +3.3 V를 연결하고 R18 10 kΩ이 open/E-Stop 상태를 LOW로 만든다. GPIO19를 output, USB 또는 강제 pull-up으로 구성하지 않는다. ESP32-S3의 GPIO19 power-up high glitch는 firmware 초기화 전에도 생길 수 있으므로 reset 중 U4/J7 enable 파형을 계측하고 필요하면 하드웨어 보강을 요구한다.
- `FAULT_N_CH0~3`은 active-low이며 R19~R22 10 kΩ 외부 pull-up이 있다. LOW를 fault로 처리한다. 단선/미연결도 pull-up 때문에 HIGH로 보일 수 있으므로 커넥터 presence를 별도 검증한다.
- J7은 2x32이다. 모든 짝수 핀은 GND이고 각 채널의 홀수 핀 8개는 순서대로 `PWM_MAG, DIR, CHx_ENABLE, FAULT_N, ADC_I_RAW, ADC_TEMP_RAW, +3.3V, +12V`다. CH0은 1~15, CH1은 17~31, CH2는 33~47, CH3은 49~63을 사용한다.
- 여덟 ADC는 각 1 kΩ/100 nF RC filter를 통과한다(`tau=100 us`, 약 1.59 kHz). Carrier에 분압/clamp가 보이지 않으므로 ADC 허용 전압을 Power Stage에서 보장하고, attenuation/calibration/saturation/단선 처리를 코드와 HIL에 포함한다. GPIO3은 strapping pin이므로 CH3 temperature source 연결 상태의 cold boot/reset을 검증한다.
- Logic Carrier는 `PWM_MAG/DIR`만 전달하며 complementary gate 신호나 dead time을 만들지 않는다. Power Stage가 이를 안전하게 제공하는지 확인하고, direction 전환은 PWM=0/blanking 조건에서 수행한다.
- `PWM_MAG/DIR`에는 buffer/level shifter/절연이 보이지 않는다. 3.3 V input 호환성과 cable noise를 실측한다. 16 kHz/60 Hz는 firmware 요구이지 Carrier 회로가 보장한 값이 아니다.
- J5 E-Stop은 U4를 통해 `CHx_ENABLE`만 차단하고 +24 V/+12 V/+5 V rail이나 PWM/DIR을 끊지 않는다. 동작 후에도 전력부를 live로 취급하고 firmware도 PWM 0을 적용한다. 이 단일 74HC08 경로를 safety-rated power disconnect로 표현하지 않는다.
- 회로도상 U3 TX(GPIO43)/RX(GPIO44)는 NC이고 J7에도 A↔B 통신 경로가 없다. B측 UART는 회로 개정 또는 명시적 외부 harness 없이는 확정된 것이 아니다. GPIO43/44는 DevKit USB-to-UART bridge와, GPIO19/20은 native USB/JTAG와 충돌할 수 있다.
- 현재 `hardware/`에는 PDF 회로도와 분석 문서만 있고 편집 가능한 EDA/CAD source·BOM은 없다. harness connector, USB-to-UART bridge 격리, ADC clamp 등 물리 개정은 CAD 원본과 확정된 board revision을 확보한 뒤 반영하고 새 회로도/PDF 해시와 문서를 함께 갱신한다.
- GPIO39~42는 Logic Carrier 신호와 classic JTAG 핀이 겹친다. ESP32_B에서 JTAG를 이 핀에 attach하지 않는다. N8R8의 Octal PSRAM 관련 GPIO35~37도 대체 UART/GPIO로 임의 사용하지 않는다. DevKitC-1 v1.1에서는 GPIO38이 onboard RGB LED와 공유되므로 CH2 enable을 건드리는 LED/RMT 초기화를 금지한다.
- J6 +5 V pin 공급과 USB 전원 공급을 동시에 사용하지 않는다. 전원 주입 전 J1/J2/J3/J6 극성·전압, 공통 GND와 J7 pin 1/odd-even 방향을 계측한다.
- `ESP32_B_Algo/main/power_stage_pinmap.h`는 Logic Carrier 핀맵을 따르고, B UART는 GPIO43/44를 사용하며, native USB console은 비활성화한다. compile-time 핀 소유권 검사와 host exact-value test를 유지한다. 다만 UART 외부 harness/bridge contention, ADC 물리 단위 환산·입력 보호와 Power Stage fault 차단을 HIL로 확정하기 전에는 Power Stage/PDLC/HV를 연결하지 않는다.
- 저전압 제어부와 고전압 전력부를 분리하고 E-Stop, 퓨즈, 방전저항, HV 표시와 절연 구조를 적용한다.

## 네트워크와 기록 경계

- 현재 TabUI 서버는 MacBook에서 직접 실행하는 plain HTTP와 인증 없는 시연용 구성이다. 기본 사용은 localhost이며 신뢰된 격리 LAN/MOCK/HIL 밖에 직접 노출하지 않는다.
- 외부 네트워크에서는 reverse proxy TLS, 인증 또는 부스 PIN, Origin/CSRF 검사와 request/rate limit을 먼저 적용한다.
- replay는 최근 상태 snapshot 기록이다. command/ACK/fault 감사 로그나 완료된 제어 증거로 간주하지 않는다.

## 개발 및 검증 규칙

- `Simul_Twin/`은 읽기와 실행만 허용하며 파일 생성·수정·삭제·포맷을 하지 않는다.
- `ESP32_A_Algo/`의 카메라 기능은 반드시 프로젝트 내부에서 완결하고 sibling 경로·symlink·`ESP_Camera/` 소스를 빌드 입력으로 사용하지 않는다. 선택적인 `ESP_Camera/`가 남아 있다면 standalone 레퍼런스로만 취급한다.
- ESP32_B 제품 펌웨어 변경, 빌드와 플래시 작업은 유일한 canonical 경로인 `ESP32_B_Algo/`를 기준으로 한다.
- wire schema 변경은 TabUI adapter, ESP32_A, ESP32_B, host tests와 README 예시를 함께 갱신한다.
- ESP32_B의 핀, peripheral, UART, ADC 또는 출력 극성을 바꾸기 전에 `hardware/Logic carrier.pdf`와 `hardware/README.md`를 읽는다. 하나의 board pinmap에서 역할·방향·active level을 정의하고 exact-value, 중복 소유, 금지 핀과 safe-default를 host/static test로 검사한다.
- ESP32_B 부팅 시 네 `ENABLE` LOW와 PWM duty 0을 가장 먼저 보장한다. 유효 명령+TTL, `EN_GLOBAL=HIGH`, 모든 `FAULT_N=HIGH` 이전에는 enable하지 않으며, 차단 시 enable LOW를 먼저 적용한다.
- `FAULT_N`은 U4 하드웨어 AND에 포함되지 않는다. Power Stage 자체 차단이 없다면 펌웨어 polling이 유일한 추가 차단이므로 latency/jitter를 계측하고 요구 한계를 문서화한다.
- 문서와 코드에는 현재 사용하는 구성, 센서와 채널만 기록한다.
- TabUI LIVE 기본값은 `transport=usb`, `usb-port=auto`로 유지한다. `/dev/ttyUSB0`, `TABUI_SERIAL_*` 또는 MacBook↔A 외부 UART를 production 경로로 다시 도입하지 않는다.
- 실제 하드웨어가 없어도 TabUI typecheck/build, backend unit test, ESP32_A/B host test를 실행한다.
- HIL에서는 command ACK, manual TTL, A의 AUTO 지속, A→B timeout safe-off, stale sequence 거부, target/commanded/applied MI 차이를 확인한다.
- Fault reset HIL은 B reboot의 `boot_id` 교체와 status sequence rebase, A reboot의 `source_session_id` 교체, challenge one-time 소비, stale/replayed request 거부, exact `control_result` correlation, timeout ACK을 모두 포함한다.
- ADC HIL은 8개 raw/mV와 validity mask, 0 V·정상·clamp 경계, open/short, power-off injection, board-specific A/°C 환산, GPIO3 source 연결 cold boot를 포함한다.
- 실측 전에는 MI-Vrms-투과도 LUT, 온도 임계값과 카메라 개선 수치를 확정값으로 단정하지 않는다.
