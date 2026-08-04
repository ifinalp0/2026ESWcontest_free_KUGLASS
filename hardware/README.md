# KUGLASS 하드웨어 기준

이 디렉터리의 회로도는 ESP32_B 펌웨어를 구현할 때의 물리적 기준입니다. ESP32_B는 독립적으로 Power Stage에 직결되는 구성이 아니라 **Logic Carrier에 장착되어** SPWM과 제어 신호를 만듭니다. Logic Carrier의 CH0~CH3에는 동일한 단일 채널 Power Stage PCB를 한 장씩, 총 네 장 연결합니다.

## 기준 회로와 적용 우선순위

1. ESP32_B의 실제 GPIO, 신호명, 극성, 커넥터와 전원 연결은 [Logic carrier.pdf](<./Logic carrier.pdf>)를 최우선 기준으로 사용합니다.
2. 이 README는 회로도를 코드 개발용으로 풀어 쓴 파생 문서입니다. PDF와 충돌하면 PDF를 따르고 README·핀맵·테스트를 함께 수정합니다.
3. [Power_stage.pdf](<./Power_stage.pdf>)는 실제로 네 장 사용하는 동일 단일 채널 Power Stage PCB의 기준 회로도입니다. PDF의 `CH0` 표기는 한 장의 논리 채널명이며, 실제 조립에서는 같은 PCB를 Logic Carrier J7의 CH0, CH1, CH2, CH3 블록에 한 장씩 연결합니다. 각 보드의 J10 핀 기능은 해당 J7 채널 블록의 `PWM_MAG/DIR/CHx_ENABLE/FAULT_N/ADC/+3.3V/+12V`에 대응하고, 각 보드 U14C가 `CHx_ENABLE AND FAULT_N` 의미의 `RUN_OK`를 만듭니다.

분석 기준은 2026-08-03의 2페이지 PDF이며 SHA-256은 다음과 같습니다.

```text
c6e7c129e7d5cd66f2e6cc850b9797e58e25d15bd59cb817267279b90fc0fa92  Logic carrier.pdf
```

PDF가 교체되면 해시를 갱신하고 이 문서, 루트 `README.md`, `AGENT.md`, ESP32_B 핀맵과 핀 소유권 테스트를 다시 검토합니다. PDF에는 명확한 보드 revision/title block이 보이지 않으므로 제작 PCB의 revision과 PDF가 같은지는 별도로 확인해야 합니다.

현재 `hardware/`에는 위 PDF 두 개와 이 README만 있으며 편집 가능한 KiCad/Altium schematic·PCB, BOM, netlist, Gerber 또는 placement 파일은 없습니다. 따라서 아래의 "다음 revision 요구사항"은 현재 PDF에 이미 반영되었다는 뜻이 아닙니다. 실제 회로 변경은 원본 설계와 실제 PCB revision을 확보한 뒤 수행하고, 새 schematic PDF·BOM·제작 출력과 revision 식별자를 함께 저장소에 넣어 검토해야 합니다. 그 전까지는 위 PDF가 현재 핀맵과 연결의 권위 있는 기준입니다.

## Logic Carrier의 역할

```text
ESP32_A 명령
  -> ESP32_B DevKitC-1-N8R8 (Logic Carrier U3)
     -> PWM_MAG_CHx / DIR_CHx / ENABLE_CHx
E-Stop NC -> EN_GLOBAL ----+-> 74HC08 AND -> CHx_ENABLE
                           +-> ESP32_B GPIO19 상태 입력
단일 채널 Power Stage PCB x4 -> FAULT_N_CHx -------> ESP32_B fault 입력
단일 채널 Power Stage PCB x4 -> ADC_*_CHx_RAW -> 1 kOhm/100 nF RC -> ESP32_B ADC1
Logic Carrier J7 <---------------------------> CH0~CH3 Power Stage PCB 각 1장
```

- ESP32-S3-DevKitC-1-N8R8 모듈을 U3에 장착하고 +5 V로 공급합니다.
- CH0~CH3의 `PWM_MAG`, `DIR`, 개별 `ENABLE`을 ESP32_B에서 생성합니다.
- NC E-Stop의 `EN_GLOBAL`과 개별 enable을 74HC08로 AND하여 Power Stage에 전달되는 `CHx_ENABLE`을 하드웨어에서 차단합니다.
- 단일 채널 Power Stage PCB 네 장의 active-low `FAULT_N`을 각각 pull-up하여 ESP32_B가 읽게 합니다.
- 네 Power Stage PCB의 전류·온도 analog raw 신호 8개를 RC 저역통과 필터 뒤 ESP32_B ADC1에 전달합니다.
- J7의 CH0~CH3 채널 블록은 동일한 단일 채널 Power Stage PCB 한 장씩에 로직 신호, analog feedback, +3.3 V, +12 V와 GND를 배포합니다.
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

