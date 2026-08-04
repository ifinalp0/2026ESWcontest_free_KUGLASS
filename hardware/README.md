# KUGLASS 하드웨어 기준

이 디렉터리의 회로도는 ESP32_B 펌웨어를 구현할 때의 물리적 기준입니다. ESP32_B는 독립적으로 Power Stage에 직결되는 구성이 아니라 **Logic Carrier에 장착되어** SPWM과 제어 신호를 만들고, Logic Carrier가 안전 게이팅·Fault/ADC 피드백·전원 및 Power Stage 커넥터를 제공합니다.

## 기준 회로와 적용 우선순위

1. ESP32_B의 실제 GPIO, 신호명, 극성, 커넥터와 전원 연결은 [Logic carrier.pdf](<./Logic carrier.pdf>)를 최우선 기준으로 사용합니다.
2. 이 README는 회로도를 코드 개발용으로 풀어 쓴 파생 문서입니다. PDF와 충돌하면 PDF를 따르고 README·핀맵·테스트를 함께 수정합니다.
3. [Power_stage.pdf](<./Power_stage.pdf>)는 별도의 1채널 Power Stage 회로도입니다. 현재 PDF의 J10 CH0 핀 순서와 `PWM_MAG/DIR/CH0_ENABLE/FAULT_N/ADC/+3.3V/+12V` 명칭은 Logic Carrier J7의 CH0 블록과 맞고, Power Stage U14C가 `CH0_ENABLE AND FAULT_N`으로 `RUN_OK_CH0`를 만듭니다. 실제 제작 PCB와 4채널 복제본도 같은 revision인지 확인해야 하며, 같은 디렉터리에 있다는 이유만으로 자동 호환된다고 가정하지 않습니다.

분석 기준은 2026-08-03의 2페이지 PDF이며 SHA-256은 다음과 같습니다.

```text
c6e7c129e7d5cd66f2e6cc850b9797e58e25d15bd59cb817267279b90fc0fa92  Logic carrier.pdf
```

PDF가 교체되면 해시를 갱신하고 이 문서, 루트 `README.md`, `AGENT.md`, ESP32_B 핀맵과 핀 소유권 테스트를 다시 검토합니다. PDF에는 명확한 보드 revision/title block이 보이지 않으므로 제작 PCB의 revision과 PDF가 같은지는 별도로 확인해야 합니다.

## Logic Carrier의 역할

```text
ESP32_A 명령
  -> ESP32_B DevKitC-1-N8R8 (Logic Carrier U3)
     -> PWM_MAG_CHx / DIR_CHx / ENABLE_CHx
E-Stop NC -> EN_GLOBAL ----+-> 74HC08 AND -> CHx_ENABLE
                           +-> ESP32_B GPIO19 상태 입력
Power Stage -> FAULT_N_CHx -------> ESP32_B fault 입력
Power Stage -> ADC_*_CHx_RAW -> 1 kOhm/100 nF RC -> ESP32_B ADC1
Logic Carrier J7 <--------------------------------> Power Stage
```

- ESP32-S3-DevKitC-1-N8R8 모듈을 U3에 장착하고 +5 V로 공급합니다.
- CH0~CH3의 `PWM_MAG`, `DIR`, 개별 `ENABLE`을 ESP32_B에서 생성합니다.
- NC E-Stop의 `EN_GLOBAL`과 개별 enable을 74HC08로 AND하여 Power Stage에 전달되는 `CHx_ENABLE`을 하드웨어에서 차단합니다.
- Power Stage의 active-low `FAULT_N`을 pull-up하여 ESP32_B가 읽게 합니다.
- Power Stage의 전류·온도 analog raw 신호 8개를 RC 저역통과 필터 뒤 ESP32_B ADC1에 전달합니다.
- J7 하나로 네 채널의 로직 신호, analog feedback, +3.3 V, +12 V와 GND를 Power Stage 쪽에 배포합니다.
- Carrier에는 절연 소자가 보이지 않습니다. ESP32_B, Logic Carrier와 Power Stage 저전압 인터페이스는 공통 GND를 사용합니다.
- `PWM_MAG`와 `DIR`에는 buffer, level shifter나 series resistor가 보이지 않으며 ESP32의 3.3 V GPIO가 J7까지 이어집니다. Power Stage 입력의 3.3 V 호환성, 부하, cable 길이와 noise margin을 실측합니다.

