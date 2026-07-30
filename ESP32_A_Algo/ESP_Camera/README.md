# ESP32-S3-DevKitC-1U-N8R8 + T&D OV2640 USB 유선 카메라

제공된 사진과 **동일한 빨간색 2×9핀 OV2640 보드**를 ESP32에 점퍼선으로
연결하고, DevKitC의 USB-to-UART 케이블을 통해 컴퓨터 웹 브라우저에서
영상을 보는 독립 ESP-IDF
프로젝트입니다.

- 대상 보드: `ESP32-S3-DevKitC-1U-N8R8`
- 빌드 타깃: `esp32s3` 전용
- 메모리: 8 MB Quad Flash + 8 MB Octal PSRAM
- 전송: 기존 USB-to-UART 케이블, 2,000,000 baud
- 영상: RGB565 QVGA(320×240) 캡처 → 품질 85 소프트웨어 JPEG
- 실측: 장면에 따라 약 4.8 fps, 프레임당 약 15 KiB
- 로컬 화면: `http://127.0.0.1:8765/`
- 네트워크·Wi-Fi·외장 안테나: 사용하지 않음
- ESP-IDF: 5.1 이상

> [!IMPORTANT]
> 이 핀맵은 사진처럼 `DCLK`, `PWDN`, `NC`가 표시된 18핀 보드 전용입니다.
> 24핀 FPC OV2640, Waveshare의 외부 XCLK형 보드, AI-Thinker ESP32-CAM의
> 카메라 커넥터와는 핀맵이 다릅니다. 실크가 다르면 연결하지 마세요.

`ESP32-S3-WROOM-1U`에는 PCB 안테나가 없지만 이 프로젝트는 무선 기능을
초기화하지 않으므로 외장 안테나가 필요하지 않습니다.

## 이 모듈에서 특히 주의할 점

이 보드에는 OV2640용 **12 MHz 발진기가 이미 실장**되어 있습니다. 따라서
외부 `XCLK` 핀이 없으며:

- 보드의 `DCLK`는 카메라가 출력하는 **PCLK(pixel clock)** 입니다.
- `DCLK`에 ESP32 출력 클럭을 넣으면 안 됩니다.
- 코드는 `pin_xclk = -1`, `xclk_freq_hz = 12000000`을 사용합니다.
- `SCL`/`SDA`의 SCCB(I2C 호환)는 센서 설정용입니다. 영상은
  `D0..D7 + DCLK + HREF + VSYNC` 병렬 버스로 전달됩니다.

## ESP32-S3-DevKitC-1U-N8R8 기준

프로젝트는 공식 ESP32-S3-DevKitC-1U-N8R8의 헤더와 메모리 구성을
기준으로 합니다. `J1-n` 위치 표기는 공식 DevKitC-1 v1.0/v1.1 기준이며,
GPIO 실크가 다른 호환 보드에는 이 핀맵을 그대로 적용하면 안 됩니다.

다음 자원은 카메라 핀에서 의도적으로 제외했습니다.

- GPIO19/20: ESP32-S3 native USB D-/D+
- GPIO0/3/45/46: 부트 스트랩
- GPIO26~34: WROOM-1U 모듈 내부용이며 DevKit 헤더에 노출되지 않음
- GPIO35~37: N8R8의 Octal PSRAM 내부 통신에 사용
- GPIO38/48: DevKitC-1 리비전별 RGB LED
- GPIO43/44: USB-to-UART 콘솔

코드가 이 자원 중 하나를 카메라 핀으로 사용하도록 변경되면 컴파일 단계의
`static_assert`가 오류를 발생시킵니다.

## 카메라 헤더 방향과 번호

아래는 **렌즈를 정면으로 보고, 핀 헤더가 아래쪽**인 방향입니다. 위 행은
렌즈에 가까운 행, 아래 행은 PCB 가장자리에 가까운 행입니다.

```text
                         렌즈

  안쪽(홀수)  [GND] [SCL] [SDA] [D0 ] [D2] [D4] [D6] [DCLK] [PWDN]
  바깥(짝수)  [3.3] [VSY] [HREF][RST] [D1] [D3] [D5] [D7  ] [NC  ]
                1/2   3/4   5/6   7/8  9/10 11/12 13/14 15/16 17/18

                         PCB 가장자리
```