## J7과 단일 채널 Power Stage PCB 4장 핀맵

J7은 generic `Conn_02x32_Odd_Even` 심볼이며 CH0~CH3의 네 블록으로 나뉩니다. 각 블록은 동일한 단일 채널 Power Stage PCB 한 장에 연결됩니다. **모든 짝수 핀 2~64는 GND**이며, 신호와 전원은 홀수 핀에 있습니다. PDF만으로 실제 connector part, pitch, keying과 mating-face 관찰 방향은 확정할 수 없습니다. 케이블 방향이나 odd/even 열을 뒤집으면 신호를 GND에 단락할 수 있으므로 pin 1 표시와 실제 PCB 실크를 제작 문서에 고정하고 계측기로 확인합니다.

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
- `FAULT_N`은 Logic Carrier U4의 하드웨어 AND 입력이 아닙니다. `Power_stage.pdf`의 U14C는 `RUN_OK`를 차단하며, CH0~CH3에 연결한 동일 단일 채널 PCB 네 장에서 이 회로가 같은 revision으로 구현됐는지 각각 확인합니다. 그렇지 않은 보드에서는 ESP32_B polling과 enable 차단 latency가 추가 보호의 전부이므로 최악 latency/jitter를 실측합니다.
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
- ESP32_B canonical firmware는 현재 ADC1 여덟 입력의 raw/mV acquisition, median-5/EWMA filtering과 validity telemetry를 구현했습니다. 그러나 mV를 전류 A·온도 °C로 바꾸는 board-specific 환산계수, 단선·포화 판정과 보호 임계값은 아직 확정되지 않았습니다. GPIO1~8은 계속 ADC 전용으로 예약하며 UART/PWM/일반 출력으로 재사용하지 않습니다.
- CH3 temperature는 GPIO3/ADC1_CH2입니다. GPIO3은 ESP32-S3 strapping pin이므로 reset 시 외부 analog source가 boot strap level을 바꾸지 않는지 실제 보드에서 검증해야 합니다.

## 다음 Logic Carrier/Power Stage revision 요구사항

이 절은 저장소 문서와 firmware만으로 끝낼 수 있는 항목이 아니라 schematic·PCB 수정, 제작 및 실측이 필요한 항목입니다. 현재 PDF를 직접 편집한 것으로 간주하지 않습니다.

### A↔B UART connector와 bridge 격리

현재 firmware 계약은 `ESP32_A GPIO39 TX -> ESP32_B GPIO44 RX`, `ESP32_A GPIO40 RX <- ESP32_B GPIO43 TX`, 공통 GND, 115200 8-N-1입니다. Logic Carrier U3의 GPIO43/44는 NC이고 J7에도 통신 핀이 없으므로 다음 Carrier revision에는 별도의 keyed 3-contact connector를 둡니다.

제작 문서에는 mating-face 기준과 pin 1 표시를 포함하여 다음 논리 핀을 고정합니다. connector part·pitch·locking 방식도 BOM에 기록합니다.

| 권장 contact | 신호 | 연결 |
| ---: | --- | --- |
| 1 | GND | ESP32_A GND ↔ ESP32_B/Carrier GND |
| 2 | `A_TX/B_RX` | ESP32_A GPIO39 → ESP32_B GPIO44 |
| 3 | `B_TX/A_RX` | ESP32_B GPIO43 → ESP32_A GPIO40 |