## 전원 경로

| 위치 | 핀/부품 | 회로도상 연결 | 개발·조립 시 의미 |
| --- | --- | --- | --- |
| J1 | 1 / 2 | `+24V_BAT` / GND | 24 V 입력 |
| F1 | 15 A | `+24V_BAT` 직렬 | 메인 입력 퓨즈 |
| J4 | 1 / 2 | 퓨즈 뒤 전원과 `+24V_SW` 사이 | 외부 main power switch 연결 |
| C16 / C17 | 100 uF / 100 nF | `+24V_SW`-GND | 벌크 및 고주파 decoupling |
| J3 `Boost_IN` | 1 / 2 | `+24V_SW` / GND | 회로도 명칭상 외부 변환부 입력 연결 |
| J2 | 1 / 2 | `+12V` / GND | 외부에서 생성된 12 V 입력, J7에 배포 |
| J6 `Buck_OUT` | 1 / 2 | `+5V` / GND | 외부 buck의 5 V 출력 입력, U3 5V 핀 공급 |
| C18 / C19 | 100 uF / 100 uF | `+5V`-GND | +5 V bulk decoupling |
| U3 | 3V3 pins | `+3.3V` | DevKit의 3.3 V rail을 Logic Carrier/J7에 배포 |

이 회로도에는 24 V→12 V 또는 24 V→5 V 변환 회로 자체가 없습니다. J2/J3/J6의 이름만으로 전압 방향이나 모듈 정격을 추정하지 말고 실제 외부 DC-DC 배선과 출력 전압을 무부하·부하 상태에서 확인한 뒤 U3/J7을 연결합니다. +3.3 V와 +12 V는 J7에서 채널마다 반복되지만 채널별 퓨즈나 전류 제한은 회로도에 보이지 않습니다.

Espressif는 DevKit의 USB 전원, 5V pin 전원과 3V3 pin 전원을 상호 배타적인 공급 방식으로 안내합니다. J6로 +5 V를 공급하는 동안 전원을 내보내는 USB cable을 동시에 연결하지 않습니다. flash/monitor가 필요하면 역급전이 없도록 전원 경로를 분리·계측한 방법을 먼저 정합니다.

## ESP32_B GPIO 핀맵

다음 표가 `PWM_MAG/DIR`형 Logic Carrier용 핀맵입니다. `ENABLE_CHx`는 ESP32_B가 U4에 넣는 신호이고, J7의 `CHx_ENABLE`은 `EN_GLOBAL`과 AND된 결과라는 차이를 유지해야 합니다.

| 채널 | PWM magnitude | Direction | MCU enable | Fault input | Current ADC | Temperature ADC |
| --- | --- | --- | --- | --- | --- | --- |
| CH0 | GPIO10 (`PWM_MAG_CH0`) | GPIO11 (`DIR_CH0`) | GPIO12 (`ENABLE_CH0`) | GPIO13 (`FAULT_N_CH0`) | GPIO1 / ADC1_CH0 | GPIO2 / ADC1_CH1 |
| CH1 | GPIO14 (`PWM_MAG_CH1`) | GPIO15 (`DIR_CH1`) | GPIO16 (`ENABLE_CH1`) | GPIO17 (`FAULT_N_CH1`) | GPIO4 / ADC1_CH3 | GPIO5 / ADC1_CH4 |
| CH2 | GPIO18 (`PWM_MAG_CH2`) | GPIO21 (`DIR_CH2`) | GPIO38 (`ENABLE_CH2`) | GPIO39 (`FAULT_N_CH2`) | GPIO6 / ADC1_CH5 | GPIO7 / ADC1_CH6 |
| CH3 | GPIO40 (`PWM_MAG_CH3`) | GPIO41 (`DIR_CH3`) | GPIO42 (`ENABLE_CH3`) | GPIO47 (`FAULT_N_CH3`) | GPIO8 / ADC1_CH7 | GPIO3 / ADC1_CH2 |