| 핀 | 실크 | 방향(카메라 기준) | 설명 |
|---:|---|---|---|
| 1 | GND | 전원 | 공통 접지 |
| 2 | 3.3 | 전원 | 3.3 V 입력, 5 V 금지 |
| 3 | SCL | 입력 | SCCB 클럭, `SIOC` |
| 4 | VSYNC | 출력 | 프레임 동기 |
| 5 | SDA | 양방향 | SCCB 데이터, `SIOD` |
| 6 | HREF | 출력 | 유효 라인/데이터 |
| 7 | D0 | 출력 | 영상 데이터 bit 0 |
| 8 | RST | 입력 | Reset, active-low |
| 9 | D2 | 출력 | 영상 데이터 bit 2 |
| 10 | D1 | 출력 | 영상 데이터 bit 1 |
| 11 | D4 | 출력 | 영상 데이터 bit 4 |
| 12 | D3 | 출력 | 영상 데이터 bit 3 |
| 13 | D6 | 출력 | 영상 데이터 bit 6 |
| 14 | D5 | 출력 | 영상 데이터 bit 5 |
| 15 | DCLK | 출력 | PCLK(pixel clock) |
| 16 | D7 | 출력 | 영상 데이터 bit 7 |
| 17 | PWDN | 입력 | Power-down, active-high |
| 18 | NC | - | 연결하지 않음 |

핀 번호 순서가 데이터 비트 순서와 다릅니다. 반드시 `D0`부터 `D7`까지
각 실크를 확인해 연결하세요.

## 배선 — ESP32-S3-DevKitC-1U-N8R8

`idf.py set-target esp32s3`로 빌드할 때 적용되는 배선입니다.

| 카메라 | ESP32-S3 | 공식 DevKitC-1 | 비고 |
|---|---:|---:|---|
| 3.3 | 3V3 | J1-1 또는 J1-2 | 5 V 금지 |
| GND | GND | J1-22 | 공통 접지 |
| SCL | GPIO4 | J1-4 | SCCB SIOC |
| SDA | GPIO5 | J1-5 | SCCB SIOD |
| VSYNC | GPIO6 | J1-6 | 입력 |
| HREF | GPIO7 | J1-7 | 입력 |
| DCLK | GPIO8 | J1-12 | PCLK 입력 |
| D0 | GPIO9 | J1-15 | 입력 |
| D1 | GPIO10 | J1-16 | 입력 |
| D2 | GPIO11 | J1-17 | 입력 |
| D3 | GPIO12 | J1-18 | 입력 |
| D4 | GPIO13 | J1-19 | 입력 |
| D5 | GPIO14 | J1-20 | 입력 |
| D6 | GPIO15 | J1-8 | 입력 |
| D7 | GPIO16 | J1-9 | 입력 |
| RST | GPIO17 | J1-10 | 카메라 reset, active-low |
| PWDN | GPIO18 | J1-11 | active-high 출력 |
| NC | 미연결 | - | 어떤 GPIO에도 연결하지 않음 |

GPIO19/20은 ESP32-S3 native USB용으로 비워 두었습니다. GPIO 번호는 개발
보드에 인쇄된 실크와 대조하세요. 카메라의 `RST`를 DevKit의 `RST/EN`
핀(J1-3)에 연결하지 말고, 표와 같이 GPIO17에 연결해야 합니다.

> [!CAUTION]
> 공식 J1을 위에서 아래로 보면 `... GPIO17, GPIO18, GPIO8, GPIO3,
> GPIO46, GPIO9 ...` 순서입니다. DCLK의 GPIO8(J1-12) 다음에는
> GPIO3(J1-13)과 GPIO46(J1-14)을 **두 칸 건너뛰고** D0의
> GPIO9(J1-15)에 연결하세요. GPIO3/46은 부트 스트랩 핀입니다.

## 전원과 신호 품질

- 카메라 모듈 전원은 **3.3 V만** 사용합니다. GPIO 핀으로 전원을 공급하지
  마세요.
- 공식 DevKit의 두 USB 포트는 하나 또는 둘 다 연결할 수 있습니다. 이 USB
  전원 방식을 외부 5 V 헤더 또는 3V3 헤더 전원과 섞지 마세요. 처음에는
  USB-to-UART 포트 하나로 DevKit을 공급하는 방식을 권장합니다.
- 외부 3.3 V 전원 설계 시 Espressif는 공급 능력을 500 mA 이상으로
  확보하도록 권장합니다. 이는 상시 소비전류가 아니라 Wi-Fi 피크를 위한
  전원 설계 기준이며, 카메라 부하와 마진은 별도로 더해야 합니다. DevKit
  3V3 헤더의 실제 여유 전류는 USB 전원과 탑재 LDO에 따라 달라집니다.
