# Logic Carrier as-built 기준

이 폴더는 현재 제작된 Logic Carrier 1장의 기준 자료입니다. ESP32_B DevKitC-1-N8R8을 U3에 장착하고, J7의 CH0~CH3 블록에 동일한 단일 채널 Power Stage PCB를 각각 한 장씩 연결합니다.

## 원본의 역할

| 파일 | 역할 |
| --- | --- |
| `Logic carrier.pdf` | 2페이지 전체 as-built 회로 기준. ADC filter page를 포함함 |
| `Logic carrier.kicad_sch` | 편집 가능한 main schematic과 main-sheet netlist |
| `datasheets/74HC08.pdf` | U4 부품 자료 |

KiCad main schematic은 ADC filter용 `untitled.kicad_sch`를 참조하지만 해당 하위 시트 파일은 이 폴더에 없습니다. 따라서 KiCad가 내보낸 netlist/BOM/ERC에서 R23-R30과 C24-C31이 빠지고 ADC 계층 연결 오류가 발생합니다. 실제 제작 회로와 ADC 필터의 기준은 `Logic carrier.pdf` 2페이지이며, 누락 파일을 추측으로 재작성하지 않습니다.

## 역할과 연결

- U3: ESP32-S3-DevKitC-1-N8R8
- U4: 74HC08, `CHx_ENABLE = EN_GLOBAL AND ENABLE_CHx`
- J5: NC E-Stop 입력. 정상 closed는 `EN_GLOBAL=HIGH`, open/단선은 R18 10 kohm으로 LOW
- R19-R22: Power Stage `FAULT_N_CH0~3`의 10 kohm pull-up
- J7: 2x32 Power Stage interface. 모든 짝수 핀은 GND
- R23-R30/C24-C31: 여덟 ADC 입력의 1 kohm/100 nF RC filter

GPIO와 커넥터의 기계 판독 기준은 [`../contracts/esp32_b_io.json`](../contracts/esp32_b_io.json)입니다.

## 전원 경로

| 위치 | 연결 | 현재 의미 |
| --- | --- | --- |
| J1 | `+24V_BAT`, GND | 24 V 입력 |
| F1 | 15 A | J1 이후 직렬 퓨즈 |
| J4 | fuse output, `+24V_SW` | 외부 main power switch |
| J3 | `+24V_SW`, GND | 외부 boost 입력으로 명명된 연결 |
| J2 | `+12V`, GND | 외부 12 V 입력 및 J7 분배 |
| J6 | `+5V`, GND | 외부 buck 5 V 출력 입력 및 U3 공급 |
| U3 3V3 | `+3.3V` | Logic Carrier와 J7 logic rail |

24 V에서 12 V나 5 V를 만드는 변환기는 이 보드에 없습니다. J2/J3/J6의 실제 외부 모듈과 배선은 별도 조립 정보이며 이름만으로 방향이나 정격을 추정하지 않습니다.

## 현재 회로 제약

- U4의 MCU측 `ENABLE_CHx` 입력에는 외부 pull-down이 없습니다. 펌웨어가 가장 이른 초기화 단계에서 네 enable을 LOW로 만들어야 합니다.
- `EN_GLOBAL`은 U4 입력과 GPIO19에 직접 연결됩니다. GPIO19는 input-only로 유지합니다.
- J5는 enable만 차단하고 전원 rail이나 PWM/DIR은 차단하지 않습니다.
- `FAULT_N=HIGH`는 커넥터가 빠진 경우에도 pull-up으로 관찰될 수 있습니다.
- ADC filter에는 clamp, divider 또는 level shifter가 없습니다.
- GPIO3은 CH3 temperature ADC와 strapping 기능을 공유합니다.
- GPIO38은 CH2 enable이며 일부 DevKitC-1 보드의 RGB LED와 공유될 수 있습니다.
- GPIO43/44는 Logic Carrier에서 A-B UART connector로 라우팅되지 않습니다. 펌웨어의 A-B 링크는 외부 harness가 필요합니다.
- J7의 mating-face 방향과 실제 케이블 pin 1은 파일만으로 확정하지 않고 실물 조립에서 확인합니다.

## 제작 자료 상태

이 보드는 제작 완료 상태이지만 현재 저장소에는 Logic Carrier PCB, Gerber, drill, placement, 조립 BOM, 보드 serial/revision 표가 없습니다. 이 부재를 미제작 상태로 해석하지 않으며, 반대로 파일에 없는 제조 세부값을 추측하지도 않습니다.