- 이 connector로 +3.3 V/+5 V를 전달하지 않습니다. 두 보드의 전원을 묶어야 하는 별도 요구가 생기면 역급전, ground loop와 power sequencing을 포함한 전원 설계로 취급합니다.
- ESP32_B GPIO44에 DevKit USB-to-UART bridge TX와 ESP32_A TX가 동시에 push-pull로 연결되는 상태를 허용하지 않습니다. board revision에 맞는 0 Ω link/jumper, tri-state buffer 또는 검증된 alternate GPIO routing 중 하나로 **항상 단일 송신기만** B RX를 구동하게 합니다. 단순 series resistor만으로 두 출력의 동시 구동을 정상 상태로 인정하지 않습니다.
- GPIO43/44를 유지하면 flash/monitor용 bridge 연결 상태와 A↔B 운전 상태를 silk와 조립 절차에서 명확히 구분합니다. alternate GPIO를 선택한다면 N8R8 flash/PSRAM, strapping, JTAG, RGB LED와 기존 Carrier 핀 소유권을 다시 전부 검사한 뒤 PDF·pinmap·test를 함께 바꿉니다.
- harness acceptance test는 TX/RX/GND continuity, 역삽입 방지, A/B 각각 unpowered 상태의 back-power, bridge 연결/분리 상태의 contention, reset 때 ROM byte 유입과 115200 장시간 오류율을 포함합니다.

### U4 enable 기본 LOW와 GPIO19 안전 net 분리

현재 U4의 MCU측 `ENABLE_CH0~3` 입력에는 외부 pull-down이 보이지 않습니다. Power Stage의 R15/R16/R17 10 kOhm pull-down은 U4 이후의 `CH0_ENABLE/PWM_MAG/DIR` 입력에 있으므로 U4 입력의 boot/reset 부동을 해결하지 못합니다.

- 다음 Carrier revision에서는 U4의 네 MCU enable 입력 각각에 외부 pull-down을 추가해 ESP32가 reset, unpowered 또는 high-impedance일 때 LOW를 보장합니다. 저항값은 ESP32 output drive/leakage, U4 VIH/VIL, 전력과 측정된 boot waveform을 근거로 schematic calculation에 기록합니다.
- J5 NC 접점이 만드는 하드웨어 안전 신호를 `EN_GLOBAL_SAFE`, MCU가 읽는 신호를 `EN_GLOBAL_SENSE`처럼 분리합니다. `EN_GLOBAL_SAFE`는 ESP32 pin 상태와 무관하게 U4 게이트를 직접 차단해야 하며, MCU sense는 단방향 buffer/isolator와 필요한 current limiting을 거쳐야 합니다.
- GPIO19를 계속 sense에 사용한다면 power-on glitch 또는 잘못된 output configuration이 `EN_GLOBAL_SAFE`를 HIGH로 구동할 수 없음을 회로적으로 증명합니다. GPIO를 바꾸는 경우에도 위 UART와 마찬가지로 모든 pin ownership과 strapping 제약을 다시 검증합니다.
- 전원 안정 전과 ESP32 reset 중에 출력 enable을 강제로 LOW로 유지해야 한다면 supervisor/power-good가 포함된 별도 `RESET_RELEASED` gate를 사용합니다. 안전식은 최소한 `CHx_ENABLE = EN_GLOBAL_SAFE AND MCU_ENABLE_CHx AND RESET_RELEASED`의 의미를 만족해야 합니다.
- scope acceptance test는 J5 정상/동작/단선, ESP32 미장착, 5 V/3.3 V 상승·하강, brownout, EN/reset 반복, GPIO19 glitch와 각 MCU enable의 high-impedance 조건을 포함하며 모든 경우 J7 `CHx_ENABLE`이 LOW인지 확인합니다.

### ADC clamp, 사양과 물리 단위 환산

Carrier의 1 kOhm/100 nF RC는 anti-alias/noise filter이지 과전압 보호 회로가 아닙니다. 다음 revision에서는 R23~R30 뒤의 ESP32 ADC node를 보호하되, clamp leakage가 current/NTC 정확도를 훼손하거나 Power Stage가 켜지고 ESP32가 꺼진 상태에서 3.3 V rail을 역급전하지 않도록 설계합니다. rail clamp, 전용 low-leakage protection device, 추가 current limiting 또는 power-off isolation 중 실제 source 조건에 맞는 topology를 선정하고 계산·시험 결과를 남깁니다.

회로와 calibration 자료에는 채널별로 최소 다음 값을 제공해야 합니다.

- 정상·fault·connector open·Power Stage off 때의 raw 최소/최대 전압과 source impedance
- 허용 가능한 양/음 transient, clamp 전류, ADC pin absolute maximum과 power-off injection 조건
- current sense의 shunt 값, amplifier/transfer gain, zero offset, polarity와 `A/V` 또는 `mV/A` 계수
- NTC의 pull-up 전압·저항, `R25`, beta 또는 Steinhart-Hart 계수, 허용오차와 사용 온도 범위
- TLV1701 current limit reference와 실제 trip current, `FAULT_N` assert/deassert 조건
- board serial/revision별 calibration point, 온도와 사용 계측기