공통 안전 입력은 **GPIO19 `EN_GLOBAL`**입니다. GPIO19는 출력이 아니라 외부 NC E-Stop 회로의 상태를 읽는 input-only 핀으로 취급합니다.

### U3 DevKit header 위치

| 기능 | U3 header pin |
| --- | --- |
| CH0 PWM / DIR / ENABLE / FAULT_N | J1-16 / J1-17 / J1-18 / J1-19 |
| CH1 PWM / DIR / ENABLE / FAULT_N | J1-20 / J1-8 / J1-9 / J1-10 |
| CH2 PWM / DIR / ENABLE / FAULT_N | J1-11 / J3-18 / J3-10 / J3-9 |
| CH3 PWM / DIR / ENABLE / FAULT_N | J3-8 / J3-7 / J3-6 / J3-17 |
| CH0 current / temperature ADC | J3-4 / J3-5 |
| CH1 current / temperature ADC | J1-4 / J1-5 |
| CH2 current / temperature ADC | J1-6 / J1-7 |
| CH3 current / temperature ADC | J1-12 / J1-13 |
| `EN_GLOBAL` | J3-20 |

## J7 Power Stage 커넥터 핀맵

J7은 generic `Conn_02x32_Odd_Even` 심볼입니다. **모든 짝수 핀 2~64는 GND**이며, 신호와 전원은 홀수 핀에 있습니다. PDF만으로 실제 connector part, pitch, keying과 mating-face 관찰 방향은 확정할 수 없습니다. 케이블 방향이나 odd/even 열을 뒤집으면 신호를 GND에 단락할 수 있으므로 pin 1 표시와 실제 PCB 실크를 제작 문서에 고정하고 계측기로 확인합니다.

| 채널 | PWM_MAG | DIR | gated enable | FAULT_N | ADC current raw | ADC temp raw | +3.3 V | +12 V |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| CH0 | 1 | 3 | 5 | 7 | 9 | 11 | 13 | 15 |
| CH1 | 17 | 19 | 21 | 23 | 25 | 27 | 29 | 31 |
| CH2 | 33 | 35 | 37 | 39 | 41 | 43 | 45 | 47 |
| CH3 | 49 | 51 | 53 | 55 | 57 | 59 | 61 | 63 |

신호 방향과 의미는 다음과 같습니다.

| 신호 | 방향 | active level / 조건 |
| --- | --- | --- |
| `PWM_MAG_CHx` | ESP32_B → Logic Carrier J7 → Power Stage | 0~3.3 V PWM magnitude. Carrier는 complementary gate 신호나 dead time을 만들지 않음 |
| `DIR_CHx` | ESP32_B → Logic Carrier J7 → Power Stage | 0/3.3 V 극성 선택. 어느 레벨이 어느 극성인지는 Power Stage와 실측으로 확정 |
| `ENABLE_CHx` | ESP32_B → 74HC08 | HIGH=request enable |
| `CHx_ENABLE` | 74HC08 → Power Stage | HIGH only when `EN_GLOBAL AND ENABLE_CHx` |
| `FAULT_N_CHx` | Power Stage → ESP32_B | LOW=fault, HIGH=normal |
| `ADC_I_CHx_RAW` | Power Stage → RC filter → ESP32_B | analog current feedback |
| `ADC_TEMP_CHx_RAW` | Power Stage → RC filter → ESP32_B | analog temperature feedback |

## E-Stop, enable과 Fault

