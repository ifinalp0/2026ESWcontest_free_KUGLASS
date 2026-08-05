# ESP32_B_TEST

> **주의:** Power Stage를 모두 분리한 상태에서 ESP32_B의 logic-level 출력만
> 측정하기 위한 테스트 펌웨어입니다. Power Stage 또는 고전압이 연결된 장비에
> 이 펌웨어를 사용하지 마십시오.

이 폴더는 `ESP32_B_Algo`와 동일한 출력 알고리즘을 사용하되, 한 가지 동작만
다릅니다. ESP32_A 명령과 Power Stage 안전 입력이 없어도 독립 출력 테스트가
진행됩니다.

- 부팅 후 CH0~CH3 모두 `MI=0.25`, `enable=true`인 내부 명령을 1 ms마다
  갱신합니다. 따라서 ESP32_A UART 명령과 TTL lease가 필요하지 않습니다.
- 연결되지 않은 `EN_GLOBAL`/`FAULT_N` 상태는 software 출력 허가 조건에서
  제외하며, 해당 GPIO의 ISR와 내부 pull-up도 설정하지 않습니다.
- `PWM_MAG`, `DIR`, MCU의 원시 `ENABLE_CHx` 핀에서는 기존과 같은 16 kHz
  carrier/60 Hz SPWM을 확인할 수 있습니다.
- Logic Carrier J7의 `CHx_ENABLE`은 여전히 하드웨어 식
  `EN_GLOBAL AND ENABLE_CHx`를 따릅니다. J7에서 enable을 측정하려면 E-Stop
  NC 회로가 연결되어 `EN_GLOBAL=HIGH`여야 합니다.

독립 출력 테스트 이외의 핀맵, SPWM, 방향 blanking, MI slew, ADC/status 및
watchdog 구현은 `ESP32_B_Algo`와 같습니다.

ESP32_B는 ESP32_A에서 받은 CH0~CH3 명령을 검증하고, Logic Carrier를 통해 CH0~CH3에 한 장씩 연결된 동일 단일 채널 Power Stage PCB 네 장에 `PWM_MAG`, `DIR`, `ENABLE`을 출력하는 actuator 전용 ESP32-S3 펌웨어입니다. 핀과 신호 극성은 [`Logic carrier.pdf`](<../hardware/Logic carrier.pdf>)와 [`Power_stage.pdf`](<../hardware/Power_stage.pdf>)를 기준으로 구현했습니다.

이 프로젝트는 protocol/JSON 구현까지 `ESP32_B_Algo/` 안에 포함하므로 ESP32_A 소스 디렉터리에 빌드 의존성이 없습니다.

```text
ESP32_A
  -> UART JSONL: 4채널 full command + seq + TTL
ESP32_B
  -> 검증 / fault latch / MI slew / 60 Hz SPWM
Logic Carrier
  -> CHx_ENABLE = EN_GLOBAL AND ENABLE_CHx
단일 채널 Power Stage PCB x4 (CH0~CH3 각 1장)
  -> RUN_OK = CHx_ENABLE AND FAULT_N
  -> PWM_LEFT = PWM_MAG AND DIR
  -> PWM_RIGHT = PWM_MAG AND NOT DIR
```

## 안전 동작

- 초기화 첫 단계에서 네 `ENABLE`을 LOW로 만들고 MCPWM을 continuous force-low로 고정합니다.
- E-Stop `EN_GLOBAL`과 네 `FAULT_N`의 falling edge는 ISR에서 latch하며, 모든 software enable을 즉시 LOW로 내립니다.
- ISR는 lock을 기다리지 않고 trip을 먼저 기록해 enable을 내리며, 출력 task는 ENABLE commit 전후의 trip/event를 모두 검사합니다. 입력이 다시 HIGH가 되어도 승인되지 않은 edge가 있으면 재활성화되지 않습니다.
- 잘못된 JSON, oversize line, 불완전/중복 channel set, 범위 밖 MI, active lease 중 stale sequence는 현재 command lease 전체를 즉시 무효화하고 safe-off합니다.
- 통신 TTL 초과, E-Stop, Power Stage fault, 출력 task watchdog 실패도 `ENABLE LOW + PWM force-low + applied_mi=0`으로 처리합니다.
- 방향 반전은 `ENABLE LOW -> PWM force-low -> 1 ms blanking -> DIR 변경 -> PWM 준비 -> 안전 입력 재검사 -> ENABLE HIGH` 순서입니다.
- Power Stage의 IRS2104 bootstrap refresh 여유를 위해 MI/duty는 최대 `0.95`로 제한합니다. 10 MHz MCPWM에서 1 carrier tick보다 작은 duty는 enable하지 않습니다.
- 출력 task는 실제 경과시간으로 60 Hz 위상과 MI slew를 갱신하며 100 ms task watchdog에 등록됩니다.

