# ESP32_B_Algo

ESP32_B는 ESP32_A가 계산한 CH0~CH3 목표 MI를 받아 **Logic Carrier에 장착된 상태에서** Power Stage PCB용 신호를 만드는 4채널 actuator 펌웨어입니다. 하드웨어 기준은 [`hardware/Logic carrier.pdf`](<../hardware/Logic carrier.pdf>)와 [`hardware/README.md`](../hardware/README.md)입니다.

`ESP32_B_Algo/`는 ESP32_B 제품 보드에 빌드·플래시하는 유일한 canonical 소스입니다.

```text
ESP32_A
  -> UART1 JSON Lines, CH0~CH3 full frame + TTL
ESP32_B
  -> 4채널 16 kHz SPWM + enable/direction
Logic Carrier
  -> EN_GLOBAL AND gate + Fault/ADC + J7
Power Stage PCB
  -> PDLC CH0~CH3
```

## 책임

- `v=1`, `type=actuator_command`, 단조 증가 `seq`와 CH0~CH3 full/unique set 검증
- 250 ms heartbeat TTL과 stale/duplicate sequence 거부
- 16 kHz carrier, 60 Hz 상당 Simplified Unipolar SPWM 생성
- 부팅, 통신 timeout, E-Stop과 Power Stage Fault 시 safe-off
- 실제 적용 MI와 채널 Fault를 `controller_id=B` 상태 frame으로 회신

ESP32_B는 카메라나 내부온도 정책을 계산하지 않습니다. Logic Carrier 핀을 통한 Power Stage 출력과 안전 상태만 소유합니다.

## 명령

```json
{"v":1,"type":"actuator_command","seq":5501,"ttl_ms":250,"ch":[[0,0.72,true],[1,0.68,true],[2,0.42,true],[3,0.55,true]]}
```

누락·중복·범위 이탈 channel, 잘못된 MI/boolean 또는 active lease 안의 stale sequence는 frame 전체를 거부합니다.

## 상태

```json
{"v":1,"type":"status","controller_id":"B","seq":5501,"estop":false,"fault_code":"NONE","ch":[{"id":0,"mi":0.72,"fault":false},{"id":1,"mi":0.68,"fault":false},{"id":2,"mi":0.42,"fault":false},{"id":3,"mi":0.55,"fault":false}]}
```

`mi`는 B가 현재 적용 중인 값입니다. A가 보낸 목표값으로 대체하지 않습니다.

## Logic Carrier 핀맵

회로도 기준 GPIO는 다음과 같습니다.

| 채널 | `PWM_MAG` | `DIR` | `ENABLE` | `FAULT_N` | Current ADC | Temperature ADC |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CH0 | GPIO10 | GPIO11 | GPIO12 | GPIO13 | GPIO1 | GPIO2 |
| CH1 | GPIO14 | GPIO15 | GPIO16 | GPIO17 | GPIO4 | GPIO5 |
| CH2 | GPIO18 | GPIO21 | GPIO38 | GPIO39 | GPIO6 | GPIO7 |
| CH3 | GPIO40 | GPIO41 | GPIO42 | GPIO47 | GPIO8 | GPIO3 |

- `EN_GLOBAL`은 GPIO19 input-only입니다. NC E-Stop과 외부 10 kΩ pull-down을 firmware가 우회해서는 안 됩니다.
- `FAULT_N`은 외부 10 kΩ pull-up이 있는 active-low 입력입니다.
- Logic Carrier의 실제 출력은 `CHx_ENABLE = EN_GLOBAL AND ENABLE_CHx`입니다.
- ADC 8개는 1 kΩ/100 nF filter를 통과하며 Carrier에 분압/clamp가 없습니다. GPIO3은 strapping pin입니다.
- J7 모든 짝수 핀은 GND이고, 각 채널 홀수 핀은 `PWM_MAG, DIR, CHx_ENABLE, FAULT_N, ADC_I_RAW, ADC_TEMP_RAW, +3.3 V, +12 V` 순서입니다.
- 채널당 `PWM_A/PWM_B`가 아니라 단일 `PWM_MAG/DIR` 인터페이스입니다.

## 구현 상태와 남은 하드웨어 확인

- `main/power_stage_pinmap.h`의 제어·Fault·E-Stop·ADC 예약 핀은 위 Logic Carrier 핀맵과 일치합니다.
- 모든 Carrier 핀과 UART 핀은 compile-time 중복 검사를 거치며 host test가 GPIO exact value를 검사합니다.
- B UART 기본값은 TX=GPIO43, RX=GPIO44입니다. 제어/ADC 핀과는 충돌하지 않지만 회로도에서 U3 TX/RX는 NC이고 A↔B 전용 connector가 없으므로 외부 harness가 필요합니다. DevKit USB-to-UART bridge와의 contention 및 flash/monitor 경로도 실기에서 확정해야 합니다.
- native USB console과 secondary console은 비활성화되어 GPIO19 `EN_GLOBAL`을 점유하지 않습니다.
- DevKitC-1 v1.1의 onboard RGB LED는 GPIO38을 사용하여 `ENABLE_CH2`와 공유되므로 LED/RMT 초기화도 금지해야 합니다.
- 이 코드에는 Logic Carrier의 current/temperature ADC 8개 수집이 아직 없습니다.

최초 플래시는 Power Stage와 고전압을 분리한 상태에서 수행하고, UART harness와 핀 contention을 확인한 뒤 저전압 로직 파형부터 검증하십시오. ADC 보호·수집과 Power Stage 자체 fault 차단을 완성하고 HIL을 통과하기 전에는 PDLC/HV를 연결하지 마십시오.

초기화는 enable LOW/PWM 0을 가장 먼저 보장하고, `EN_GLOBAL=HIGH`, 모든 `FAULT_N=HIGH`, 유효 full frame과 TTL을 만족한 뒤에만 출력합니다. Fault는 Logic Carrier U4의 enable AND에는 포함되지 않으므로 polling latency와 실제 Power Stage revision의 `FAULT_N/RUN_OK` 자체 차단도 HIL에서 검증합니다.

## 빌드와 검증

```bash
cd ESP32_B_Algo
idf.py set-target esp32s3
idf.py build
sh host_tests/run_tests.sh
```

HIL에서는 exact GPIO·active level, 부팅/reset 기본 off, 네 채널 출력, A→B timeout, stale sequence, E-Stop hardware AND, Power Stage Fault, ADC 8개, GPIO3 cold boot와 B→A 상태 회신을 확인합니다.