- J5는 `E_STOP_NC_SW_CONN`입니다. pin 1은 +3.3 V, pin 2는 `EN_GLOBAL`이며 R18 10 kOhm이 GND로 pull-down합니다. 정상 NC 접점이 닫혀 있을 때 HIGH, E-Stop 동작·단선·미장착 때 LOW가 되는 구조입니다.
- `EN_GLOBAL`은 GPIO19에서 **읽기만** 합니다. GPIO19를 output/high로 설정하거나 내부 pull-up으로 강제하면 하드웨어 E-Stop 경로를 우회하거나 외부 회로와 전기적으로 충돌할 수 있습니다.
- ESP32-S3는 power-up 중 GPIO19/20에 짧은 high-level glitch가 생길 수 있고 USB 기능도 기본 활성화됩니다. 이는 `app_main`의 GPIO 설정만으로 완전히 제거할 수 없는 구간입니다. 실제 `EN_GLOBAL` 파형과 U4 출력이 reset/power-cycle 중 안전한지 scope로 확인하고, 필요하면 다음 Carrier revision에서 GPIO 선택 또는 외부 강제 차단 구조를 보강합니다.
- U4 74HC08의 식은 모든 채널에 대해 `CHx_ENABLE = EN_GLOBAL AND ENABLE_CHx`입니다. 펌웨어의 안전 판단과 별개로 E-Stop이 LOW이면 J7 enable이 LOW가 되어야 합니다.
- J5 E-Stop은 `CHx_ENABLE`만 막고 F1/J4의 +24 V나 +12 V/+5 V rail을 끊지 않습니다. 동작 후에도 전력부는 live로 취급합니다. PWM/DIR도 U4에서 차단되지 않으므로 firmware가 동시에 PWM 0과 안전 direction을 적용해야 합니다.
- 단일 NC 접점과 74HC08에는 이중화, 강제유도 접점 feedback 또는 safety relay 기능이 없습니다. 이 회로를 safety-rated emergency power disconnect로 표현하지 않습니다.
- R19~R22가 `FAULT_N_CH0~CH3`를 각각 +3.3 V로 10 kOhm pull-up합니다. 펌웨어는 LOW를 즉시 fault로 latch하고 안전 조건을 재확인한 뒤에만 clear해야 합니다.
- `FAULT_N`은 Logic Carrier U4의 하드웨어 AND 입력이 아닙니다. 현재 `Power_stage.pdf`에는 U14C의 `RUN_OK` 차단이 있지만 실제 Power Stage revision에서 동일하게 구현되었는지 확인합니다. 그렇지 않다면 ESP32_B polling과 enable 차단 latency가 추가 보호의 전부이므로 최악 latency/jitter를 실측합니다.
- Fault 커넥터가 빠져도 pull-up 때문에 HIGH로 읽힐 수 있습니다. 따라서 `FAULT_N=HIGH`만으로 Power Stage가 연결·정상이라고 단정하지 말고 HIL에서 커넥터 연속성 또는 별도 presence 검증을 포함합니다.
- 회로도상 MCU 측 `ENABLE_CHx` 입력에는 외부 pull-down이 보이지 않습니다. 펌웨어는 가장 이른 초기화 단계에서 네 enable GPIO를 LOW로 구동해야 하며, 제작 회로에서는 부팅/리셋 중 LOW를 보장하는 외부 pull-down 추가 여부를 검토해야 합니다.

## ADC 입력 필터와 제약

2페이지의 여덟 입력은 모두 1 kOhm 직렬 저항과 100 nF GND capacitor로 구성됩니다. 시정수는 100 us, 이상적인 차단 주파수는 약 1.59 kHz입니다.

| 채널 | Current filter | Temperature filter |
| --- | --- | --- |
| CH0 | R23 / C24 | R24 / C25 |
| CH1 | R25 / C26 | R26 / C27 |
| CH2 | R27 / C28 | R28 / C29 |
| CH3 | R29 / C30 | R30 / C31 |