물리 E-Stop과 Power Stage의 `RUN_OK` 차단이 최종 안전 경로입니다. 이 펌웨어만으로 인증된 안전 회로를 대체하지 않습니다.

## 명령과 reset

정상 출력 명령은 CH0~CH3를 정확히 한 번씩 포함해야 합니다.

```json
{"v":1,"type":"actuator_command","seq":5501,"ttl_ms":250,"ch":[[0,0.72,true],[1,0.68,true],[2,0.42,true],[3,0.55,true]]}
```

- `seq`는 active lease 동안 wrap-safe forward 값이어야 합니다.
- `ttl_ms` 허용 범위는 50~1000 ms입니다.
- 통신 timeout 또는 invalid command fault는 다음 정상 full frame으로 복구할 수 있습니다.
- E-Stop/Power Stage fault는 입력이 HIGH로 복귀해도 latch되며 명시적 reset이 필요합니다.

```json
{"v":1,"type":"control","seq":9001,"source_session_id":2712847316,"target_boot_id":305419896,"reset_challenge":2271560481,"command":"reset_fault"}
```

`source_session_id`는 A가 부팅마다 생성한 nonzero u32, `target_boot_id`와 `reset_challenge`는 가장 최근 B status에서 받은 값이어야 합니다. B는 challenge를 각 safety trip과 유효 reset 시도마다 원자적으로 교체하므로 지연·재생된 reset이 이후 fault를 지울 수 없습니다. 모든 안전 입력이 HIGH이고 challenge 확인 뒤 새 ISR event가 없을 때만 fault를 clear하며, 성공 후에도 새 actuator full frame이 오기 전까지 출력하지 않습니다. A는 UART write만으로 성공 처리하지 않고 아래 `control_result`를 정확한 `source_session_id`와 `seq`로 확인해야 합니다.

## 상태와 ADC

B가 송신하는 모든 프레임은 현재 A가 파싱할 수 있는 `type=status` 형식입니다. `seq`는 command sequence가 아니라 B의 독립적인 status sequence이므로 100 ms 주기 frame마다 증가합니다. 부팅·오류·reset 결과는 선택적 `diagnostic` 필드로 전달됩니다.

```json
{"v":1,"type":"status","controller_id":"B","seq":101,"boot_id":305419896,"reset_challenge":2271560481,"estop":false,"fault_code":"NONE","ch":[{"id":0,"mi":0.7200,"fault":false},{"id":1,"mi":0.6800,"fault":false},{"id":2,"mi":0.4200,"fault":false},{"id":3,"mi":0.5500,"fault":false}],"adc":{"initialized":true,"i_cali":true,"t_cali":true,"raw_valid_mask":255,"mv_valid_mask":255,"i_raw":[120,121,119,122],"t_raw":[2010,2002,2021,1998],"i_mv":[28,29,28,29],"t_mv":[1620,1614,1628,1611]}}
```

- `boot_id`는 B 부팅 동안 고정되고 재부팅 때 바뀝니다. `reset_challenge`는 one-time reset 권한이며 safety trip 또는 reset 시도 뒤 바뀝니다. 둘 다 nonzero u32입니다.
- reset 응답 status에는 `control_result:{"command":"reset_fault","seq":9001,"source_session_id":2712847316,"ok":true,"error":"NONE"}`가 추가됩니다. 실패 error는 `RESET_UNSAFE`, `TARGET_BOOT_MISMATCH`, `CHALLENGE_MISMATCH` 중 하나입니다.
- `mi`는 목표값이 아니라 B가 실제로 적용 중인 값입니다. safe-off 즉시 0이 됩니다.
- `raw_valid_mask`와 `mv_valid_mask`의 bit 0~3은 CH0~CH3 current, bit 4~7은 CH0~CH3 temperature입니다.
- current ADC는 0 dB, temperature ADC는 12 dB attenuation을 사용합니다.
- 각 입력은 settling read 뒤 5개 표본 median과 EWMA 1/8을 거칩니다. 전체 8채널 scan 주기는 5 ms이고 100 ms보다 오래된 표본은 status에서 invalid 처리합니다.
- ESP-IDF curve-fitting calibration을 만들 수 없더라도 raw ADC는 계속 보고합니다. 이때 해당 `mv_valid_mask` bit는 0입니다.
- `diagnostic` 예: `BOOT`, `BOOT_ADC_UNAVAILABLE`, `INVALID_CHANNEL_SET`, `RESET_OK`, `RESET_UNSAFE`, `TARGET_BOOT_MISMATCH`, `CHALLENGE_MISMATCH`.

