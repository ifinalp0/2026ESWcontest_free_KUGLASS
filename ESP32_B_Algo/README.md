# ESP32_B 제품 펌웨어

`ESP32_B_Algo/`는 KUGLASS의 ESP32_B DevKit에 빌드·플래시하는 canonical
ESP-IDF 프로젝트입니다. ESP32_A의 CH0~CH3 명령을 검증하고 제작된 Logic
Carrier를 통해 단일 채널 Power Stage PCB 4장을 구동합니다.

이 프로젝트는 protocol과 JSON 구현을 자체 소유하며 ESP32_A source에 빌드
의존성이 없습니다. `For_Test/`의 독립 시험 firmware는 제품 대체 구현이 아니며,
제품 변경은 이 폴더와 `host_tests/`를 기준으로 합니다.

## 책임

- A→B full-frame의 version, sequence, TTL, 채널 집합과 값 검증
- CH0~CH3의 16 kHz carrier와 60 Hz polarity SPWM 생성
- E-Stop, invalid frame, timeout과 watchdog 전체 safe-off
- Power Stage Fault의 채널별 latch와 해당 채널만 safe-off
- 방향 전환 blanking과 MI slew
- 실제 applied MI, Fault, ADC와 reset 결과 status 전송
- boot/session/challenge에 묶인 latched Fault reset

## 코드 구조

| 경로 | 역할 |
| --- | --- |
| `main/app_main.cpp` | 하드웨어 초기화, task와 link 조립 |
| `main/control_protocol.*` | actuator/control frame 검증 |
| `main/channel_manager.*` | full-frame 적용, 채널별 Fault 상태, MI slew와 출력 상태 |
| `main/spwm_generator.*` | MCPWM, 60 Hz 위상과 방향 blanking |
| `main/fault_manager.*` | 전역 E-Stop/명령 오류/timeout latch와 reset |
| `main/estop_input_qualifier.h` | E-Stop LOW 확인과 HIGH 안정화 상태 |
| `main/analog_monitor.*` | ADC1 8채널 sampling과 validity |
| `main/status_reporter.*` | B status와 `control_result` |
| `main/power_stage_pinmap.h` | Logic Carrier exact pin 계약 |
| `host_tests/` | 제품 계약의 host 검증 |

## 출력과 안전 동작

```text
ESP32_A -> ESP32_B -> Logic Carrier -> Power Stage PCB ×4 -> PDLC CH0~CH3
                          |                    |
                          +<- Fault/ADC <------+
```

- 초기화 첫 단계에서 네 `ENABLE`을 LOW, MCPWM을 continuous force-low로 만듭니다.
- `EN_GLOBAL` falling edge는 Logic Carrier U4에서 네 `CHx_ENABLE`을 즉시
  차단하고 ISR도 네 software enable을 즉시 LOW로 내립니다. reset이 필요한
  E-Stop latch는 1 ms 출력 주기에서 10회 연속 LOW가 확인될 때 확정합니다. 그
  전에 HIGH로 복귀한 glitch는 10회 연속 HIGH 안정화 뒤 해제하지만, 이전 command
  lease는 재사용하지 않고 다음 유효 full frame까지 전체 출력을 LOW로 유지합니다.
- `FAULT_N_CHx` falling edge도 해당 채널의 enable을 즉시 LOW로 내리지만,
  reset이 필요한 latch는 1 ms 출력 주기에서 10회 연속 LOW가 확인될 때
  확정합니다. 첫 짧은 pulse는 10회 연속 HIGH 안정화 뒤 자동 복구할 수 있지만,
  5초 안에 두 번째 falling edge가 발생하면 self-clearing fault의 반복 재인가로
  판단해 해당 채널을 latch합니다. 나머지 정상 채널은 활성 lease에 따라 계속
  동작합니다.
- invalid/oversize JSON, 불완전·중복 채널, 범위 밖 MI, 활성 lease의 stale
  sequence는 lease 전체를 무효화하고 safe-off합니다.
- timeout과 output task watchdog도 `ENABLE LOW + PWM force-low + applied_mi=0`으로
  처리합니다.
