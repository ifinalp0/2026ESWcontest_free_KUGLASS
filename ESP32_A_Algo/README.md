# ESP32_A 제품 펌웨어

`ESP32_A_Algo/`는 KUGLASS의 ESP32_A DevKit에 빌드·플래시하는 canonical
ESP-IDF 프로젝트입니다. 카메라와 내부온도를 처리하고 CH0~CH3의 권위 있는
목표 MI를 계산하여 ESP32_B로 전달합니다. 전력 출력을 직접 만들지는 않습니다.

독립 시험 코드는 [`../For_Test/`](../For_Test/)에 격리되어 있으며 이 프로젝트의
소스나 빌드 입력이 아닙니다. 제품 계약 검증에는 이 폴더의 `host_tests/`를
사용합니다.

## 책임

- OV2640 카메라의 좌/우 ROI 밝기·포화·highlight·Edge Density 계산
- YwRobot SEN050007 DS18B20 내부온도의 CRC·범위·stale 검사
- 상황 모드, privacy, thermal, camera glare 정책과 LUT/MI servo
- CH0~CH3 수동 override TTL과 AUTO 복귀
- ESP32_B로 20 Hz full-frame heartbeat 전송 및 B status 검증
- 센서 품질, target/commanded MI와 B의 applied MI/Fault를 TabUI로 중계
- 요청 중인 경우 VGA JPEG 영상을 TabUI USB 링크로 보조 전송

## 코드 구조

| 경로 | 역할 |
| --- | --- |
| `main/app_main.cpp` | task와 서비스 조립, 20 Hz 제어 loop |
| `main/camera_service.*` | OV2640 초기화, frame 수집, JPEG 전송 |
| `main/camera_metric_adapter.*` | RGB565 ROI와 카메라 지표 |
| `main/camera_recovery.*` | 실패 감지와 재초기화 backoff |
| `main/ds18b20_sensor.*` | OneWire 온도 수집 |
| `main/sensor_state.*` | 센서 snapshot, 유효성·stale 병합 |
| `main/policy_engine.*` | 모드·센서 정책과 MI servo |
| `main/ui_protocol.*` | TabUI 명령 검증 |
| `main/esp32_b_link.*` | A→B command와 B status 처리 |
| `main/master_telemetry.*` | TabUI 상태·ACK 출력 |
| `host_tests/` | 제품 계약의 host 검증 |

## 연결

```text
OV2640 + DS18B20 -> ESP32_A -> UART1 -> ESP32_B
                          |
MacBook TabUI <-----------+
         micro-USB / USB Serial-JTAG CDC
```

| 링크·센서 | ESP32_A 자원 | 상대편·설정 |
| --- | --- | --- |
| MacBook TabUI | DevKit USB 단자, USB Serial/JTAG GPIO19/20 | 데이터 micro-USB, macOS `/dev/cu.usbmodem*` |
| A→B 송신 | ESP32_A UART1 TX GPIO39 | ESP32_B RX GPIO44, 115200 8-N-1 |
| B→A 수신 | ESP32_A UART1 RX GPIO40 | ESP32_B TX GPIO43, 115200 8-N-1 |
| 내부온도 | GPIO41 | DS18B20 DAT, 3.3 V 외부전원, 공통 GND |
| 카메라 | GPIO4~18 | `main/camera_pins.h` 계약 |

TabUI용 DevKit USB와 A↔B UART는 서로 다른 링크입니다. MacBook↔A 구간에 외부
UART 배선을 추가하지 않습니다. A↔B는 다음 외부 3선 harness로 연결합니다.

```text
ESP32_A GPIO39 TX -> ESP32_B GPIO44 RX
ESP32_A GPIO40 RX <- ESP32_B GPIO43 TX
ESP32_A GND       --- ESP32_B GND
```

TX끼리 또는 RX끼리 연결하지 않습니다. Logic Carrier에는 이 UART가 라우팅되지
않습니다. DS18B20 parasite power는 사용하지 않습니다.

### 카메라 핀

카메라의 물리 핀 번호와 데이터 bit 번호가 다르므로 모듈 실크를 기준으로
배선합니다. 렌즈를 정면으로 보고 header를 아래에 둔 방향입니다.

```text
                         렌즈

  안쪽(홀수)  [GND] [SCL] [SDA] [D0 ] [D2] [D4] [D6] [DCLK] [PWDN]
  바깥(짝수)  [3.3] [VSY] [HREF][RST] [D1] [D3] [D5] [D7  ] [NC  ]
                1/2   3/4   5/6   7/8  9/10 11/12 13/14 15/16 17/18

                         PCB 가장자리
```

| 카메라 핀 | ESP32-S3 | DevKit header | 카메라 핀 | ESP32-S3 | DevKit header |
| --- | ---: | ---: | --- | ---: | ---: |
| 1 GND | GND | J1-22 | 2 3.3 | 3V3 | J1-1/2 |
| 3 SCL | GPIO4 | J1-4 | 4 VSYNC | GPIO6 | J1-6 |
| 5 SDA | GPIO5 | J1-5 | 6 HREF | GPIO7 | J1-7 |
| 7 D0 | GPIO9 | J1-15 | 8 RST | GPIO17 | J1-10 |
| 9 D2 | GPIO11 | J1-17 | 10 D1 | GPIO10 | J1-16 |
| 11 D4 | GPIO13 | J1-19 | 12 D3 | GPIO12 | J1-18 |
| 13 D6 | GPIO15 | J1-8 | 14 D5 | GPIO14 | J1-20 |
| 15 DCLK/PCLK | GPIO8 | J1-12 | 16 D7 | GPIO16 | J1-9 |
| 17 PWDN | GPIO18 | J1-11 | 18 NC | 미연결 | - |