- 카메라 바로 옆 3.3 V-GND 사이에 100 nF 세라믹과 47~100 µF 전해
  커패시터를 병렬로 추가하면 전원 강하에 도움이 됩니다.
- 이 보드 회로에는 SCCB 풀업이 보이지 않습니다. `SCL`과 `SDA` 각각에
  4.7 kΩ을 3.3 V로 연결하는 것을 권장합니다.
- `RST`와 `PWDN`에는 내부 풀 저항이 없으므로 부동으로 두지 않습니다.
  S3의 GPIO 제어 배선에도 각각 10 kΩ 풀업(RST→3.3 V)과
  10 kΩ 풀다운(PWDN→GND)을 추가하면 부팅 중 상태가 안정적입니다.
- `DCLK`, `D0..D7`, `HREF`, `VSYNC` 점퍼는 가급적 10 cm 이하로 하고,
  길이를 비슷하게 맞춥니다.
- 전원을 끈 상태에서 배선한 후, 3.3 V-GND 단락 여부를 확인하고 켭니다.

## 빌드와 업로드

이 폴더는 상위 PDLC 제어 펌웨어와 분리된 ESP-IDF 프로젝트입니다. 명령은
반드시 `ESP_Camera` 폴더에서 실행합니다. 최상위 CMake 설정과 컴포넌트
매니페스트가 타깃을 `esp32s3`로 제한하므로 다른 ESP32 타깃으로는
빌드되지 않습니다.

처음 한 번은 기존 빌드 설정을 정리하고 타깃을 명시하세요.

```bash
cd ESP_Camera
source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh"
idf.py fullclean
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbserial-1120 flash
```

VS Code에서는 `ESP-IDF: Set Espressif Device Target`에서 `esp32s3`,
`ESP-IDF: Select Port to Use`에서 DevKit의 포트를 선택한 후
`ESP-IDF: Flash Your Project`를 실행합니다. 플래시 후 같은 포트는 바이너리
영상 전송에 사용되므로 ESP-IDF 시리얼 모니터와 로컬 뷰어를 동시에 열 수
없습니다.

Linux에서는 포트가 보통 `/dev/ttyUSB0` 또는 `/dev/ttyACM0`입니다.
`idf.py -p ...`에서 포트를 생략하면 자동 탐색을 시도합니다.

처음 빌드할 때 ESP-IDF Component Manager가 Espressif의
`esp32-camera` 2.1.7을 내려받습니다. Flash 헤더는 N8R8의 8 MB 용량으로
생성되며, 현재 factory 파티션은 3 MB입니다. 남은 상위 Flash 영역은 향후
OTA 또는 저장소 파티션 설계를 위해 비워 둡니다.

## 컴퓨터에서 화면 보기

```bash
source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh"
python host_tools/serial_camera_viewer.py \
  --port /dev/cu.usbserial-1120
```

터미널에 `Serial connected`가 표시되면 브라우저에서
`http://127.0.0.1:8765/`을 엽니다. 뷰어는 이 컴퓨터의 loopback
주소에만 바인딩되므로 다른 컴퓨터에서는 접근할 수 없습니다.

이 방식은 USB UVC 웹캠 장치가 아니라 USB 직렬 프레임과 로컬 웹 화면의
조합입니다. 펌웨어 업로드나 ESP-IDF monitor를 다시 실행하려면 먼저
뷰어를 `Ctrl+C`로 종료해 포트를 반환해야 합니다.

## N8R8 Flash와 PSRAM

이 프로젝트는 `ESP32-S3-WROOM-1U-N8R8`의 8 MB Quad Flash와 8 MB
Octal PSRAM을 기본값으로 고정합니다. Flash는 QIO 80 MHz, PSRAM은 Octal
80 MHz와 부팅 메모리 테스트를 사용합니다. Octal **Flash** 옵션은 켜지
않습니다. N8/N8R2 등 다른 메모리 변형에서 빌드하려면
`sdkconfig.defaults`와 보드 프로필 검사를 함께 변경해야 합니다.

## 해상도와 메모리 동작

펌웨어는 부팅 때 **ESP-IDF 설정에서 활성화되고 정상 초기화된 PSRAM**을
확인합니다. 보드에 PSRAM이 있어도 SDK에서 비활성화되어 있으면 안전하게
PSRAM 없음으로 동작합니다.

펌웨어는 RGB565 QVGA(320×240) 프레임 버퍼 한 개를 사용합니다.
N8R8에서는 153,600바이트 RAW 버퍼가 PSRAM에 배치되고 PSRAM DMA가
활성화됩니다. 센서의 JPEG 출력은 사용하지 않고 ESP32-S3에서 품질 85,
4:2:0 JPEG로 변환한 뒤 전송합니다. 실제 시험 장면에서는 JPEG가 약
15 KiB였고 약 4.8 fps로 수신됐습니다.