Current 변환은 확정된 계수에 대해 `I=(V_ADC-V_ZERO)/K_I` 형태로 정의합니다. 현재 Power Stage 그림처럼 NTC가 pull-up과 GND 사이에 있다면 `R_NTC=R_PULLUP*V_ADC/(V_PULLUP-V_ADC)`를 먼저 계산한 뒤 확정된 beta/Steinhart-Hart 식으로 °C를 구합니다. 이 계수와 분모 경계가 확정되기 전에는 raw/mV telemetry를 보호 판단용 A/°C라고 표기하지 않습니다.

ADC HIL은 각 채널의 0 V, 정상 기준점, 최대 정상점, clamp 직전/이후, connector open/short-to-GND/short-to-3.3 V, Power Stage만 powered, ESP32만 powered, GPIO3 source 연결 cold boot를 포함합니다. 환산 오차와 보호 반응을 channel/revision별 기록으로 남긴 뒤에만 firmware 임계값을 활성화합니다.

### 동일 단일 채널 Power Stage PCB 네 장의 제작 구성

실제 구성은 통합 4채널 Power Stage PCB 한 장이 아니라, `Power_stage.pdf`를 기준으로 제작한 동일 단일 채널 PCB 네 장입니다. 네 보드를 Logic Carrier J7의 CH0~CH3 블록에 한 장씩 연결합니다. PDF와 보드 내부의 `CH0` 명칭은 단일 채널 기준 명칭이며, 조립된 시스템에서는 연결된 J7 블록에 따라 CH0, CH1, CH2, CH3 역할을 맡습니다.

네 장 구성을 승인하려면 다음 자료와 검증이 필요합니다.

- revision/date가 있는 단일 채널 editable schematic와 PCB layout
- 동일 BOM, Gerber, drill, placement, stack-up/copper weight와 assembly drawing으로 제작됐음을 식별할 수 있는 네 보드의 revision/serial
- J7의 CH0~CH3 각 블록과 각 보드 J10 사이의 harness mapping, mating-face와 pin 1 표시
- 네 보드 각각의 U14 logic, IRS2104 bootstrap/dead-time, R9 shunt/TLV1701 fault, NTC와 input pull-down 값 일치 확인
- +72 V/+12 V/+3.3 V 부하 예산, 공통 GND/PGND 결합점, creepage/clearance, thermal/current 검토
- 각 제작 PCB의 silkscreen revision, 실물 사진과 continuity/low-voltage bring-up 결과

네 PCB의 connector pinout, direction polarity, `RUN_OK`, current/temperature transfer와 fault threshold가 모두 일치하는지 확인합니다. 채널별 차이는 Logic Carrier J7 연결 위치뿐이어야 합니다.

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

## A↔B reset/status session 계약

Fault reset은 출력 enable과 직접 연결되는 제어이므로 단순한 `seq` 재사용만으로 허용하지 않습니다. A와 B의 reset/reboot 뒤 지연된 UART frame이 현재 보드의 fault를 해제하지 못하도록 다음 wire contract를 사용합니다.

- B는 부팅마다 nonzero u32 `boot_id`와 nonzero u32 `reset_challenge`를 새로 만들고 regular status와 control-result status 모두에 넣습니다.
- A는 부팅마다 nonzero u32 `source_session_id`를 새로 만들며, reset request의 `seq`는 u32입니다.
- reset request의 top-level known fields는 `v,type,seq,source_session_id,target_boot_id,reset_challenge,command`입니다. `target_boot_id`와 `reset_challenge`는 A가 가장 최근의 B status에서 받은 값을 그대로 사용합니다.
- B는 `target_boot_id`, `reset_challenge`, 실제 `EN_GLOBAL/FAULT_N` 안전 상태를 모두 검증하기 전에는 fault를 clear하지 않습니다. matching challenge는 물리 상태가 unsafe여서 reset이 실패해도 즉시 소비하고 새 challenge로 교체하여, 같은 request가 나중에 재생되어 clear되지 않게 합니다.
- B의 결과는 status의 `control_result` object로 보내며 fields는 `command,seq,source_session_id,ok,error`입니다. A는 현재 `boot_id`, 자신이 보낸 `source_session_id`와 request `seq`가 모두 맞는 결과만 수락합니다. status의 top-level `seq`는 독립적인 B status sequence이며 control request correlation에 사용하지 않습니다.
- 모든 ID와 challenge는 0을 금지합니다. `error`는 `NONE`, `RESET_UNSAFE`, `TARGET_BOOT_MISMATCH`, `CHALLENGE_MISMATCH` 중 하나입니다. `ok=true`는 `error=NONE`일 때만 허용합니다.
- 성공적인 reset 뒤에도 출력은 off를 유지하고, 현재 B boot를 대상으로 한 새 full actuator frame과 살아 있는 TTL을 받아야만 다시 enable합니다.
- 이 challenge는 accidental stale/replayed frame을 막는 freshness 장치이지 암호학적 인증이 아닙니다. 공격자에 대한 인증이 필요하면 별도의 authenticated transport와 key provisioning을 설계합니다.

