# ESP32_A_Algo

ESP32_A는 KUGLASS의 카메라·내부온도 입력과 4채널 목표 MI를 소유하는 ESP-IDF firmware입니다.

```text
OV2640 카메라 + DS18B20 내부온도
  -> ESP32_A: ROI 지표·정책·LUT·MI servo
  -> UART1 20 Hz JSON Lines, CH0~CH3 full frame + 250 ms TTL
ESP32_B
```

TabUI는 native USB CDC로 고수준 명령을 보내고 ESP32_A 상태를 받습니다. ESP32_A는 전력 출력을 직접 생성하지 않습니다.

## 책임

- 카메라 좌/우 ROI 평균 밝기, 포화 비율, highlight 면적과 Edge Density 계산
- DS18B20 내부온도 측정, CRC·범위·stale 검사
- 상황 모드, privacy, thermal, camera glare 정책
- LUT와 rate-limited MI servo를 이용한 CH0~CH3 목표 생성
- 채널별 수동 override TTL과 AUTO 복귀
- ESP32_B에 20 Hz full-frame heartbeat 전송
- 카메라·온도 품질, 목표/명령 MI와 ESP32_B 상태를 TabUI로 중계

## 카메라 통합

ESP32_A가 사용하는 카메라 서비스와 핀 계약은 `main/camera_service.*`, `main/camera_pins.h`가 직접 소유합니다. 카메라 드라이버는 `main/idf_component.yml`과 `dependencies.lock`으로 고정하므로 sibling 프로젝트나 저장소 루트의 파일 없이 이 폴더만으로 구성·빌드·플래시할 수 있습니다.

깨끗한 개발 환경의 최초 빌드에는 ESP-IDF 6.0.2와 Espressif Component Registry 접속 또는 이미 채워진 component cache가 필요합니다. 생성되는 `managed_components/`는 로컬 빌드 산출물이며 `ESP_Camera/`를 대체하거나 참조하지 않습니다.

선택적인 standalone 카메라 시험 프로젝트는 ESP32_A firmware의 일부가 아닙니다. 해당 프로젝트의 `app_main`, serial frame server, viewer, partition 설정이나 빌드 캐시는 사용하지 않으며 그 폴더가 삭제되어도 ESP32_A에는 영향이 없습니다.

카메라와 DS18B20은 별도 FreeRTOS task에서 실행됩니다. 따라서 카메라 프레임 대기가 지연되어도 온도 수집과 20 Hz 제어 heartbeat는 계속 실행됩니다. 카메라는 두 번 연속 capture에 실패하면 deinit하며, 2초부터 최대 30초까지 증가하는 backoff로 자동 재초기화합니다. 프레임 시각은 드라이버가 기록한 실제 capture timestamp를 사용하고 1초가 지나면 invalid로 전환합니다.

카메라 driver의 exposure/gain을 실제 단위로 검증하기 전에는 `ae_metadata_valid=false`를 유지하고 정책의 AE pressure 항에서 제외합니다. 임의 값은 실측 metadata로 보고하지 않습니다.

## 배선

| 링크/센서 | ESP32_A | 상대편 | 설정 |
|---|---:|---|---|
| TabUI backend | native USB GPIO19/20 | Docker host USB device | JSON Lines |
| ESP32_B TX | GPIO39 / UART1 TX | ESP32_B RX | 115200 8-N-1 |
| ESP32_B RX | GPIO40 / UART1 RX | ESP32_B TX | 115200 8-N-1 |
| 내부온도 | GPIO41 | DS18B20 DQ | 3.3 V, 외부 4.7 kΩ pull-up |
| 카메라 | GPIO4~18 | OV2640 | `main/camera_pins.h` 내부 핀 계약 준수 |

ESP32_A와 ESP32_B는 GND를 공유해야 합니다. DS18B20은 외부전원 방식만 사용하며 parasite power를 지원하지 않습니다.

카메라 상세 핀은 ESP32_A 내부 compile-time 계약으로 검사합니다.

| 신호 | GPIO | 신호 | GPIO |
|---|---:|---|---:|
| SIOC | 4 | SIOD | 5 |
| VSYNC | 6 | HREF | 7 |
| PCLK/DCLK | 8 | D0~D7 | 9~16 |
| RESET | 17 | PWDN | 18 |
| XCLK | 미사용 | 모듈 자체 발진기 | 12 MHz |

카메라, native USB, B UART와 DS18B20의 핀 중복 및 DevKit 예약 핀 사용은 firmware와 host compile test에서 모두 거부합니다.

### 카메라 헤더와 실기 배선

아래 방향은 렌즈를 정면으로 보고 핀 헤더를 아래에 둔 상태입니다. 물리 핀 번호와 데이터 bit 순서가 다르므로 실크의 `D0`~`D7`을 기준으로 배선합니다.