- Carrier에는 ADC raw 입력용 분압, clamp 또는 level shifter가 보이지 않습니다. Power Stage가 ESP32-S3 ADC 허용 범위 안의 비음수 전압을 내보내는지 먼저 계측해야 합니다.
- ADC attenuation, calibration, 기준 전압, 영점/이득, 단선·포화 판정과 sampling rate를 코드와 calibration 기록에 명시합니다.
- ADC 기능을 아직 구현하지 않은 경우에도 GPIO1~8은 모두 예약하고 UART/PWM/일반 출력으로 재사용하지 않습니다.
- CH3 temperature는 GPIO3/ADC1_CH2입니다. GPIO3은 ESP32-S3 strapping pin이므로 reset 시 외부 analog source가 boot strap level을 바꾸지 않는지 실제 보드에서 검증해야 합니다.

## ESP32_B 코드를 작성할 때의 필수 규칙

1. 위 GPIO 표를 하나의 board pinmap 정의로 유지하고 PWM, GPIO, ADC, UART 초기화가 같은 핀을 중복 소유하지 못하도록 compile-time 또는 host test를 둡니다.
2. 부팅 순서는 `ENABLE_CH0~3=LOW`와 PWM duty 0을 먼저 확정한 뒤 Fault/`EN_GLOBAL` 입력, PWM peripheral, 통신 순으로 초기화합니다. 유효한 full-frame 명령, 살아 있는 TTL, `EN_GLOBAL=HIGH`, 모든 `FAULT_N=HIGH`를 모두 만족한 뒤에만 출력합니다.
3. 차단 순서는 channel enable LOW를 먼저 적용한 뒤 PWM 0과 direction의 안전 기본값을 적용합니다. E-Stop, Fault, 잘못된 frame, watchdog 또는 heartbeat timeout은 즉시 safe-off하고 Fault 정책에 따라 latch합니다.
4. `EN_GLOBAL` GPIO19는 input-only이며 외부 R18 pull-down을 존중합니다. `FAULT_N`은 active-low이고 외부 pull-up이 이미 있으므로 핀 모드와 내부 pull 설정을 회로 의도에 맞게 명시합니다.
5. Logic Carrier 인터페이스는 채널당 **단일 `PWM_MAG` + `DIR`**입니다. `PWM_A/PWM_B` dual-PWM 설계의 핀맵을 이 회로에 적용하지 않습니다. 방향을 바꿀 때는 PWM=0 구간과 Power Stage의 dead-time/blanking 요구를 지켜야 합니다.
6. Carrier는 MOSFET complementary drive나 dead time을 만들지 않습니다. 해당 기능이 Power Stage에서 보장되는지 확인하기 전에는 고전압을 연결하지 않습니다.
7. `FAULT_N`, ADC, `EN_GLOBAL`, PWM/DIR/ENABLE 핀 중 어느 것도 UART나 debug 출력에 재사용하지 않습니다.
8. 회로도의 U3 `TX`/`RX`는 NC로 표시되고 J7에도 A↔B 통신 커넥터가 없습니다. B측 UART 배선은 현재 Logic Carrier PDF만으로 확정되지 않았습니다. GPIO43/44는 DevKit의 U0TXD/U0RXD 및 USB-to-UART bridge와 연결되므로 외부 A↔B 링크에 쓸 경우 contention과 flash/monitor 경로를 함께 설계해야 합니다.
9. GPIO19/20은 ESP32-S3 native USB/JTAG 핀이며 이 Carrier는 GPIO19를 `EN_GLOBAL`로 사용합니다. ESP32_B에서 native USB를 동시에 사용한다고 가정하지 않습니다.
10. GPIO3은 strapping pin입니다. CH3 temperature 회로가 연결된 상태의 power-on/reset boot를 HIL 항목에 포함합니다.
11. GPIO39~42는 Carrier 신호이면서 classic JTAG 기능 핀입니다. external JTAG를 이 핀에 attach하지 않습니다. N8R8의 Octal PSRAM에 쓰이는 GPIO35~37도 대체 통신 핀으로 지정하지 않습니다. DevKitC-1 v1.1은 RGB LED data를 GPIO38에 연결하므로 CH2 enable과 공유됩니다. board revision을 확인하고 RGB LED 초기화·RMT/LED 예제를 금지합니다.
12. `EN_GLOBAL`과 `FAULT_N` 입력은 한 GPIO config mask로 묶지 않습니다. 전자는 외부 pull-down, 후자는 외부 pull-up 회로이므로 방향은 모두 input이어도 pull 정책을 분리합니다.
13. 16 kHz carrier와 60 Hz fundamental은 현재 firmware/system 요구이며 Logic Carrier가 정한 값이 아닙니다. Power Stage 및 PDLC 실측 없이 회로도의 보장값처럼 취급하지 않습니다.