아래 JSON은 reset에 관계된 필드만 보여 주는 schema excerpt이며, 그대로 송신하는 완전한 B status frame은 아닙니다. 실제 status에는 `controller_id`, `estop`, `fault_code`, CH0~CH3 full set과 ADC block도 포함됩니다. 숫자는 설명용이며 실제 challenge는 직전 값에서 추측해 증가시키지 않습니다.

```json
{"v":1,"type":"status","seq":42,"boot_id":1234,"reset_challenge":5678}
{"v":1,"type":"control","seq":9001,"source_session_id":2468,"target_boot_id":1234,"reset_challenge":5678,"command":"reset_fault"}
{"v":1,"type":"status","seq":43,"boot_id":1234,"reset_challenge":9876,"control_result":{"command":"reset_fault","seq":9001,"source_session_id":2468,"ok":true,"error":"NONE"}}
```

Espressif의 [ESP32-S3 GPIO 표](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html), [ESP32-S3-DevKitC-1 v1.1 header/revision 표](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)와 [ESP32-S3 hardware guideline](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)을 함께 확인합니다. DevKit 공식 표에서 J3-21은 GND이지만 Logic Carrier U3 심볼에는 존재하지 않는 `IO22`처럼 표시되어 있습니다. 현재 해당 핀은 NC이지만, 다음 회로 개정에서 심볼을 바로잡아야 합니다.

## 현재 저장소 구현과의 정합 상태

2026-08-04 기준 ESP32_B에 빌드·플래시할 canonical 펌웨어는 `ESP32_B_Algo/`입니다.

- `ESP32_B_Algo/main/power_stage_pinmap.h`의 `PWM_MAG/DIR/ENABLE/FAULT_N/EN_GLOBAL`과 ADC 예약 핀은 위 Logic Carrier 핀맵과 일치합니다.
- B측 UART는 TX=GPIO43, RX=GPIO44로 제어·ADC 핀과 겹치지 않으며, native USB/secondary console은 비활성화되어 있습니다.
- compile-time 중복 검사와 `ESP32_B_Algo/host_tests/test_b_core.cpp`의 exact-value test가 핀 소유권을 검증합니다.
- Logic Carrier 회로도에서 GPIO43/44는 NC이므로 A↔B 통신에는 명시적 외부 harness가 필요하며 DevKit USB-to-UART bridge와의 contention을 실기에서 확정해야 합니다.
- current/temperature ADC 8개 raw/mV acquisition과 telemetry는 구현됐습니다. A/°C 물리 단위 환산, clamp·단선·포화 보호, Power Stage 자체 fault 차단과 전체 HIL은 아직 완료 조건입니다.

따라서 `ESP32_B_Algo/`와 자동 핀 소유권 검사를 기준으로 사용하되, 아래 HIL 항목을 통과하기 전에는 Power Stage/PDLC/HV를 연결하지 않습니다.

## Bring-up/HIL 체크리스트

각 결과에는 날짜, Carrier/Power Stage/DevKit revision, board serial, firmware commit, 계측기와 측정 파일을 기록합니다. 한 단계가 실패하면 이후 전력 단계를 진행하지 않습니다.

### 1. 무전원 조립·연속성