```text
                         렌즈

  안쪽(홀수)  [GND] [SCL] [SDA] [D0 ] [D2] [D4] [D6] [DCLK] [PWDN]
  바깥(짝수)  [3.3] [VSY] [HREF][RST] [D1] [D3] [D5] [D7  ] [NC  ]
                1/2   3/4   5/6   7/8  9/10 11/12 13/14 15/16 17/18

                         PCB 가장자리
```

| 카메라 핀/실크 | ESP32-S3 | DevKitC-1 header | 카메라 핀/실크 | ESP32-S3 | DevKitC-1 header |
|---|---:|---:|---|---:|---:|
| 1 GND | GND | J1-22 | 2 3.3 | 3V3 | J1-1/2 |
| 3 SCL | GPIO4 | J1-4 | 4 VSYNC | GPIO6 | J1-6 |
| 5 SDA | GPIO5 | J1-5 | 6 HREF | GPIO7 | J1-7 |
| 7 D0 | GPIO9 | J1-15 | 8 RST | GPIO17 | J1-10 |
| 9 D2 | GPIO11 | J1-17 | 10 D1 | GPIO10 | J1-16 |
| 11 D4 | GPIO13 | J1-19 | 12 D3 | GPIO12 | J1-18 |
| 13 D6 | GPIO15 | J1-8 | 14 D5 | GPIO14 | J1-20 |
| 15 DCLK/PCLK | GPIO8 | J1-12 | 16 D7 | GPIO16 | J1-9 |
| 17 PWDN | GPIO18 | J1-11 | 18 NC | 미연결 | - |

공식 J1에서 GPIO8 다음에는 부트 스트랩 GPIO3·46을 두 칸 건너뛰고 GPIO9에 연결합니다. 카메라 RST는 DevKit의 RST/EN이 아니라 GPIO17에 연결합니다.

- 카메라에는 3.3 V만 공급하며 5 V 또는 GPIO 전원을 사용하지 않습니다. 외부 3.3 V 공급은 500 mA 이상의 여유를 확보합니다.
- 카메라 가까이에 100 nF와 47~100 µF를 3.3 V-GND 사이에 병렬 배치합니다.
- SCL·SDA에는 각각 4.7 kΩ pull-up을 3.3 V로 연결합니다.
- RST는 10 kΩ pull-up, PWDN은 10 kΩ pull-down을 권장하며 두 신호를 부동 상태로 두지 않습니다.
- DCLK, D0~D7, HREF, VSYNC 점퍼는 가능하면 10 cm 이하의 비슷한 길이로 배선하고 전원 인가 전에 3.3 V-GND 단락을 확인합니다.
- 카메라 probe 실패 시 전원·SCL/SDA·RST/PWDN을 먼저 확인하고, frame/VSYNC 오류 시 DCLK와 D0~D7 순서·배선 길이를 확인합니다.