카메라는 3.3 V만 사용하고, 가까운 위치에 100 nF와 47~100 µF decoupling을
둡니다. SCL/SDA에는 4.7 kΩ pull-up, RST에는 10 kΩ pull-up, PWDN에는 10 kΩ
pull-down을 권장합니다. 데이터·sync 배선은 가능하면 10 cm 이하로 유지하고
전원 인가 전 3.3 V-GND 단락과 D0~D7 순서를 확인합니다.

상세 핀은 compile-time 계약과 host test가 검사합니다. 근거 자료는
[ESP32 Camera Driver 2.1.7](https://components.espressif.com/components/espressif/esp32-camera),
[ESP32-S3-DevKitC-1 v1.1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html),
[ESP32-S3-WROOM-1/1U](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)과
[카메라 모듈 자료](https://web.archive.org/web/20190918034827/https://www.mpja.com/download/35647mpdata.pdf)입니다.

## 카메라 런타임

- 초기화 직후 horizontal mirror와 vertical flip을 적용해 180° 장착 방향을
  보정합니다. ROI 계산과 TabUI 영상은 같은 정방향 frame을 사용합니다.
- 캡처는 VGA 640×480 RGB565이고, 요청 시 품질 90 JPEG로 변환합니다.
- `KUGLCAM1` header, JPEG marker와 FNV-1a 검사를 사용하는 frame을 기존 USB
  Serial/JTAG byte stream에 JSON Lines와 함께 다중화합니다.
- 영상은 200 ms 간격, 최대 15초 lease의 보조 경로입니다. 길이 1 queue가 차
  있으면 새 JPEG를 건너뛰며, 실패해도 scalar metric과 제어는 계속합니다.
- task 우선순위는 `policy_20hz=8`, `tabui_rx=7`, 센서=6, B link/telemetry=5,
  `camera=4`, `camera_tx=3`입니다.
- 카메라는 두 번 연속 capture 실패 시 deinit하고 2~30초 backoff로 복구합니다.
  frame은 실제 capture timestamp 기준 1초가 지나면 invalid입니다.
- exposure/gain 단위를 검증하기 전 `ae_metadata_valid=false`를 유지합니다.

카메라 driver는 `main/idf_component.yml`과 `dependencies.lock`으로 고정합니다.
`ESP32_A_Algo/` 밖의 source, symlink나 시험 프로젝트를 빌드 입력으로 사용하지
않습니다.

## 통신과 상태

TabUI 명령, A→B actuator command, B status와 Fault reset의 전체 형식은
[`../docs/PROTOCOL.md`](../docs/PROTOCOL.md)를 따릅니다.

- ESP32_A는 CH0~CH3를 정확히 한 번씩 포함한 full frame만 전송합니다.
- B status는 version, controller, nonzero boot/challenge, sequence와 채널 집합을
  검증한 뒤에만 적용합니다.
- ESP32_B application 시작 전에 GPIO43으로 나오는 알려진 ESP32-S3 ROM boot
  banner line은 status parser 오류로 승격하지 않습니다. 이 line은 B freshness나
  online 상태를 갱신하지 않으며, `B_RESTARTING`으로 이전 status freshness와
  Fault reset context를 즉시 무효화합니다.
- 그 밖의 B status parser 오류는 다음 유효한 forward status를 받을 때까지
  downstream error로 유지합니다.
- `applied_mi`는 유효한 B status에서만 갱신합니다.
- Fault reset은 한 건만 pending으로 유지하며, B의 정확한 `control_result`를
  받아야 완료 ACK합니다. 현재 timeout은 1,500 ms입니다.
- `set_environment`와 `set_channel_fault`는
  `KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS=1`인 격리 HIL build에서만 허용합니다.

## 빌드와 플래시

ESP-IDF 6.0.2, ESP32-S3 N8R8 profile과 component registry/cache가 필요합니다.

```bash
idf.py set-target esp32s3
idf.py build

ESP32_A_PORT=/dev/cu.usbmodem1101
idf.py -p "$ESP32_A_PORT" flash
```

구성이나 프로젝트 경로를 바꿨다면 cache를 다시 만듭니다.

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

격리 HIL용 진단 명령 build:

```bash
idf.py -D KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS=1 build
```

## 검증

```bash
sh host_tests/run_tests.sh
sh ../hardware/validation/BAD_JSON/host_tests/run_tests.sh
```

Host test는 핀 소유권, 카메라 방향·복구·timestamp, RGB565 ROI, DS18B20,
센서 병합, 정책, UI/A-B protocol, B status, telemetry, task priority와 프로젝트
독립성을 검사합니다. BAD_JSON regression은 실제 B formatter가 `adc`를 마지막
field로 만든 status와 ROM banner가 섞인 stream을 A parser에 통과시킵니다.

HIL에서는 카메라 장애 중 DS18B20과 20 Hz heartbeat 지속, 1초 stale 전환,
무재부팅 복구, 실제 영상과 ROI 방향 일치, USB/A-B UART, 수동 TTL, B timeout,
target/commanded/applied MI 분리, 영상 활성 중 heartbeat와 task jitter를 확인합니다.