회로도에 current 환산 계수, NTC Beta/분압 사양, 허용 온도 및 보호 임계값이 확정되어 있지 않으므로 ADC는 현재 진단 telemetry입니다. 보정되지 않은 수치로 software over-current/over-temperature trip을 만들지 않았습니다.

## Logic Carrier 핀맵

| 채널 | `PWM_MAG` | `DIR` | `ENABLE` | `FAULT_N` | Current ADC | Temperature ADC |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CH0 | GPIO10 | GPIO11 | GPIO12 | GPIO13 | GPIO1 / ADC1_CH0 | GPIO2 / ADC1_CH1 |
| CH1 | GPIO14 | GPIO15 | GPIO16 | GPIO17 | GPIO4 / ADC1_CH3 | GPIO5 / ADC1_CH4 |
| CH2 | GPIO18 | GPIO21 | GPIO38 | GPIO39 | GPIO6 / ADC1_CH5 | GPIO7 / ADC1_CH6 |
| CH3 | GPIO40 | GPIO41 | GPIO42 | GPIO47 | GPIO8 / ADC1_CH7 | GPIO3 / ADC1_CH2 |

- `EN_GLOBAL`은 GPIO19 active-high input입니다. NC E-Stop과 외부 10 kΩ pull-down을 firmware가 우회하지 않습니다.
- `FAULT_N`은 외부 10 kΩ pull-up이 있는 active-low 입력입니다.
- ADC 여덟 개에는 Carrier의 1 kΩ/100 nF filter가 있지만 별도 clamp는 없습니다.
- GPIO3은 strapping pin이고, DevKitC-1 onboard RGB LED의 GPIO38 사용은 `ENABLE_CH2`와 충돌하므로 LED/RMT를 초기화하면 안 됩니다.
- J7의 모든 짝수 핀은 GND입니다. 각 채널은 단일 `PWM_MAG/DIR` 인터페이스이며 `PWM_A/PWM_B` 직접 출력 방식이 아닙니다.

## UART와 설정

B UART1 기본 핀은 TX=GPIO43, RX=GPIO44, 115200 8-N-1입니다. Logic Carrier에는 A↔B UART connector가 없으므로 공통 GND를 포함한 외부 harness가 필요합니다.

```text
ESP32_A GPIO39 TX -> ESP32_B GPIO44 RX
ESP32_A GPIO40 RX <- ESP32_B GPIO43 TX
GND               --- GND
```

GPIO43/44는 DevKit의 onboard USB-UART/ROM UART0와도 연결될 수 있습니다. 외부 A 신호와 bridge TX의 push-pull contention, ROM boot byte 유입 여부를 반드시 실기에서 확인해야 합니다. `sdkconfig.defaults`는 USB Serial/JTAG를 비활성화하여 GPIO19 `EN_GLOBAL` 점유를 막고, console 및 second-stage bootloader log를 끄며, GPIO 제어 함수를 IRAM에 배치합니다. ROM 자체 출력은 eFuse를 변경하지 않았으므로 보드에서 별도 확인이 필요합니다.

## 빌드와 검증

ESP-IDF 6.0.2 / ESP32-S3 빌드:

```bash
cd ESP32_B_Algo
idf.py set-target esp32s3
idf.py build
sh host_tests/run_tests.sh
```

Host test는 exact pin/ADC channel, session/challenge가 필수인 strict reset parser, correlated reset result formatting, transactional full-frame 적용, 0.95 clamp, hard safe-off, fault 우선순위, ADC filter와 timestamp wrap, status/diagnostic, 1-tick 이하 PWM 차단, 방향 blanking, ENABLE commit 거부, oversize JSONL 복구를 검사합니다.

실제 Power Stage/HV를 연결하기 전에는 다음 HIL 검증이 필요합니다.

1. 전원 인가와 reset 전 구간에서 네 `ENABLE`, `PWM_MAG`, `DIR` 파형 확인
2. 네 채널 16 kHz carrier, 60 Hz polarity, 최대 95% duty 확인
3. 방향 전환 시 ENABLE/PWM LOW blanking 시간 확인
4. 짧은 `FAULT_N`/E-Stop pulse가 latch되고 reset 전까지 재활성되지 않는지 확인
5. UART 단선/oversize/stale/replayed frame과 task watchdog safe-off 확인
6. GPIO3 cold boot, GPIO19 USB/reset glitch, GPIO38 RGB LED contention 확인
7. ADC 여덟 입력의 실제 전압 범위, current 환산, NTC 곡선과 saturation 확인
8. 동일 revision의 단일 채널 Power Stage PCB 네 장 각각에서 `RUN_OK = CH_ENABLE AND FAULT_N` 확인

최초 검증은 Power Stage와 고전압을 분리한 상태에서 logic-level 파형부터 수행하십시오.