배선 근거는 [ESP32 Camera Driver 2.1.7](https://components.espressif.com/components/espressif/esp32-camera), [ESP32-S3-DevKitC-1 v1.1 가이드](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html), [ESP32-S3-WROOM-1/1U 데이터시트](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf), [카메라 모듈 자료](https://web.archive.org/web/20190918034827/https://www.mpja.com/download/35647mpdata.pdf)와 [모듈 회로도](https://web.archive.org/web/20191021162536/https://www.mpja.com/download/35647mpsch.pdf)입니다.

## TabUI → ESP32_A

모든 LIVE 명령은 `v=1`, `type=ui_command`, 단조 증가 `seq`를 포함합니다.

```json
{"v":1,"type":"ui_command","seq":101,"command":"set_mode","mode":"driving"}
{"v":1,"type":"ui_command","seq":102,"command":"set_demo","demo_mode":"hot_summer"}
{"v":1,"type":"ui_command","seq":103,"command":"manual_channel","channel_id":2,"target_mi":0.42,"ttl_ms":30000,"enable":true}
{"v":1,"type":"ui_command","seq":104,"command":"return_auto","channel_id":2}
```

지원 명령:

- `set_mode`: `driving`, `stopped`, `camping`, `parked`
- `set_demo`: `none`, `hot_summer`, `camping`, `parked`, `camera_saturation`
- `manual_channel`: channel 0~3, MI 0.0~1.0, TTL 1~300초
- `return_auto`: 단일 채널 또는 전체 수동 override 해제
- `reset_fault`: 최신 B `boot_id`/`reset_challenge`를 사용해 전달하며, B가 같은
  boot/source/sequence를 포함한 `control_result`를 보낸 뒤에만 최종 ACK
- `set_environment`, `set_channel_fault`: 명시적 HIL firmware 전용

HIL 환경 입력은 `internal_temp_c`, `front_left_saturation`, `front_right_saturation`, `edge_density`만 허용합니다. production 기본값인 `KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS=0`에서는 환경·fault 주입을 거부합니다.

## ESP32_A → ESP32_B

ESP32_A는 매 frame에 CH0~CH3 전체를 한 번씩 포함합니다.

```json
{"v":1,"type":"actuator_command","seq":5501,"ttl_ms":250,"ch":[[0,0.72,true],[1,0.68,true],[2,0.42,true],[3,0.55,true]]}
```

채널 누락·중복, 잘못된 ID, 유한 범위를 벗어난 MI와 boolean 이외의 enable 값은 frame 전체 거부 대상입니다.

ESP32_B 상태 계약:

```json
{"v":1,"type":"status","controller_id":"B","seq":5501,"boot_id":2189921,"reset_challenge":771020,"estop":false,"fault_code":"NONE","ch":[{"id":0,"mi":0.72,"fault":false},{"id":1,"mi":0.68,"fault":false},{"id":2,"mi":0.42,"fault":false},{"id":3,"mi":0.55,"fault":false}]}
```

ESP32_A는 version/type/controller, nonzero `boot_id`/`reset_challenge`, E-Stop과
fault code, CH0~CH3 full/unique set을 엄격하게 검증합니다. 동일 B boot에서는
timeout 뒤에도 forward sequence만 허용하며, `boot_id`가 바뀐 경우에만 sequence를
즉시 다시 설정합니다. 유효한 원문 status는 TabUI로 그대로 중계합니다.

B status의 선택 필드인 `diagnostic`, `adc`, `control_result`도 타입과 범위를
검증합니다. ADC validity mask가 유효성의 기준이며 ADC 값만으로 보호 동작을
판단하지 않습니다.

Fault reset은 다음처럼 A의 부팅별 nonzero session과 현재 B context에 묶입니다.

```json
{"v":1,"type":"control","seq":105,"source_session_id":345991,"target_boot_id":2189921,"reset_challenge":771020,"command":"reset_fault"}
```

A는 reset을 한 건만 pending으로 유지합니다. UART 전송 성공은 완료 ACK가
아니며, B의 정확히 일치하는 `control_result` 수신 시에만 성공/거부를 응답합니다.
context가 없거나 이미 pending이면 즉시 실패하고, 응답이 없으면 1500 ms 뒤
`B_RESET_TIMEOUT`으로 실패합니다. B의 challenge는 reset 시도 및 safety trip에서
변경되므로 이전 reset frame을 재사용할 수 없습니다.

## 상태 telemetry

ESP32_A의 `type=state`에는 다음 정보가 포함됩니다.

- 카메라와 AE metadata 유효성, 좌/우 포화 비율, Edge Density, glare와 frame timestamp
- 내부온도 또는 `null`
- CH0~CH3 `target_mi`, `commanded_mi`, enable, master fault와 manual remaining TTL
- ESP32_B healthy/stale와 protocol 오류

`applied_mi`는 B가 보낸 유효 `type=status`에서만 갱신됩니다.

## 빌드

```bash
idf.py set-target esp32s3
idf.py build
ESP32_A_PORT=/dev/ttyUSB0  # 실제 ESP32_A 포트로 변경
idf.py -p "$ESP32_A_PORT" flash
```

폴더 이동이나 ESP-IDF 설정 변경 뒤에는 캐시를 재생성합니다.

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

격리된 HIL 환경에서만 진단 입력 firmware를 별도로 빌드합니다.

```bash
idf.py -D KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS=1 build
```

## 검증

```bash
sh host_tests/run_tests.sh
```

Host test는 카메라/A UART/DS18B20 핀 충돌, 카메라 복구 backoff, timestamp wrap/stale 경계, 센서별 상태 병합, 4채널 protocol, UI command parser, policy, RGB565 byte order와 ROI, DS18B20 CRC, B status, JSON line accumulator와 master telemetry를 검사합니다.

또한 프로젝트 독립성 검사는 빌드 입력의 sibling 경로 참조, 외부 symlink, 내부 카메라 서비스 누락과 카메라 드라이버 version pin 누락을 거부합니다.

HIL에서는 다음을 확인합니다.

- 카메라 응답을 끊어도 DS18B20 timestamp와 A→B 20 Hz heartbeat가 계속 갱신되는지
- 마지막 정상 frame 이후 1초 안에 `camera_valid=false`가 되는지
- 카메라 재연결 후 ESP32_A 재부팅 없이 frame과 telemetry가 복구되는지
- USB CDC, A↔B UART, 수동 TTL, stale sequence와 A→B timeout
- `target_mi`, `commanded_mi`, ESP32_B의 `applied_mi`가 구분되는지
- 각 task의 stack high-water와 20 Hz control jitter가 안전한 범위인지