- 실제 Carrier PCB와 DevKitC revision, U3 방향, PDF의 J7 pin 1/odd-even 방향이 일치하며 모든 even pin이 GND인지 확인
- J1 극성, F1 정격, J4 switch, 외부 +12 V/+5 V connector와 공통 GND의 단락·역삽입 확인
- keyed UART harness의 pin 1/GND, 두 교차 signal continuity와 인접 핀 short 0건 확인
- flash/monitor bridge와 A TX가 B GPIO44를 동시에 구동할 수 없도록 정해진 link/jumper/buffer 상태 확인
- GPIO 표와 build artifact의 실제 pinmap 일치, 중복 소유 0건; v1.1이면 GPIO38 RGB LED 비활성 확인

### 2. Logic Carrier 단독 저전압

- Power Stage/J7/HV를 분리하고 전류 제한 전원으로 +5 V/3.3 V rail과 소비전류 확인
- ESP32_B 미장착, boot, reset, brownout과 power-down 전체에서 네 U4/J7 `CHx_ENABLE` LOW 확인
- 네 MCU enable 입력 pull-down과 `EN_GLOBAL_SAFE`/sense isolation을 실측하고 GPIO19 glitch가 U4 enable을 만들지 않는지 scope로 확인
- J5 정상/동작/단선에서 `EN_GLOBAL`과 네 J7 enable의 하드웨어 차단 확인
- reset 직후 네 PWM duty 0, safe direction과 enable LOW 확인

### 3. A↔B link와 session/reset

- A/B 각각 powered/unpowered, bridge connected/isolated 상태에서 back-power와 push-pull contention이 없고 115200 장시간 송수신 오류가 없는지 확인
- B power-cycle마다 nonzero `boot_id`와 `reset_challenge`가 새로 바뀌며 A가 이전 B status/session을 폐기하는지 확인
- A reboot마다 nonzero `source_session_id`가 바뀌고, 다른 session의 늦은 `control_result`를 수락하지 않는지 확인
- 이전 `target_boot_id` request는 `TARGET_BOOT_MISMATCH`, 잘못되거나 소비된 challenge는 `CHALLENGE_MISMATCH`이며 fault가 clear되지 않는지 확인
- matching challenge라도 J5 또는 `FAULT_N`이 unsafe이면 `RESET_UNSAFE`, 출력 off, 새 challenge 발행인지 확인
- 안전 상태에서 matching reset은 같은 request `seq/source_session_id`의 `control_result`, `ok=true/error=NONE`을 반환하고, 동일 frame replay는 실패하는지 확인
- reset 성공 뒤 새 full actuator frame 전에는 enable되지 않으며 UART 단선, oversize, stale/replayed actuator frame과 task watchdog에서 safe-off하는지 확인

### 4. Fault와 ADC 주입

- 각 `FAULT_N`에 짧은 LOW pulse와 지속 LOW를 주입해 ISR latch, enable/PWM 차단과 안전한 challenge reset 확인
- ADC 여덟 입력의 0 V, calibration points, 최대 정상점, clamp 경계, saturation와 open/short 처리 확인
- Power Stage만 powered/ESP32만 powered 조건에서 ADC pin과 3.3 V rail 역급전이 없는지 확인
- 확정 계수로 channel별 current A와 temperature °C 오차를 기록하고 protection threshold 양쪽에서 fault 반응 확인
- GPIO3 temperature source 연결 상태에서 cold boot/reset을 반복해 strapping 실패가 없는지 확인

### 5. 단일 채널 Power Stage PCB 네 장의 단계적 연결

- 네 Power Stage PCB가 동일한 승인 schematic/BOM/Gerber revision인지 확인하고, J7의 CH0~CH3 블록과 각 보드 J10 mapping 및 `RUN_OK = CH_ENABLE AND FAULT_N` 의미가 일치하는지 확인
- 한 번에 PCB 한 장씩 해당 채널에 logic-only로 연결해 `PWM_MAG`, `DIR`, `CHx_ENABLE`, `FAULT_N`, ADC 방향·극성 확인
- 저전압 전류 제한 공급과 더미 부하에서 16 kHz carrier, 60 Hz polarity, 최대 duty, direction blanking, IRS2104 dead-time/bootstrap과 thermal 상태 확인
- 네 채널 동시 저전압 시험에서 rail droop, ground bounce, cross-channel fault/ADC coupling과 connector 온도 확인
- 위 결과를 모두 승인한 뒤에만 PDLC를 연결하고, 별도 HV 안전 절차와 enclosure/interlock 아래에서 +72 V 시험 진행