핀맵 헤더 자체는 ESP-IDF 없이도 ESP32-S3-WROOM-1U 프로필에 대해 검사할
수 있습니다.

```bash
clang++ -std=c++17 -Wall -Wextra \
  -I host_tests/stubs -I main \
  -DCONFIG_IDF_TARGET_ESP32S3=1 \
  host_tests/test_camera_pins.cpp -o /tmp/test_camera_pins_s3
/tmp/test_camera_pins_s3
```

## 정상 실행 확인

```text
Serial connected: /dev/cu.usbserial-1120 @ 2000000
Camera viewer: http://127.0.0.1:8765/
```

웹 화면 상태가 `실시간 수신 중`으로 바뀌고 `정상` 프레임 수가 증가하면
카메라 병렬 버스, PSRAM DMA, USB 직렬 전송이 모두 동작한 것입니다.

## 문제 해결

### `Camera probe failed` / `Camera not detected`

- 3.3 V와 GND를 먼저 측정합니다.
- `SCL`/`SDA`가 뒤바뀌지 않았는지 확인합니다.
- SCL/SDA에 4.7 kΩ 풀업을 추가합니다.
- RST가 low, PWDN이 high로 고정되지 않았는지 확인합니다.
- 사진과 다른 OV2640 보드인지 확인합니다.

OV2640의 일반 7-bit SCCB 주소는 `0x30`입니다.

### `Failed to get the frame`, `EV-VSYNC-OVF`, 화면 깨짐

- `DCLK`가 `PCLK` 입력 GPIO에 연결됐는지 확인합니다. `XCLK` 출력이
  아닙니다.
- D0~D7을 숫자 순서대로 다시 확인합니다. 헤더의 물리적 핀 번호 순서와
  데이터 비트 순서는 다릅니다.
- 점퍼선을 10 cm 이하로 줄이고 DCLK/데이터선 길이를 맞춥니다.
- 브레드보드를 제거하고 직접 점퍼로 시험합니다.
- 전원을 껐다 켜 첫 프레임부터 다시 확인합니다.

### 재부팅, brownout, 프레임 전송 중 멈춤

- USB 포트 대신 안정적인 3.3 V 레귤레이터를 사용하거나 전원 커패시터를
  추가합니다.
- 카메라 3.3 V를 ESP32 GPIO에서 가져오지 않습니다.
- 먼저 다른 스트림 클라이언트를 모두 닫습니다.

### 빌드 대상과 배선이 맞지 않음

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

빌드 로그의 target과 사용한 배선 표가 반드시 같아야 합니다.

## 상위 프로젝트와의 관계

상위 `ESP32_A_Algo` 펌웨어는 ESP32-S3 GPIO3~18 및 GPIO21 대부분을 이미
사용합니다. 이 카메라 펌웨어는 **같은 ESP32에 동시에 합쳐진 코드가
아니며**, 카메라 단독 시험용입니다. 두 기능을 한 MCU에 통합하려면 전체
핀맵과 실시간 처리 부하를 다시 설계해야 합니다.

## 근거 자료

- [Espressif ESP32 Camera Driver 2.1.7](https://components.espressif.com/components/espressif/esp32-camera)
- [ESP32-S3-DevKitC-1 v1.1 공식 사용자 가이드](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)
- [ESP32-S3-WROOM-1/1U 공식 데이터시트](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
- [ESP32-S3 전원 설계 체크리스트](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)
- [사진과 동일한 MPJA 35647-MP 모듈 자료(보관본)](https://web.archive.org/web/20190918034827/https://www.mpja.com/download/35647mpdata.pdf)
- [동일 보드 회로도: 12 MHz 발진기와 핀 번호(보관본)](https://web.archive.org/web/20191021162536/https://www.mpja.com/download/35647mpsch.pdf)
- [OV2640 데이터시트(Espressif 미러)](https://dl.espressif.com/dl/schematics/Camera_OV2640.pdf)
- [OV2640 하드웨어 애플리케이션 노트](https://w.electrodragon.com/w/images/1/1b/OV2640_Camera_Module_Hardware_Application_Notes_1.04.pdf)

`티앤디`는 확인 가능한 판매자명이며, PCB 제조사 표기는 확인되지
않았습니다. 따라서 최종 배선 판단은 상품명보다 실제 PCB 실크를
우선합니다.