Espressif의 [ESP32-S3 GPIO 표](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html), [ESP32-S3-DevKitC-1 v1.1 header/revision 표](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)와 [ESP32-S3 hardware guideline](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)을 함께 확인합니다. DevKit 공식 표에서 J3-21은 GND이지만 Logic Carrier U3 심볼에는 존재하지 않는 `IO22`처럼 표시되어 있습니다. 현재 해당 핀은 NC이지만, 다음 회로 개정에서 심볼을 바로잡아야 합니다.

## 현재 저장소 구현과의 정합 상태

2026-08-04 기준 ESP32_B에 빌드·플래시할 canonical 펌웨어는 `ESP32_B_Algo/`입니다.

- `ESP32_B_Algo/main/power_stage_pinmap.h`의 `PWM_MAG/DIR/ENABLE/FAULT_N/EN_GLOBAL`과 ADC 예약 핀은 위 Logic Carrier 핀맵과 일치합니다.
- B측 UART는 TX=GPIO43, RX=GPIO44로 제어·ADC 핀과 겹치지 않으며, native USB/secondary console은 비활성화되어 있습니다.
- compile-time 중복 검사와 `ESP32_B_Algo/host_tests/test_b_core.cpp`의 exact-value test가 핀 소유권을 검증합니다.
- Logic Carrier 회로도에서 GPIO43/44는 NC이므로 A↔B 통신에는 명시적 외부 harness가 필요하며 DevKit USB-to-UART bridge와의 contention을 실기에서 확정해야 합니다.
- current/temperature ADC 8개 수집·보호, Power Stage 자체 fault 차단과 전체 HIL은 아직 완료 조건입니다.

따라서 `ESP32_B_Algo/`와 자동 핀 소유권 검사를 기준으로 사용하되, 아래 HIL 항목을 통과하기 전에는 Power Stage/PDLC/HV를 연결하지 않습니다.

## HIL 전 체크리스트

- 실제 Carrier PCB와 DevKitC revision, U3 방향, J7 pin 1/odd-even 방향과 PDF 일치; v1.1이면 GPIO38 RGB LED 비활성 확인
- J1 극성, F1 정격, J4 switch, 외부 +12 V/+5 V와 공통 GND 계측
- GPIO 표와 build artifact의 실제 pinmap 일치, 중복 소유 0건
- reset 직후 네 `CHx_ENABLE` LOW, PWM duty 0
- power-up/reset 중 GPIO19와 U4/J7 enable glitch 측정
- J5 정상/동작/단선에서 `EN_GLOBAL`과 J7 enable의 하드웨어 차단 확인
- 각 `FAULT_N` LOW 주입, latch, 출력 차단과 안전한 clear 확인
- 여덟 ADC raw의 0 V, 기준점, 최대점, saturation/단선 처리 확인
- GPIO3 ADC source 연결 상태에서 cold boot/reset 성공
- A↔B 통신 harness가 Power Stage 신호와 충돌하지 않고 flash/monitor와 contention이 없음
- Power Stage 분리 상태에서 `PWM_MAG`, `DIR`, `CHx_ENABLE`의 주파수·위상·극성 측정
- 저전압 더미 부하와 dead-time 확인을 통과한 뒤에만 Power Stage 및 PDLC/HV 연결
