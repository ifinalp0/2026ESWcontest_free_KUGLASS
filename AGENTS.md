# KUGLASS AI 작업 계약

이 파일은 저장소에서 AI 에이전트가 따라야 하는 단일 진입점이다. 작업 전 이
문서를 읽고, 변경 대상 폴더의 `README.md`와 아래에 지정된 기준 문서를 추가로
확인한다.

## 1. 문서와 구현의 기준

서로 다른 종류의 정보는 다음 기준을 사용한다.

| 정보 | 기준 |
| --- | --- |
| 작품 목적·범위 | [`개발 계획서.md`](개발 계획서.md) |
| 현재 시스템 구조·실행 방법 | [`README.md`](README.md), [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| 통신 형식·상태 의미 | [`docs/PROTOCOL.md`](docs/PROTOCOL.md), 현재 코드와 host test |
| 구현된 동작 | `TabUI/`, `ESP32_A_Algo/`, `ESP32_B_Algo/`의 현재 코드와 동반 테스트 |
| 물리 핀·전원·커넥터·안전 | `hardware/manifest.json`, `hardware/contracts/*.json`, KiCad/PDF 원본 |

- 문서와 구현이 다르면 추측하지 않는다. 차이를 확인하고 현재 구현을 기준으로
  수정 범위를 판단하되, 하드웨어 사실은 반드시 하드웨어 기준을 우선한다.
- `unknown`, 미실측 값, 누락 자료를 임의의 확정값으로 채우지 않는다.
- 문서에는 현재 사용하는 구성만 기록한다. 계획, 후보, 실측 완료 상태를 구분한다.

## 2. 제품 범위

- KUGLASS는 **1:10 차량 모형용 PDLC 4채널 시연 프로토타입**이다. 실차 안전
  장치나 자율주행 인지 성능 향상 장치로 표현하지 않는다.
- ESP32_A와 ESP32_B는 소프트웨어 모듈명이 아니라 서로 다른 두 대의
  ESP32-S3 DevKit 장치다.
- `ESP32_A_Algo/`와 `ESP32_B_Algo/`가 각 장치에 빌드·플래시하는 유일한 제품
  펌웨어 프로젝트다.
- 제품 입력은 카메라 1대와 YwRobot SEN050007 DS18B20 내부온도센서 모듈
  1개뿐이다.
- 채널은 항상 CH0~CH3이다. 코드, 프로토콜, UI, 테스트, 문서의 범위를 함께
  유지한다.

### 디렉터리 경계

- `TabUI/`: MacBook에서 실행하는 production 백엔드, 브라우저 HMI,
  ESP32_A USB gateway.
- `ESP32_A_Algo/`: 입력 처리, 정책, 목표 MI, A→B 통신을 소유하는 제품 펌웨어.
- `ESP32_B_Algo/`: 4채널 SPWM, Logic Carrier/Power Stage 제어, Fault와 TTL을
  소유하는 제품 펌웨어.
- `hardware/`: 제작된 하드웨어 원본과 기계 판독 계약, 안전·HIL 기준.
- `For_Test/`: 독립 시험용 펌웨어와 실험 코드. 사용자가 특정 시험을 지목한
  경우에만 `For_Test/README.md`를 먼저 읽고 해당 하위 폴더만 제한적으로 본다.
  일반 탐색, 설계 판단, 일괄 검색, 제품 구현의 근거로 사용하지 않는다.
- 제품 폴더 안의 `host_tests/`는 제품 계약의 일부다. 제품 변경 시 제외하지 않는다.
- `Simul_Twin/`: 독립 디지털 트윈. 읽기와 실행만 허용하며 파일 생성, 수정,
  삭제, 포맷을 하지 않는다. production 명령 경로에도 연결하지 않는다.

## 3. 시스템 책임

```text
카메라 + DS18B20 내부온도
  -> ESP32_A: 입력 품질, 정책, LUT, MI servo, CH0~CH3 목표 MI
  -> A-B UART JSON Lines: 20 Hz full frame + 250 ms TTL
ESP32_B: frame 검증, 4채널 SPWM, 로컬 Fault/timeout 차단
  -> Logic Carrier: EN_GLOBAL gate, Fault/ADC, J7
  -> 단일 채널 Power Stage PCB 4장
  -> PDLC CH0~CH3
```

```text
브라우저 -> MacBook TabUI frontend/backend
  -> 데이터 micro-USB -> ESP32_A DevKit USB Serial/JTAG CDC
ESP32_A -> 상태·ACK·B 적용 MI/Fault -> TabUI -> 브라우저
```

- 자동 판단과 권위 있는 `target_mi`는 ESP32_A가 소유한다.
- ESP32_B는 목표를 재계산하지 않는다. 명령 검증, 출력, 로컬 차단과 실제 적용
  상태 회신만 담당한다.
- ESP32_B는 Logic Carrier U3에 장착한다. Carrier의 하드웨어 게이트와 J7을
  생략해 Power Stage에 임의 직결하지 않는다.
- TabUI는 화면, API, 명령 검증, 상태 집계, replay snapshot과 USB gateway를
  담당한다. LIVE에서 자동 MI를 계산하지 않는다.
- `target_mi`, A가 전송한 `commanded_mi`, B가 보고한 `applied_mi`를 구분한다.
- MI는 값이 클수록 CLEAR이며 현재 운용 상한 0.60이 완전 투명 방향이다.
  0.0 또는 disable은 전원 차단·강산란 방향이다.

## 4. 런타임과 통신 불변조건

상세 frame 예시와 필드 설명은 [`docs/PROTOCOL.md`](docs/PROTOCOL.md)를 따른다.

- TabUI→A는 `v=1`, `type=ui_command`, 단조 증가 `seq`를 사용한다.
- A→B는 `v=1`, `type=actuator_command`, CH0~CH3 full/unique set, `ttl_ms`,
  단조 증가 `seq`를 사용한다.
- B→A는 `v=1`, `type=status`, `controller_id=B`, CH0~CH3 full/unique set,
  실제 `mi`/`fault`, `estop`, `fault_code`를 사용한다.
- 모든 B status에는 nonzero u32 `boot_id`와 `reset_challenge`가 필요하다.
  `diagnostic`, `adc`, `control_result`는 정의된 용도에서만 추가한다.
- 채널 ID는 0~3, MI는 유한한 0.0~0.60, enable/fault는 JSON boolean이다.
  누락, 중복, 타입 오류, 범위 이탈 frame은 부분 적용하지 않고 전체 거부한다.
- B status `seq`는 B가 소유하는 독립 sequence다. A는 같은 `boot_id` 안에서만
  비교하며 새 `boot_id`를 받으면 sequence 기준을 즉시 재설정한다.
- A는 부팅마다 nonzero u32 `source_session_id`를 만든다. Fault reset은
  `v,type,seq,source_session_id,target_boot_id,reset_challenge,command`를 모두
  가진 `type=control`, `command=reset_fault` frame으로만 보낸다.
- B는 reset 결과를 `control_result.command/seq/source_session_id/ok/error`로
  보고한다. A는 B boot, A session, request sequence가 모두 일치하는 결과만
  ACK한다. UART write 완료를 reset 성공으로 간주하지 않으며 timeout에서 실패한다.
- backend→A 명령 lease와 A→B heartbeat TTL을 분리한다. TabUI 장애는 A의 AUTO를
  멈추지 않지만, B가 A heartbeat를 잃으면 safe-off한다.
- B는 활성 lease 동안 duplicate/stale sequence를 heartbeat로 인정하지 않는다.
  timeout과 safe-off 뒤 첫 유효 full frame에서만 기준을 다시 설정한다.
- wire schema를 바꾸면 TabUI adapter, A, B, host test, 프로토콜 문서를 같은
  변경에서 함께 갱신한다.

## 5. 센서·모드·상태 경계

- production 입력은 카메라 ROI/AE metadata와 DS18B20 내부온도뿐이다.
- LIVE에서 태블릿 값으로 카메라, 온도 또는 latched fault를 덮어쓰지 않는다.
- `set_environment`, `set_channel_fault`는 명시적 MOCK 또는 양쪽에서 허용한 HIL
  빌드에서만 사용한다.
- 카메라 AE exposure/gain 단위가 검증되지 않았으면
  `ae_metadata_valid=false`로 보고하고 해당 정책 항에서 제외한다.
- 카메라나 온도가 stale이면 결측으로 표시한다. 임의 기본값을 실제 측정값처럼
  사용하지 않는다.
- 우선순위는 `E-Stop > 채널별 latched Fault > Manual(관리자 지속/일반 TTL) > Demo/Auto`다.
- LIVE 연결이 끊기면 마지막 실제 상태를 `STALE/OFFLINE`으로 표시하고 명령을
  막는다. MOCK으로 자동 전환하지 않는다.
- MOCK/REPLAY는 ESP32_A USB 장치를 열거나 ESP32_B 출력 명령을 만들지 않는다.
- UI는 runtime mode, A/B online·stale, last seen, control source, 관리자 지속/일반 manual TTL,
  target/commanded/applied MI, 센서 품질, E-Stop/Fault를 구분한다.
- replay는 최근 상태 snapshot이다. 명령/ACK/Fault 감사 로그나 제어 완료 증거로
  표현하지 않는다.

## 6. 하드웨어 작업 규칙

Logic Carrier 1장과 동일한 단일 채널 Power Stage 4장은 제작 완료된 as-built
하드웨어다. 사용자가 하드웨어 설계 변경을 명시하지 않은 작업에서는 소프트웨어와
문서를 하드웨어에 맞추며 KiCad 원본, 커넥터, 핀 계약을 바꾸지 않는다.

하드웨어 관련 작업은 반드시 다음 순서로 확인한다.

1. [`hardware/README.md`](hardware/README.md)
2. [`hardware/manifest.json`](hardware/manifest.json)
3. [`hardware/contracts/esp32_b_io.json`](hardware/contracts/esp32_b_io.json)
4. [`hardware/contracts/power_stage.json`](hardware/contracts/power_stage.json)
5. [`hardware/contracts/safety.json`](hardware/contracts/safety.json)
6. 대상 보드 README
7. 대상 KiCad/PDF 원본
8. 실기 작업이면 [`hardware/validation/README.md`](hardware/validation/README.md)

자료가 충돌하거나 값이 `unknown`이면 중단하고 원본과 실물을 대조한다.

### 고정 연결

- TabUI↔A: `MacBook -> 데이터 micro-USB -> ESP32_A DevKit USB 단자 ->
  USB Serial/JTAG(GPIO19/20)`. 외부 GPIO UART를 추가하지 않는다. macOS에서는
  `/dev/cu.usbmodem*`를 사용하고 TabUI LIVE 기본은 `transport=usb`,
  `usb-port=auto`다.
- ESP32_A와 ESP32_B는 다음 외부 3선 UART harness로 교차 연결한다. TX끼리 또는
  RX끼리 연결하지 않는다.

  ```text
  ESP32_A GPIO39 TX -> ESP32_B GPIO44 RX
  ESP32_A GPIO40 RX <- ESP32_B GPIO43 TX
  ESP32_A GND       --- ESP32_B GND
  ```

  UART1 설정은 115200 8-N-1, flow control 없음이다. Logic Carrier에는 이 링크가
  라우팅되지 않으므로 반드시 별도 harness를 사용한다.
- ESP32_A의 DS18B20 DAT는 GPIO41이다. A의 UART/센서 핀과 ESP32_B 핀맵은
  별개이며, 카메라 핀과 실제 DevKit header 충돌은 HIL 전에 확인한다.
- DS18B20 모듈은 3.3 V 외부전원과 공통 GND를 사용한다. parasite power는
  사용하지 않는다.
- Logic Carrier 회로의 U3 TX(GPIO43)/RX(GPIO44)는 NC이고 J7에도 A↔B 통신
  경로가 없다. ESP32_B GPIO43/44와 DevKit bridge의 contention을 실측한다.

### ESP32_B / Logic Carrier 핀 계약

| 채널 | `PWM_MAG` | `DIR` | MCU `ENABLE` | `FAULT_N` | Current ADC | Temperature ADC |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CH0 | GPIO10 | GPIO11 | GPIO12 | GPIO13 | GPIO1 | GPIO2 |
| CH1 | GPIO14 | GPIO15 | GPIO16 | GPIO17 | GPIO4 | GPIO5 |
| CH2 | GPIO18 | GPIO21 | GPIO38 | GPIO39 | GPIO6 | GPIO7 |
| CH3 | GPIO40 | GPIO41 | GPIO42 | GPIO47 | GPIO8 | GPIO3 |

- `EN_GLOBAL`은 GPIO19 **input-only**다. J5 NC E-Stop이 열리면 R18 10 kΩ이
  LOW로 만든다. GPIO19를 output, USB 또는 강제 pull-up으로 구성하지 않는다.
- `FAULT_N_CH0~3`은 active-low이며 R19~R22 10 kΩ 외부 pull-up을 사용한다.
  LOW는 fault다. 첫 짧은 pulse는 10회 연속 HIGH 뒤 복구할 수 있지만 5초 안의
  두 번째 falling edge는 해당 채널을 latch한다. HIGH만으로 커넥터 연결을
  증명하지 않는다.
- J7은 2x32, 모든 짝수 핀은 GND다. 각 채널의 홀수 8핀은
  `PWM_MAG, DIR, CHx_ENABLE, FAULT_N, ADC_I_RAW, ADC_TEMP_RAW, +3.3V, +12V`
  순서다. CH0=1~15, CH1=17~31, CH2=33~47, CH3=49~63이다.
- Logic Carrier의 `CHx_ENABLE`은 `EN_GLOBAL AND ENABLE_CHx`다. J5는 enable만
  차단하고 +24/+12/+5 V rail이나 PWM/DIR은 끊지 않는다. safety-rated power
  disconnect로 표현하지 않는다.
- Power Stage의 `RUN_OK = CHx_ENABLE AND FAULT_N`이 양쪽 IRS2104 shutdown을
  구동한다. Logic Carrier U4 AND에는 `FAULT_N`이 포함되지 않는다.
- ADC 8개는 1 kΩ/100 nF RC filter를 통과한다. Carrier에 divider/clamp가 없으므로
  입력 범위, attenuation, calibration, saturation, 단선을 실측한다.
- ADC raw/mV 수집과 validity telemetry는 구현되었다. 보드별 전류 A·온도 °C
  calibration과 보호 임계/HIL이 확정되기 전에는 물리량이나 보호 판단으로
  승격하지 않는다.
- GPIO3은 CH3 temperature ADC와 strapping pin을 공유한다. GPIO38은 CH2 enable과
  일부 DevKit RGB LED를 공유한다. GPIO39~42에는 classic JTAG를 attach하지 않고,
  GPIO35~37도 Octal PSRAM 때문에 임의 사용하지 않는다.
- Logic Carrier는 `PWM_MAG/DIR`만 전달한다. Power Stage IRS2104가 complementary
  drive와 dead time을 담당한다. 방향 전환은 PWM=0/blanking 조건에서 수행한다.
- `PWM_MAG/DIR`에는 buffer, level shifter, 절연이 보이지 않는다. 3.3 V 호환성과
  cable noise를 실측한다. 16 kHz/60 Hz는 펌웨어 요구다.
- J6 +5 V 공급과 USB 전원 공급을 동시에 사용하지 않는다. 전원 인가 전
  J1/J2/J3/J6 극성·전압, 공통 GND, J7 pin 1 방향을 계측한다.
- 저장소에 없는 fabrication/BOM/ADC 하위시트 자료를 추측하지 않는다.
  자료 보유 상태는 `hardware/manifest.json`을 따른다.
- 확인된 안전 요소는 Logic Carrier F1과 J5 enable gate다. 절연, 외부 퓨즈,
  방전, HV 표시와 접근 통제는 실물에서 별도 확인한다.

### 안전 출력 순서

- ESP32_B 부팅 시 네 `ENABLE=LOW`, PWM duty 0을 가장 먼저 보장한다.
- 유효 full command와 TTL, `EN_GLOBAL=HIGH` 이전에는 어떤 채널도 enable하지
  않는다. 각 채널은 자신의 `FAULT_N_CHx=HIGH`일 때만 enable한다.
- 차단 시 enable LOW를 먼저 적용하고 PWM 0과 safe direction을 적용한다.
- 정상 활성 lease에서 `ENABLE_CHx`는 SPWM zero crossing과 방향 blanking 동안에도
  정적 HIGH를 유지한다. 방향 전환은 PWM force-low, blanking, DIR 변경, 안전 입력
  재검사, PWM 재개 순서를 지키며 disable 또는 안전 조건에서는 enable을 LOW로
  내린다.
- 최대 MI는 IRS2104 bootstrap refresh와 현재 운용 정책을 위해 0.60을 넘지 않는다.
- E-Stop, 잘못된 frame, timeout, watchdog은 전체 safe-off한다. latched Power Stage
  Fault는 해당 채널만 safe-off하고 나머지 정상 채널은 활성 lease를 계속 따른다.
- Fault clear는 B가 target boot, one-time challenge와 실제 안전 입력을 확인한
  뒤에만 허용한다. unsafe matching 요청도 challenge를 소비해야 한다.
- Power Stage/PDLC/HV는 외부 UART contention, ADC 입력·환산, Fault 차단을 HIL로
  확정하기 전 연결하지 않는다. 실기 검증은 logic-only와 저전압부터 진행한다.

## 7. 네트워크 경계

- TabUI는 현재 MacBook에서 직접 실행하는 인증 없는 plain HTTP 시연 서버다.
  localhost 또는 신뢰된 격리 LAN/MOCK/HIL에서만 사용한다.
- 외부 네트워크 노출 전 reverse proxy TLS, 인증 또는 부스 PIN, Origin/CSRF 검사,
  request/rate limit을 먼저 설계한다.

## 8. 변경 방법

- 작업 전 대상 코드, 동반 테스트, 해당 README를 함께 읽는다.
- 일반 검색은 `For_Test/`, `Simul_Twin/`, dependency/build 산출물을 제외한다.
- `ESP32_A_Algo/` 카메라 기능은 프로젝트 내부에서 완결한다. sibling 경로,
  symlink, `For_Test/` 소스를 빌드 입력으로 사용하지 않는다.
- 제품 코드를 `For_Test/` 구현으로 대체하거나 그 코드를 복사해 계약 근거로
  삼지 않는다. 시험 결과를 반영할 때 canonical 코드와 하드웨어 계약을 다시
  검증한다.
- ESP32_B 변경은 유일한 canonical 경로 `ESP32_B_Algo/`에서 수행한다.
- 기능 변경 시 코드, 관련 host test, 컴포넌트 README와 상위 문서의 사실관계를
  한 작업에서 맞춘다.
- 문서를 고칠 때 루트 README에는 개요와 진입점, 컴포넌트 README에는 로컬
  실행·배선·검증, `docs/`에는 시스템 공통 계약을 둔다. 같은 상세 예시를 여러
  문서에 복제하지 않는다.

## 9. 검증 규칙

하드웨어가 없어도 변경 범위에 맞는 정적·host 검증을 수행한다.

| 변경 범위 | 최소 검증 |
| --- | --- |
| 문서만 | 링크·경로·명령 존재 여부, 관련 문서 간 용어와 값 비교 |
| TabUI | `cd TabUI && npm run check && npm run build` |
| ESP32_A | `cd ESP32_A_Algo && sh host_tests/run_tests.sh` |
| ESP32_B 일반 | `cd ESP32_B_Algo && sh host_tests/run_tests.sh` |
| ESP32_B 핀·ADC·connector·안전 | 하드웨어 계약 검사 후 ESP32_B host test |

ESP32_B 하드웨어 영향 변경의 필수 명령:

```bash
python3 hardware/tools/validate_hardware_contract.py
cd ESP32_B_Algo
sh host_tests/run_tests.sh
```

HIL에서는 command ACK, 관리자 지속/일반 manual TTL, A의 AUTO 지속, A→B timeout safe-off,
stale sequence 거부, target/commanded/applied MI 분리를 확인한다. `FAULT_N`은
첫 짧은 pulse 뒤 10회 연속 HIGH 복구, 5초 안의 두 번째 falling edge latch와
10회 연속 LOW latch를 확인한다. Fault reset은
B reboot/`boot_id`, A reboot/`source_session_id`, one-time challenge, replay 거부,
정확한 `control_result` correlation과 timeout을 포함한다. ADC는 8개 raw/mV,
validity mask, 0 V·정상·clamp 경계, open/short, power-off injection, 보드별
A/°C calibration과 GPIO3 source 연결 cold boot를 포함한다. 네 제작 Power Stage
각각에서 `RUN_OK` 하드웨어 차단과 firmware Fault latch latency를 계측한다.

실측 전에는 MI-Vrms-투과도 LUT, 내부온도 임계값, 카메라 개선 수치, 열·절연
성능을 확정값으로 단정하지 않는다.
