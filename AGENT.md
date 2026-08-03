# KUGLASS 개발 에이전트 지침

## 기준 문서

- 제품 구조와 개발 범위의 기준은 루트의 `개발 계획서.md`이다.
- 작품은 1:10 차량 모형용 PDLC 4채널 시연 프로토타입이다. 실차 안전 장치나 자율주행 인지 성능 향상 장치로 표현하지 않는다.
- 입력 센서는 카메라 1대와 DS18B20 내부온도센서 1개만 사용한다.
- 채널 범위는 CH0~CH3이다. 코드, 프로토콜, UI, 테스트와 문서에서 이 범위를 동일하게 유지한다.

## 런타임 구조

```text
카메라 + DS18B20 내부온도센서
  -> ESP32_A: 입력 처리, 정책, LUT, MI servo, CH0~CH3 목표 MI
  -> UART 또는 RS-485 JSON Lines, 20 Hz full frame + 250 ms TTL
ESP32_B: 명령 검증, 4채널 SPWM, 로컬 Fault/timeout 차단
  -> Power Stage PCB
  -> PDLC CH0~CH3
```

```text
태블릿 브라우저
  -> 격리 LAN의 동일 출처 HTTP
Docker TabUI frontend + backend
  -> USB CDC JSON Lines
ESP32_A
  -> 상태·ACK·ESP32_B 적용 MI/Fault
TabUI -> 태블릿
```

- 자동 판단과 권위 있는 목표 MI는 ESP32_A가 소유한다.
- ESP32_B는 목표를 다시 계산하지 않고 검증·구동·로컬 차단과 적용 상태 회신을 담당한다.
- ESP32_B는 Power Stage PCB를 직접 제어한다. 별도의 중간 신호 분배 PCB를 전제로 하지 않는다.
- TabUI는 UI 정적 파일, API, 명령 검증, 상태 집계, replay snapshot과 장치 gateway를 담당한다. LIVE 모드에서 자동 MI를 계산하지 않는다.

## 디렉터리 역할

- `개발 계획서.md`: 제품 구조, 개발 범위, 검증 목표의 기준 문서.
- `TabUI/`: production 태블릿 HMI와 Docker 백엔드.
- `ESP32_A_Algo/`: 카메라·내부온도 입력, 정책/MI master, A→B 통신 펌웨어.
- `ESP32_A_Algo/ESP_Camera/`: OV2640 카메라 서비스와 독립 검증 도구. 통합에 필요한 카메라 코드이므로 보존한다.
- `ESP32_B_Algo/`: CH0~CH3 SPWM, Power Stage 직접 제어, Fault와 A→B TTL 처리 펌웨어.
- `Simul_Twin/`: 독립 디지털 트윈 시뮬레이터. **사용자 지시에 따라 절대 수정하지 않는다.** production 하드웨어 명령 경로에도 연결하지 않는다.

## 명령과 상태 계약

- TabUI→A: `v=1`, `type=ui_command`, 단조 증가 `seq`를 사용한다.
- A→B: `v=1`, `type=actuator_command`, CH0~CH3의 full/unique set, `ttl_ms`, 단조 증가 `seq`를 사용한다.
- B→A: `v=1`, `type=status`, `controller_id=B`, CH0~CH3의 full/unique set과 실제 `mi`·`fault`를 사용한다.
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

## 안전 및 상태 표시

- 우선순위는 `E-Stop > latched Fault > Manual(TTL) > Demo/Auto`이다.
- ESP32_B는 부팅, 잘못된 frame, heartbeat timeout, E-Stop과 Fault에서 출력을 차단한다.
- Fault clear는 ESP32_B가 실제 안전 조건을 확인한 뒤에만 수행한다.
- LIVE 연결이 끊기면 마지막 실제 상태를 `STALE/OFFLINE`으로 표시하고 명령을 막는다. MOCK으로 자동 전환하지 않는다.
- MOCK/REPLAY는 serial 장치를 열거나 ESP32_B 출력 명령을 만들면 안 된다.
- UI는 runtime mode, A/B online·stale, last seen, control source, manual TTL, target/commanded/applied MI, 카메라·온도 품질, E-Stop/Fault를 구분한다.

## 하드웨어 경계

- ESP32_A 기준 핀은 native USB CDC(GPIO19/20), ESP32_B UART1(GPIO39/40), 외부전원 DS18B20(GPIO41)이다. 실제 DevKit header와 카메라 GPIO 충돌을 HIL 전에 다시 확인한다.
- DS18B20 data에는 외부 4.7 kΩ pull-up과 공통 3.3 V/GND가 필요하다. parasite power는 사용하지 않는다.
- ESP32_B의 CH0~CH3 Power Stage 핀맵, MCPWM 자원과 부팅 기본 off 동작은 실제 보드에서 검증한다.
- 저전압 제어부와 고전압 전력부를 분리하고 E-Stop, 퓨즈, 방전저항, HV 표시와 절연 구조를 적용한다.

## 네트워크와 기록 경계

- 현재 TabUI 서버는 plain HTTP와 인증 없는 시연용 구성이다. 신뢰된 격리 LAN/MOCK/HIL 밖에 직접 노출하지 않는다.
- 외부 네트워크에서는 reverse proxy TLS, 인증 또는 부스 PIN, Origin/CSRF 검사와 request/rate limit을 먼저 적용한다.
- replay는 최근 상태 snapshot 기록이다. command/ACK/fault 감사 로그나 완료된 제어 증거로 간주하지 않는다.

## 개발 및 검증 규칙

- `Simul_Twin/`은 읽기와 실행만 허용하며 파일 생성·수정·삭제·포맷을 하지 않는다.
- wire schema 변경은 TabUI adapter, ESP32_A, ESP32_B, host tests와 README 예시를 함께 갱신한다.
- 문서와 코드에는 현재 사용하는 구성, 센서와 채널만 기록한다.
- 실제 하드웨어가 없어도 TabUI typecheck/build, backend unit test, ESP32_A/B host test를 실행한다.
- HIL에서는 command ACK, manual TTL, A의 AUTO 지속, A→B timeout safe-off, stale sequence 거부, fault latch/clear, target/commanded/applied MI 차이를 확인한다.
- 실측 전에는 MI-Vrms-투과도 LUT, 온도 임계값과 카메라 개선 수치를 확정값으로 단정하지 않는다.
