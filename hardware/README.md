# KUGLASS 하드웨어 기준

이 디렉터리는 KUGLASS에 실제로 제작되어 사용되는 Logic Carrier 1장과 단일 채널 Power Stage 4장의 as-built 기준입니다. 펌웨어와 상위 문서는 이 하드웨어에 맞춰야 하며, 명시적인 하드웨어 설계 작업이 아닌 경우 KiCad 원본이나 핀맵을 변경하지 않습니다.

## AI와 개발자가 읽는 순서

1. [`manifest.json`](manifest.json): 제작 상태, 파일 역할, 기준 우선순위와 자료 한계
2. [`contracts/esp32_b_io.json`](contracts/esp32_b_io.json): ESP32_B GPIO, U3, J7, J10 매핑
3. [`contracts/power_stage.json`](contracts/power_stage.json): Power Stage 논리식, 전력부, feedback의 명목값
4. [`contracts/safety.json`](contracts/safety.json): 코드와 시험에서 위반하면 안 되는 조건
5. 작업 대상 보드의 README와 KiCad/PDF 원본
6. [`validation/README.md`](validation/README.md): 실기 검증 절차와 기록 형식

구조화 JSON은 빠른 탐색과 자동 검사에 쓰는 파생 계약입니다. 회로 원본과 충돌하면 추측해서 고치지 말고 작업을 중단한 뒤 원본과 실물을 대조합니다.

## 현재 물리 구성

```text
ESP32_B DevKitC-1-N8R8
  -> Logic Carrier U3
     -> PWM_MAG_CHx / DIR_CHx / ENABLE_CHx
     -> U4: CHx_ENABLE = EN_GLOBAL AND ENABLE_CHx
     -> J7 CH0~CH3
        -> 동일한 단일 채널 Power Stage PCB 4장
           -> 각 보드의 H-Bridge + LC filter
           -> PDLC CH0~CH3
```

- Logic Carrier와 Power Stage는 모두 제작 완료된 현재 하드웨어입니다.
- Power Stage 설계의 내부 신호명은 `CH0`이지만 동일 보드를 네 장 사용합니다. 실제 채널은 연결된 Logic Carrier J7 블록이 결정합니다.
- Logic Carrier의 J7은 2x32이고 모든 짝수 핀은 GND입니다.
- Power Stage의 J10은 2x8이고 모든 짝수 핀은 GND입니다. J10의 홀수 핀 1~15가 J7 각 채널 블록의 홀수 핀에 순서대로 대응합니다.
- ESP32_B를 Power Stage에 임의 직결하거나 Logic Carrier를 생략하지 않습니다.

### ESP32_A↔ESP32_B 외부 UART

Logic Carrier의 U3 TX(GPIO43)/RX(GPIO44)는 NC이고 J7에도 A↔B UART가 없습니다.
따라서 두 DevKit은 다음 외부 3선 harness로 직접 연결합니다.

```text
ESP32_A GPIO39 TX -> ESP32_B GPIO44 RX
ESP32_A GPIO40 RX <- ESP32_B GPIO43 TX
ESP32_A GND       --- ESP32_B GND
```

UART1은 115200 8-N-1, flow control 없음입니다. TX끼리 또는 RX끼리 연결하지
않으며, ESP32_B GPIO43/44와 DevKit USB-UART bridge의 contention을 실기에서
확인합니다.

## 파일 구조

```text
hardware/
├── README.md
├── manifest.json
├── SHA256SUMS
├── contracts/
│   ├── esp32_b_io.json
│   ├── power_stage.json
│   └── safety.json
├── Logic_carrier/
│   ├── README.md
│   ├── Logic carrier.kicad_sch
│   ├── Logic carrier.pdf
│   └── datasheets/
├── Power_stage/
│   ├── README.md
│   ├── power stage_.kicad_pro
│   ├── power stage_.kicad_sch
│   ├── power stage_.kicad_pcb
│   ├── power stage_.kicad_dru
│   ├── Power_stage.pdf
│   └── datasheets/
├── validation/
│   ├── README.md
│   └── current-status.json
└── tools/
    └── validate_hardware_contract.py
```

## 정보 상태의 의미

| 상태 | 의미 |
| --- | --- |
| `as_built` | 저장소 소유자가 현재 제작 하드웨어라고 확정한 정보 |
| `schematic` | 회로도/netlist에서 직접 확인한 정보 |
| `pcb` | PCB 원본에서 직접 확인한 정보 |
| `derived` | 부품값과 연결로 계산한 명목값 |
| `measured` | 보드, 시리얼, 조건과 계측기가 기록된 실측값 |
| `unknown` | 현재 자료로 확정할 수 없는 값 |

`derived` 값은 `measured` 값이 아닙니다. 전류 A, 온도 °C, fault 임계값, 열 및 절연 성능을 명목 계산만으로 실측 완료라고 표시하지 않습니다.

## 자동 정합성 검사

다음 명령은 원본 파일 해시, JSON 문법, GPIO 중복, J7/J10 매핑, ESP32_B C++ 핀맵과 ADC descriptor의 일치를 검사합니다.

```bash
python3 hardware/tools/validate_hardware_contract.py
```

회로 원본이 의도적으로 바뀐 경우에는 원본, `SHA256SUMS`, 구조화 계약, 보드 README, 펌웨어 핀맵과 테스트를 한 작업에서 함께 검토해야 합니다.

## 현재 자료의 경계

- Logic Carrier의 배포 PDF는 2페이지 전체 회로를 포함하지만, KiCad main schematic이 참조하는 `untitled.kicad_sch` ADC 하위 시트 파일은 저장소에 없습니다. ADC 필터의 as-built 기준은 PDF 2페이지입니다.
- Logic Carrier PCB layout, Gerber, drill, placement와 조립 BOM은 저장소에 없습니다.
- Power Stage에는 schematic과 PCB 원본이 있으나 custom library table과 fabrication 출력, 조립 BOM, 보드별 serial/revision 기록은 없습니다.
- Power Stage schematic의 Q1~Q4 값은 `STP40NF20`이지만 Datasheet 속성은 IRF540N URL입니다. 실제 장착 MOSFET의 marking을 확인하기 전에는 해당 URL을 부품 확정 근거로 사용하지 않습니다.
- KiCad 자동 검사 상태는 [`validation/current-status.json`](validation/current-status.json)에 기록합니다. 제작 완료 사실과 EDA 검사 통과 여부는 별개의 상태입니다.

## 하드웨어 작업 원칙

- 펌웨어 작업은 구조화 계약과 원본 회로를 읽고 현재 제작 보드에 맞춥니다.
- 하드웨어 변경을 명시적으로 요청받지 않은 작업에서는 KiCad/PDF를 수정하거나 가상의 차기 회로를 현재 상태에 섞지 않습니다.
- 위 A-B 3선 UART harness, 외부 DC-DC, E-Stop switch, PDLC와 전원 배선은 회로도 밖의 조립 요소입니다. 문서에 명시된 TX↔RX/GND 연결 외의 방향, 정격과 실제 조립 상태를 커넥터 이름만으로 추정하지 않습니다.
- +72 V와 switching node가 존재하므로 저전압 검증을 먼저 완료하고 [`validation/README.md`](validation/README.md)의 순서를 지킵니다.