- 유효한 활성 명령과 정상 안전 입력이 유지되는 동안 MCU `ENABLE_CHx`는 정적
  HIGH입니다. SPWM zero crossing이나 방향 blanking을 ENABLE 펄스로 만들지
  않습니다. 명시적 disable/MI 0, invalid frame, timeout, E-Stop, watchdog과 해당
  채널 Fault에서는 LOW입니다.
- 방향 반전 순서는 `PWM force-low -> 1 ms blanking -> DIR 변경 -> 안전 입력 재검사
  -> PWM 재개`입니다. 정상 채널의 `ENABLE_CHx`는 이 과정에서도 HIGH를
  유지합니다.
- 정상 명령의 MI 감소는 12.0 MI/s, 증가는 4.0 MI/s로 B에서만 최종 제한합니다.
  출력 task 지연이 큰 단일 변화로 누적되지 않도록 slew 계산의 `dt`는 최대 2 ms로
  제한합니다. 이 값은 software/HIL 후보이며 Power Stage·PDLC·72 V 실측 완료값이
  아닙니다.
- MI 0, disable, invalid frame, timeout, E-Stop, watchdog과 해당 채널 Fault는 정상
  slew보다 우선하며 `ENABLE LOW + PWM force-low + applied_mi=0`을 즉시 적용합니다.
- IRS2104 bootstrap refresh를 위해 MI/duty는 최대 0.95입니다. 10 MHz MCPWM에서
  1 carrier tick보다 작은 duty는 PWM을 force-low로 유지합니다.
- 물리 E-Stop과 Power Stage `RUN_OK`가 최종 차단 경로지만 인증된 안전 회로는
  아닙니다.

## 하드웨어 계약

핀과 극성의 기준은 [`../hardware/README.md`](../hardware/README.md),
[`esp32_b_io.json`](../hardware/contracts/esp32_b_io.json)과 회로 원본입니다.

| 채널 | `PWM_MAG` | `DIR` | MCU `ENABLE` | `FAULT_N` | Current ADC | Temperature ADC |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CH0 | GPIO10 | GPIO11 | GPIO12 | GPIO13 | GPIO1 / ADC1_CH0 | GPIO2 / ADC1_CH1 |
| CH1 | GPIO14 | GPIO15 | GPIO16 | GPIO17 | GPIO4 / ADC1_CH3 | GPIO5 / ADC1_CH4 |
| CH2 | GPIO18 | GPIO21 | GPIO38 | GPIO39 | GPIO6 / ADC1_CH5 | GPIO7 / ADC1_CH6 |
| CH3 | GPIO40 | GPIO41 | GPIO42 | GPIO47 | GPIO8 / ADC1_CH7 | GPIO3 / ADC1_CH2 |

- `EN_GLOBAL`은 GPIO19 active-high input-only입니다. USB/JTAG나 pull-up으로
  재구성하지 않습니다.
- `FAULT_N`은 외부 10 kΩ pull-up을 사용하는 active-low 입력입니다.
- Logic Carrier가 `CHx_ENABLE = EN_GLOBAL AND ENABLE_CHx`를 만듭니다.
- 각 Power Stage의 `RUN_OK = CHx_ENABLE AND FAULT_N`이 두 IRS2104를 shutdown합니다.
- ADC 입력에는 1 kΩ/100 nF filter가 있지만 divider/clamp가 없습니다.
- GPIO3은 strapping pin, GPIO38은 일부 DevKit의 RGB LED와 공유됩니다.
- J7은 모든 짝수 핀이 GND인 2x32입니다. pin 1과 mating 방향은 실물에서 확인합니다.

ESP32_B를 Power Stage에 직결하거나 Logic Carrier를 생략하지 않습니다.

## A↔B 외부 3선 UART

UART1은 115200 8-N-1, flow control 없음이며 B TX=GPIO43, RX=GPIO44입니다.

```text
ESP32_A GPIO39 TX -> ESP32_B GPIO44 RX
ESP32_A GPIO40 RX <- ESP32_B GPIO43 TX
ESP32_A GND       --- ESP32_B GND
```

