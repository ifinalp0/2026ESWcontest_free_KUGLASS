# Power Stage as-built 기준

이 폴더는 현재 제작된 단일 채널 Power Stage의 기준 자료입니다. 동일한 보드 네 장을 Logic Carrier J7의 CH0~CH3에 한 장씩 연결합니다. 회로와 PCB 내부의 `CH0`은 단일 보드 내부 이름이며 시스템 채널은 연결된 J7 블록이 결정합니다.

## 원본의 역할

| 파일 | 역할 |
| --- | --- |
| `power stage_.kicad_sch` | editable as-built schematic |
| `power stage_.kicad_pcb` | editable as-built PCB layout |
| `power stage_.kicad_pro` | KiCad project 설정 |
| `power stage_.kicad_dru` | 저장된 custom design rule |
| `Power_stage.pdf` | 1페이지 schematic release view |
| `datasheets/` | U1/U2, U5, U13, U14 부품 자료 |

회로 연결과 명목 계산값의 기계 판독 기준은 [`../contracts/power_stage.json`](../contracts/power_stage.json)입니다.

## 회로 구성

- J8: `+72V_HV` DC bus 입력
- Q1-Q4: H-Bridge MOSFET
- U1/U2: IRS2104 half-bridge driver, +12 V 공급
- U13: 74LVC04 direction inversion
- U14: 74LVC08 PWM routing과 `RUN_OK`
- L1/L2 470 uH와 C10 470 nF: differential output filter
- J9: filtered `PDLC_A/PDLC_B` 출력
- R9 0.1 ohm: PGND와 logic GND 사이 current shunt
- U5 TLV1701: open-collector `FAULT_N_CH0`
- R14 10 kohm과 TH1 10 kohm NTC: temperature feedback divider

논리식은 다음과 같습니다.

```text
DIR_N_CH0    = NOT DIR_CH0
PWM_LEFT_CH0 = PWM_MAG_CH0 AND DIR_CH0
PWM_RIGHT_CH0 = PWM_MAG_CH0 AND DIR_N_CH0
RUN_OK_CH0   = CH0_ENABLE AND FAULT_N_CH0
```

`RUN_OK_CH0`가 두 IRS2104의 active-low shutdown을 구동합니다. Power Stage의 fault LOW는 펌웨어 polling과 별개로 양쪽 driver를 shutdown합니다.

## J10 인터페이스

| 홀수 핀 | 신호 | 짝수 핀 |
| ---: | --- | --- |
| 1 | `PWM_MAG_CH0` | 2 GND |
| 3 | `DIR_CH0` | 4 GND |
| 5 | `CH0_ENABLE` | 6 GND |
| 7 | `FAULT_N_CH0` | 8 GND |
| 9 | `ADC_I_CH0_RAW` | 10 GND |
| 11 | `ADC_TEMP_CH0_RAW` | 12 GND |
| 13 | +3.3 V | 14 GND |
| 15 | +12 V | 16 GND |

J10 홀수 핀의 상대 위치는 Logic Carrier J7 각 채널 block의 홀수 핀과 같습니다.

## 명목 feedback 값과 사용 범위

- Current: R9 0.1 ohm이므로 이상적인 DC shunt 값은 0.1 V/A입니다. R10 10 kohm/C8 100 nF를 거쳐 raw ADC로 전달됩니다.
- Comparator: R12 33 kohm/R13 1 kohm의 명목 기준은 약 97.1 mV이고, R9 값으로 나눈 명목 trip은 약 0.971 A입니다.
- Temperature: R14 10 kohm과 TH1 10 kohm@25 °C의 명목 분압은 25 °C에서 약 1.65 V입니다.

이 값들은 schematic-derived 명목값입니다. 부품 허용오차, 장착 부품, 배선, ADC 오차와 온도 특성이 포함된 실측 calibration이 아니므로 현재 firmware는 raw/mV만 진단값으로 제공합니다. NTC beta/Steinhart-Hart 계수도 현재 자료에 없습니다.

## 현재 자료의 충돌과 경계

- Q1-Q4 schematic value는 `STP40NF20`이지만 각 Datasheet 속성은 IRF540N URL입니다. 실제 장착 marking 확인 전에는 MOSFET 정격을 어느 쪽으로도 단정하지 않습니다.
- PCB 파일은 2-layer, 1.6 mm, layer당 0.035 mm copper로 설정되어 있습니다. schematic note의 `2 oz copper preferred`는 실제 제작 copper weight 기록이 아닙니다.
- schematic에는 72 V/switching node 주변 1.5 mm clearance 지침이 있습니다. 이를 인증된 절연 등급으로 해석하지 않습니다.
- custom symbol/footprint library table이 없어 KiCad ERC에서 library resolution warning이 발생합니다. symbol과 footprint 데이터는 현재 schematic/PCB에 embed되어 있습니다.
- `kicad-cli pcb drc`는 현재 저장된 PCB/DRU 조합에서 정상 보고서 대신 tool crash로 종료됩니다. DRC 통과로 표시하지 않습니다.
- Gerber, drill, placement, 조립 BOM, 네 보드의 serial/revision 대응표는 저장소에 없습니다.

물리적으로 제작된 보드라는 사실과 EDA 자동 검사 상태는 별개입니다. 소프트웨어 작업에서는 이 회로를 현재 하드웨어로 존중하고, 확인되지 않은 제조값이나 실측값을 추가로 가정하지 않습니다.