각 TX를 상대 장치의 RX에 연결하며 TX끼리 또는 RX끼리 연결하지 않습니다. Logic
Carrier의 U3 TX/RX는 NC이고 J7에도 UART가 없으므로 별도 3선 harness가
필요합니다. GPIO43/44와 DevKit USB-UART bridge의 push-pull contention, ROM boot
byte 유입을 실기에서 확인해야 합니다. `sdkconfig.defaults`는 native USB
Serial/JTAG와 console을 비활성화하고 GPIO 제어 함수를 IRAM에 배치합니다.

## 통신과 ADC 상태

Actuator command, B status와 Fault reset의 frame은
[`../docs/PROTOCOL.md`](../docs/PROTOCOL.md)를 따릅니다.

- B의 status `seq`는 command와 별개이며 100 ms마다 증가합니다.
- nonzero `boot_id`는 부팅 동안 유지되고, `reset_challenge`는 safety trip과 reset
  시도 후 교체됩니다.
- E-Stop과 Power Stage Fault는 모두 10회 연속 LOW에서 reset-required latch로
  확정되며, 입력이 HIGH로 돌아와도 안전 조건과 일치하는 reset이 필요합니다.
  E-Stop의 1~9회 LOW glitch는 즉시 차단된 뒤 10회 연속 HIGH와 새 full command를
  받아야 복구합니다. 채널 `FAULT_N`의 첫 1~9회 LOW pulse는 해당 채널만 즉시
  차단한 뒤 10회 연속 HIGH에서 활성 lease로 자동 복구할 수 있습니다. 그러나
  5초 이내 두 번째 pulse는 latch하여 `차단 → 재상승 → 재차단` 반복을 막습니다.
  이때 status `fault_code`는 `POWER_STAGE_FAULT`이고 해당 `ch[].fault`만 true입니다.
  E-Stop 등 전역 fault reset 뒤에는 새 full command 전까지 출력하지 않습니다.
  채널 Fault만 reset하면 정상 채널 lease는 유지되고 복구 채널은 다음 출력
  주기부터 다시 적용됩니다.
- ADC는 5 ms마다 8채널을 scan하고 settling read, 5-sample median, EWMA 1/8을
  적용합니다. 100 ms보다 오래된 값은 invalid입니다.
- current ADC는 0 dB, temperature ADC는 12 dB attenuation을 사용합니다.
- calibration 생성이 실패해도 raw는 보고하되 해당 mV validity bit는 0입니다.
- 보드별 slope/offset, NTC 계수와 보호 임계가 실측되기 전 raw/mV를 A/°C 또는
  software trip 기준으로 사용하지 않습니다.

## 빌드와 검증

ESP-IDF 6.0.2와 ESP32-S3를 사용합니다.

```bash
python3 ../hardware/tools/validate_hardware_contract.py

idf.py set-target esp32s3
idf.py build
sh host_tests/run_tests.sh
sh ../hardware/validation/BAD_JSON/host_tests/run_tests.sh
```

Host test는 exact pin/ADC, strict reset parser, result correlation, transactional
full frame, 0.95 clamp, 12.0/4.0 MI/s slew, 2 ms 지연 상한, MI 0과 전역 hard
safe-off, E-Stop LOW/HIGH 10-sample
qualification, `FAULT_N` LOW/HIGH 10-sample qualification, 5초 반복 event latch와
채널별 Fault 격리, ADC filter/stale, status, direction blanking 중 정적 ENABLE,
ENABLE commit 거부와 JSONL 복구를 검사합니다.
BAD_JSON regression은 B formatter의 실제 ADC 포함 status를 A parser가 field
순서와 ROM boot banner에 관계없이 수신하는지 교차 검사합니다.

실제 Power Stage/HV를 연결하기 전에는
[`../hardware/validation/README.md`](../hardware/validation/README.md)의 순서로
부팅·reset 출력, 4채널 carrier/polarity, blanking, E-Stop/Fault pulse, UART 실패,
GPIO3/19/38, ADC 입력 범위와 네 Power Stage의 `RUN_OK`를 검증합니다. 최초 시험은
Power Stage와 고전압을 분리한 logic-level 조건에서 시작합니다.
