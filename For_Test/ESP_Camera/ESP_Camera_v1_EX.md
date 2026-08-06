# ESP_Camera v1 프로젝트 구조·기능·설정 가이드

> 기준: 현재 소스와 `sdkconfig.defaults`
> 대상: `ESP32-S3-DevKitC-1U-N8R8` + 18핀 T&D/MPJA 계열 OV2640
> 주의: 생성된 `sdkconfig`, `build/` 내용과 바이너리 크기는 소스의 고정 계약이
> 아니다. 깨끗한 빌드 후 검증 명령으로 확인한다.

## 1. 프로젝트 한눈에 보기

이 프로젝트는 OV2640 병렬 카메라의 영상을 ESP32-S3에서 캡처하고,
USB-to-UART 직렬 연결을 통해 컴퓨터의 로컬 웹 뷰어로 전달하는 독립 ESP-IDF
펌웨어이다. 펌웨어에는 Wi-Fi 또는 HTTP 서버가 없으며, HTTP 서버는 컴퓨터에서
실행하는 Python 뷰어에만 존재한다.

현재 데이터 경로는 다음과 같다.

```text
OV2640
  │  D0..D7 + DCLK(PCLK) + HREF + VSYNC
  ▼
ESP32-S3 카메라 드라이버
  │  RGB565 QVGA 320×240, PSRAM 프레임 버퍼 1개
  ▼
ESP32-S3 소프트웨어 JPEG 인코더
  │  품질 85, 4:2:0
  ▼
UART0 / GPIO43(TX), GPIO44(RX) / 2,000,000 baud
  │  28바이트 헤더(format=2) + JPEG + FNV-1a
  ▼
host_tools/serial_camera_viewer.py
  │  JPEG 검증·최신 프레임 보관·로컬 HTTP 제공
  ▼
http://127.0.0.1:8765/ → 컴퓨터 웹 브라우저
```

| 구분 | 현재 정책 |
|---|---|
| 프로젝트 이름 | `ov2640_serial_camera` |
| 대상 | ESP32-S3-DevKitC-1U-N8R8 전용 |
| 빌드 요구사항 | CMake 3.22 이상, ESP-IDF 5.5 이상 |
| 빌드 범위 | `MINIMAL_BUILD ON` |
| 카메라 | OV2640만 활성화하고 시작 시 PID 확인 |
| 캡처 | RGB565 QVGA 320×240 |
| 프레임 버퍼 | PSRAM에 1개, DRAM 대체 경로 없음 |
| 직렬 payload | JPEG만 허용, 헤더 `format=2` |
| 네트워크 | 펌웨어에서 사용하지 않음 |
| Flash/PSRAM | 8 MiB Quad Flash + 8 MiB Octal PSRAM 80 MHz |
| 파티션 | 24 KiB NVS + 3 MiB factory 앱, 상위 영역 미할당 |

## 2. 현재 디렉터리 구조

```text
For_Test/ESP_Camera/
├── CMakeLists.txt                  # 타깃, 최소 CMake, 최소 빌드, 프로젝트명
├── README.md                       # 배선·빌드·실행 중심 안내
├── ESP_Camera_v1_EX.md             # 구조·설정·유지보수 가이드
├── sdkconfig.defaults              # 재구성 가능한 프로젝트 기본 설정
├── partitions.csv                  # NVS + factory 파티션
├── .gitignore                      # 빌드/생성 설정/Python 캐시 제외
├── .clangd                         # 편집기용 clangd 옵션 보정
├── esp-dev-kits-en-master-esp32s3-2.pdf
│                                     # 로컬 DevKit 참고 PDF
├── main/
│   ├── CMakeLists.txt              # 실제 펌웨어 소스와 직접 의존성
│   ├── Kconfig.projbuild           # baud/JPEG 품질 설정
│   ├── idf_component.yml           # IDF >=5.5, esp32-camera 2.1.7
│   ├── app_main.cpp                # 펌웨어 진입점
│   ├── camera_pins.h               # 보드 핀맵과 컴파일 타임 검사
│   ├── camera_service.cpp/.h       # 카메라 초기화·OV2640·첫 프레임 검증
│   └── serial_frame_server.cpp/.h  # RGB565→JPEG 변환과 직렬 전송
├── host_tools/
│   └── serial_camera_viewer.py     # JPEG 직렬 수신 + 로컬 웹 뷰어
└── host_tests/
    ├── test_camera_pins.cpp        # 핀맵 static_assert 검사
    └── stubs/sdkconfig.h           # 호스트 검사용 설정 스텁
```

다음은 생성물이므로 소스 구조로 취급하지 않는다.

- `build/`, `managed_components/`
- `sdkconfig`, `sdkconfig.old`
- `__pycache__/`, `*.pyc`, `*.pyo`, `*.pyd`

Python 캐시는 `.gitignore`의 `__pycache__/`와 `*.py[cod]` 규칙으로 제외한다.

## 3. 빌드와 호출 흐름

최상위 `CMakeLists.txt`는 다음 정책을 강제한다.

- `cmake_minimum_required(VERSION 3.22)`
- `IDF_TARGET=esp32s3`; 다른 타깃은 구성 단계에서 거부
- `idf_build_set_property(MINIMAL_BUILD ON)`
- 프로젝트와 앱 바이너리 이름 `ov2640_serial_camera`

`MINIMAL_BUILD`는 애플리케이션과 의존성 그래프에 필요한 컴포넌트만 구성한다.
현재 `main/CMakeLists.txt`의 직접 의존성은 UART 드라이버와 PSRAM이며,
`esp32-camera`는 `main/idf_component.yml`을 통해 관리된다. Wi-Fi, Bluetooth,
네트워크 스택과 ESP32 내장 HTTP 서버는 현재 실행 경로에 없다.

실제 애플리케이션 소스는 세 개다.

```text
app_main.cpp
camera_service.cpp
serial_frame_server.cpp
```

호출 순서는 다음과 같다.

1. ESP-IDF가 `app_main()`을 실행한다.
2. `camera_service_start()`가 카메라를 초기화한다.
3. 감지한 센서가 OV2640인지 확인하고 첫 RGB565 프레임을 검증한다.
4. 실패하면 카메라를 해제하고 직렬 서버를 시작하지 않는다.
5. 성공하면 `serial_frame_server_run()`이 RGB565 프레임을 JPEG로 변환한다.
6. 28바이트 헤더와 JPEG를 UART0으로 계속 전송한다.
7. UART 초기화 또는 전송이 실패한 경우에만 직렬 서버가 반환한다.

## 4. 파일별 역할

### `main/app_main.cpp`

- 카메라 서비스를 먼저 시작한다.
- 카메라 시작이 실패하면 직렬 전송을 시작하지 않는다.
- 성공하면 직렬 프레임 서버로 진입한다.
- 기본 로그와 콘솔이 꺼져 있으므로 일반 `ESP_LOG*` 메시지를 상시 진단 채널로
  기대해서는 안 된다.

### `main/camera_pins.h`

다음 조건을 컴파일 단계에서 검사한다.

- ESP32-S3 타깃
- 8 MiB Flash
- Octal PSRAM 활성화와 80 MHz 설정
- 카메라 GPIO 간 중복 없음
- DevKit 및 모듈 예약 GPIO와 충돌 없음

보드 또는 메모리 변형을 바꿀 때는 이 검사, `sdkconfig.defaults`, 핀 테스트와
문서의 배선 표를 한 묶음으로 변경해야 한다.

### `main/camera_service.cpp`

| 항목 | 값/동작 |
|---|---|
| 외부 XCLK GPIO | `-1` |
| 센서 기준 클럭 | 카메라 PCB 내장 12 MHz 발진기 |
| 픽셀 형식 | `PIXFORMAT_RGB565` |
| 프레임 크기 | `FRAMESIZE_QVGA` = 320×240 |
| 프레임 버퍼 수 | 1 |
| 버퍼 위치 | 항상 `CAMERA_FB_IN_PSRAM` |
| grab 모드 | `CAMERA_GRAB_WHEN_EMPTY` |
| 센서 확인 | `sensor->id.PID == OV2640_PID` |
| 초기 안정화 | 500 ms 대기 |
| 최초 캡처 | 최대 5회, 재시도 사이 250 ms |

N8R8의 PSRAM은 필수 전제이다. 런타임에서 PSRAM 유무를 검사해 DRAM으로
전환하는 경로는 없으며, PSRAM 초기화 또는 할당이 실패하면 부팅 또는 카메라
초기화가 실패한다.

### `main/serial_frame_server.cpp`

- UART0을 GPIO43/44, 2,000,000 baud, 8-N-1, 흐름 제어 없음으로 설정한다.
- RX 버퍼는 256바이트, TX 버퍼는 8192바이트이다.
- UART 전환 후 로그 레벨을 `ESP_LOG_NONE`으로 설정한다.
- RGB565 입력을 품질 설정에 따라 4:2:0 JPEG로 변환한다.
- RAW 프레임 크기와 JPEG 시작/끝 마커를 검사한다.
- 헤더의 `format`을 항상 `2`로 기록한다.
- payload의 FNV-1a 32비트 검사값을 헤더에 기록해 전송한다.
- 캡처/변환 오류는 20 ms 뒤 재시도하고 UART 전송 오류는 반환한다.

### `host_tools/serial_camera_viewer.py`

호스트 도구는 한 프로세스에서 다음 역할을 수행한다.

1. 지정한 직렬 포트에 연결하고 끊기면 1초 간격으로 재연결
2. `KUGLCAM1` 헤더, 크기, `format=2`, JPEG 마커와 FNV-1a 검사
3. 최신 JPEG 한 장과 상태 통계 보관
4. 컴퓨터의 로컬 HTTP 서버와 브라우저 UI 제공

Python 3와 `pyserial`이 필요하다. `--port`에는 기본값이 없으며 반드시 실제
USB-to-UART 장치를 지정해야 한다.

### `host_tests/test_camera_pins.cpp`

12 MHz 내장 발진기, 정확한 GPIO 매핑과 예약 GPIO 판정을 `static_assert`로
검사한다. 메모리 프로필 자체는 `camera_pins.h`를 포함하는 과정에서 함께
검사된다. GPIO47은 일반 사용 가능한 핀으로 확인한다.

이 검사는 실제 카메라 통신, JPEG 변환 또는 직렬 프로토콜을 시험하지 않는다.

## 5. 하드웨어 프로필과 배선

### 대상 장치

- 개발 보드: `ESP32-S3-DevKitC-1U-N8R8`
- 모듈: `ESP32-S3-WROOM-1U-N8R8`
- Flash: 8 MiB Quad Flash
- PSRAM: 8 MiB Octal PSRAM
- 카메라: 12 MHz 발진기가 실장된 빨간색 2×9핀 OV2640 보드
- 카메라 전원: 3.3 V 전용

이 핀맵은 18핀 보드 전용이다. 24핀 FPC OV2640, AI-Thinker ESP32-CAM,
외부 XCLK 입력형 보드에는 그대로 적용할 수 없다.

### 카메라 헤더 방향

렌즈를 정면으로 보고 헤더가 아래쪽일 때의 배치이다.

```text
                         렌즈

  안쪽(홀수)  [GND] [SCL] [SDA] [D0 ] [D2] [D4] [D6] [DCLK] [PWDN]
  바깥(짝수)  [3.3] [VSY] [HREF][RST] [D1] [D3] [D5] [D7  ] [NC  ]
                1/2   3/4   5/6   7/8  9/10 11/12 13/14 15/16 17/18

                         PCB 가장자리
```

### GPIO 매핑

| 카메라 신호 | ESP32-S3 | DevKitC-1 헤더 | 용도 |
|---|---:|---:|---|
| 3.3 | 3V3 | J1-1 또는 J1-2 | 카메라 전원, 5 V 금지 |
| GND | GND | J1-22 | 공통 접지 |
| SCL/SIOC | GPIO4 | J1-4 | SCCB 설정 클럭 |
| SDA/SIOD | GPIO5 | J1-5 | SCCB 설정 데이터 |
| VSYNC | GPIO6 | J1-6 | 프레임 동기 입력 |
| HREF | GPIO7 | J1-7 | 라인 유효 입력 |
| DCLK/PCLK | GPIO8 | J1-12 | 픽셀 클럭 입력 |
| D0 | GPIO9 | J1-15 | 영상 데이터 bit 0 |
| D1 | GPIO10 | J1-16 | 영상 데이터 bit 1 |
| D2 | GPIO11 | J1-17 | 영상 데이터 bit 2 |
| D3 | GPIO12 | J1-18 | 영상 데이터 bit 3 |
| D4 | GPIO13 | J1-19 | 영상 데이터 bit 4 |
| D5 | GPIO14 | J1-20 | 영상 데이터 bit 5 |
| D6 | GPIO15 | J1-8 | 영상 데이터 bit 6 |
| D7 | GPIO16 | J1-9 | 영상 데이터 bit 7 |
| RST | GPIO17 | J1-10 | active-low reset |
| PWDN | GPIO18 | J1-11 | active-high power-down |
| NC | 미연결 | - | 연결 금지 |

모듈의 `DCLK`는 ESP32가 출력하는 XCLK가 아니라 카메라가 출력하는 PCLK이다.
카메라 PCB가 12 MHz 발진기를 자체 사용하므로 ESP32의 `pin_xclk`는 `-1`이다.

### 예약 GPIO

| GPIO | 제외 이유 |
|---|---|
| 0, 3, 45, 46 | 부트 스트랩 |
| 19, 20 | ESP32-S3 native USB D-/D+ |
| 26~34 | WROOM 모듈 내부/헤더 미노출 |
| 35~37 | Octal PSRAM |
| 38, 48 | DevKitC 리비전별 RGB LED |
| 43, 44 | USB-to-UART 브리지 |

GPIO43/44는 카메라 신호에서는 제외하지만 영상 전송용 UART0에는 의도적으로
사용한다. GPIO47은 예약 목록에 포함되지 않으며 일반 GPIO로 사용할 수 있다.

### 전기적 주의사항

- 카메라에는 반드시 3.3 V를 사용하고 GPIO에서 전원을 공급하지 않는다.
- USB 전원과 외부 5 V/3.3 V 헤더 전원을 임의로 혼용하지 않는다.
- SCL/SDA에는 각각 4.7 kΩ의 3.3 V 풀업을 권장한다.
- RST에는 10 kΩ 풀업, PWDN에는 10 kΩ 풀다운을 추가할 수 있다.
- 카메라 가까이에 100 nF와 47~100 µF 전원 커패시터를 배치할 수 있다.
- DCLK, D0~D7, HREF, VSYNC는 가급적 10 cm 이하로 길이를 비슷하게 맞춘다.
- 전원을 끈 상태에서 배선하고 3.3 V-GND 단락을 확인한 뒤 켠다.
- 카메라 RST를 DevKit의 EN/RST가 아니라 GPIO17에 연결한다.

## 6. 카메라 처리와 메모리

RGB565 QVGA 프레임 한 장의 크기는 다음과 같다.

```text
320 × 240 × 2 bytes = 153,600 bytes
```

프레임 버퍼 한 개는 항상 PSRAM에 배치되고 PSRAM DMA를 사용한다.
`sdkconfig.defaults`는 Octal PSRAM 초기화, 80 MHz, 부팅 메모리 테스트와
`CONFIG_SPIRAM_IGNORE_NOTFOUND=n`을 설정한다. N8/N8R2 등 다른 메모리 변형은
현재 프로필의 지원 대상이 아니다.

센서의 JPEG 출력은 사용하지 않는다. 펌웨어가 안정적으로 받은 RGB565를
`frame2jpg()`로 변환하며 기본 품질은 85, 크로마 서브샘플링은 4:2:0이다.
JPEG 크기와 FPS는 장면, 케이블, 전원, 인코딩 비용에 따라 달라지므로 대상
하드웨어에서 다시 측정한다.

2,000,000 baud의 8-N-1 UART 이론상 payload 상한은 약 200,000 byte/s이다.
실제 처리량은 캡처, JPEG 인코딩, 헤더와 UART 대기 비용 때문에 더 낮다.

## 7. 직렬 프레임 프로토콜

헤더 크기는 28바이트이며 멀티바이트 정수는 Little Endian이다. 현재 프로토콜은
JPEG 전용이다. `format` 필드는 헤더 호환성을 위해 남겨 두지만 유효한 값은
`2`뿐이며 펌웨어와 호스트가 모두 이를 강제한다.

| 오프셋 | 크기 | 필드 | 설명 |
|---:|---:|---|---|
| 0 | 8 | magic | ASCII `KUGLCAM1` |
| 8 | 4 | sequence | 0부터 증가하는 `uint32_t` 프레임 번호 |
| 12 | 2 | width | 현재 320 |
| 14 | 2 | height | 현재 240 |
| 16 | 1 | format | 반드시 `2`(JPEG) |
| 17 | 3 | reserved | 0으로 채움 |
| 20 | 4 | payload_size | 뒤따르는 JPEG 바이트 수 |
| 24 | 4 | payload_fnv1a | JPEG payload의 FNV-1a 32비트 값 |

헤더 뒤에는 `payload_size`만큼의 JPEG가 이어진다. payload는 `FF D8`로
시작하고 `FF D9`로 끝나야 한다.

호스트 파서는 다음 방식으로 오류에서 복구한다.

- 입력 버퍼에서 `KUGLCAM1`을 찾아 프레임 경계를 다시 맞춘다.
- 가로/세로는 1~1024, payload는 4바이트~2 MiB로 제한한다.
- `format=2`, JPEG 마커와 FNV-1a를 모두 확인한다.
- 잘못된 헤더 또는 payload는 1바이트씩 밀어 magic을 다시 찾는다.
- 직렬 연결이 끊기면 1초 간격으로 재연결한다.
- 누적 큐 대신 최신 프레임 한 장만 보관한다.

RGB565 wire format, 바이트 순서 선택 UI와 BMP 변환 경로는 지원하지 않는다.

## 8. 호스트 웹 뷰어

### 명령행 옵션

| 옵션 | 기본값 | 의미 |
|---|---|---|
| `--port` | 없음, 필수 | USB-to-UART 장치 |
| `--baud` | `2000000` | 펌웨어와 같아야 하는 baud |
| `--listen` | `127.0.0.1` | HTTP 바인드 주소 |
| `--http-port` | `8765` | 로컬 웹 서버 포트 |

`--port`를 생략하면 인자 오류로 종료한다. macOS에서는
`/dev/cu.usbserial-*`, Linux에서는 `/dev/ttyUSB*` 또는 `/dev/ttyACM*` 중
실제 DevKit 장치를 지정한다.

### HTTP 엔드포인트

| 경로 | 동작 |
|---|---|
| `/` | JPEG 전용 브라우저 UI |
| `/status` | 연결, FPS, 최근 프레임, 정상/오류 수 JSON |
| `/frame?after=N` | N 이후 최신 JPEG, 없으면 HTTP 204 |
| `/snapshot.jpg` | 최신 JPEG 정지 이미지, 아직 없으면 HTTP 503 |
| `/favicon.ico` | HTTP 204 |

`/frame`은 `Content-Type: image/jpeg`로 응답하고 브라우저는
`createImageBitmap()`으로 그린다. 기본 loopback 바인딩은 같은 컴퓨터에서만
접근 가능하다. `--listen 0.0.0.0`으로 바꾸면 인증과 TLS 없이 네트워크에
노출되므로 신뢰할 수 있는 환경에서만 사용한다.

## 9. ESP-IDF와 빌드 설정

### 버전과 의존성

| 구성 | 선언 |
|---|---|
| CMake 최소 버전 | 3.22 |
| ESP-IDF 매니페스트 조건 | `>=5.5` |
| `espressif/esp32-camera` | 2.1.7 |
| 빌드 타깃 | `esp32s3` |
| 프로젝트 이름 | `ov2640_serial_camera` |
| 빌드 방식 | `MINIMAL_BUILD ON` |

의존성을 갱신할 때는 카메라 드라이버 API, JPEG 인코딩, 생성 설정과 실제 보드
동작을 함께 검증한다.

### `sdkconfig.defaults` 핵심값

| 영역 | 기본값/정책 |
|---|---|
| CPU | 240 MHz |
| FreeRTOS tick | 1000 Hz |
| main task stack | 8192바이트 |
| Flash | 8 MiB, 80 MHz, QIO 선택, Octal Flash 끔 |
| 파티션 | 사용자 `partitions.csv` |
| PSRAM | Octal, 80 MHz, 부팅 초기화와 메모리 테스트, 필수 |
| PSRAM malloc | 항상 내부 우선 임계 16 KiB, 내부 예약 32 KiB |
| 카메라 | OV2640만 활성, PSRAM DMA |
| 직렬 영상 | 2,000,000 baud |
| JPEG | 품질 85 |
| 콘솔/로그 | 콘솔 없음, 기본/최대/bootloader 로그 NONE |
| C++ | exceptions/RTTI 비활성 |

OV2640 외 다음 14개 센서/CCM 옵션은 명시적으로 끈다.

```text
OV7670, OV7725, NT99141, OV3660, OV5640, GC2145, GC032A,
GC0308, BF3005, BF20A6, SC030IOT, HM1055, HM0360, MEGA_CCM
```

이 설정은 사용하지 않는 센서 감지표와 레지스터 집합이 링크되는 것을 막는다.
다른 센서를 지원하려면 해당 설정만 켜는 것으로 끝내지 말고 핀/클럭 조건과
`camera_service.cpp`의 OV2640 PID 검사도 함께 변경한다.

`sdkconfig.defaults`는 깨끗한 구성에서 적용할 프로젝트 정책이고 `sdkconfig`는
도구가 생성한 결과이다. 생성 결과의 Flash 모드와 옵션은 빌드 후
`build/flasher_args.json` 및 빌드 로그로 확인한다.

## 10. Flash 파티션과 빌드 산출물

NVS 파티션은 네트워크 코드를 제거한 뒤에도 유지한다. 현재 애플리케이션이
NVS를 직접 사용하지 않더라도 factory 레이아웃의 일반 영속 데이터 영역으로
남겨 둔 것이다.

| 이름 | 종류 | 오프셋 | 크기 | 용도 |
|---|---|---:|---:|---|
| nvs | data/nvs | `0x9000` | `0x6000` = 24 KiB | 일반 NVS |
| factory | app/factory | `0x10000` | `0x300000` = 3 MiB | 단일 앱 |

factory 파티션은 `0x310000`에서 끝나며 8 MiB Flash의 나머지 상위 영역은
미할당이다. OTA나 저장소로 자동 사용되지 않는다.

정상 빌드 후 예상 파일명은 다음과 같다.

| 오프셋 | 산출물 |
|---:|---|
| `0x0000` | `build/bootloader/bootloader.bin` |
| `0x8000` | `build/partition_table/partition-table.bin` |
| `0x10000` | `build/ov2640_serial_camera.bin` |

리팩터링 후 실제 바이너리와 메모리 크기는 고정값으로 문서화하지 않는다.
다음 명령의 최신 결과를 릴리스 또는 하드웨어 검증 기록에 남긴다.

```bash
idf.py build
idf.py size
idf.py size-components
```

확인할 항목은 앱 바이너리 크기, 3 MiB factory 파티션 여유, bootloader 영역
여유, DRAM/IRAM 사용량과 최종 flash mode/frequency이다.

## 11. 빌드, 플래시와 실행

### 1) ESP-IDF 환경 활성화

ESP-IDF 5.5 이상 환경을 활성화한다. 현재 설치 방식에 맞는 스크립트를
사용한다.

```bash
source /path/to/esp-idf/export.sh
```

### 2) 구성과 빌드

```bash
cd For_Test/ESP_Camera
idf.py set-target esp32s3
idf.py build
```

타깃이나 메모리 설정을 바꿨거나 오래된 빌드 트리가 남아 있으면 먼저
`idf.py fullclean`을 실행한다. 일반 소스 변경은 증분 `idf.py build`로 충분하다.

### 3) 플래시

```bash
CAMERA_SERIAL_PORT=/dev/ttyUSB0  # 실제 장치로 변경
idf.py -p "$CAMERA_SERIAL_PORT" flash
```

macOS에서는 보통 `/dev/cu.usbserial-*`, Linux에서는 `/dev/ttyUSB*` 또는
`/dev/ttyACM*`이다.

### 4) 호스트 뷰어

```bash
python host_tools/serial_camera_viewer.py \
  --port "$CAMERA_SERIAL_PORT"
```

브라우저에서 `http://127.0.0.1:8765/`을 연다. 터미널에는 다음 형식의 메시지가
표시된다.

```text
Serial connected: <serial-port> @ 2000000
Camera viewer: http://127.0.0.1:8765/
```

웹 UI가 `실시간 수신 중`으로 바뀌고 정상 프레임 수가 증가하면 전체 경로가
동작하는 것이다.

### 포트 독점

플래셔, ESP-IDF monitor와 호스트 뷰어는 같은 USB-to-UART 포트를 동시에 열 수
없다. 다시 플래시하거나 monitor를 실행하기 전에 뷰어를 `Ctrl+C`로 종료한다.
일반 monitor baud와 영상 프로토콜의 2,000,000 baud는 용도가 다르며, 콘솔과
로그가 꺼진 현재 빌드는 장시간 `idf.py monitor`와 병행하는 구조가 아니다.

## 12. 설정 변경 지점

### 직렬 baud

`idf.py menuconfig`의
`OV2640 camera application → USB-to-UART frame baud rate` 또는
`CONFIG_CAMERA_APP_SERIAL_BAUD`를 변경한다. 허용 범위는
115200~2000000이며 호스트 `--baud`도 같은 값으로 맞춘다.

### JPEG 품질

`OV2640 camera application → Software JPEG quality` 또는
`CONFIG_CAMERA_APP_JPEG_QUALITY`를 변경한다. 허용 범위는 1~100이며 값이
높을수록 화질과 payload가 늘어 UART 전송 시간과 FPS에 영향을 준다.

### 해상도와 캡처 형식

`main/camera_service.cpp`의 다음 항목을 함께 검토한다.

```cpp
config.pixel_format
config.frame_size
config.fb_count
config.fb_location
config.grab_mode
```

해상도가 커지면 PSRAM, JPEG 작업 메모리, 인코딩 시간과 UART 병목이 모두
증가한다. 호스트의 현재 제한은 최대 1024×1024, payload 2 MiB이다. 카메라
캡처 형식을 바꾸더라도 현재 wire protocol은 JPEG 전용이므로 직렬 전송 전에
유효한 JPEG로 변환해야 한다.

### 센서, 핀맵 또는 보드

다음 항목을 한 묶음으로 변경한다.

1. `main/camera_pins.h` 핀과 보드/메모리 검사
2. `main/camera_service.cpp` 센서 PID와 캡처 설정
3. `sdkconfig.defaults` Flash/PSRAM/센서 드라이버 설정
4. `host_tests/stubs/sdkconfig.h`와 `host_tests/test_camera_pins.cpp`
5. `README.md`와 이 문서의 배선/프로토콜 설명
6. 실제 보드와 카메라 데이터시트

## 13. 개발 도구와 정적 검사

`.clangd`는 ESP 교차 컴파일 명령의 일부 플래그를 편집기 분석에서 제거한다.
실제 펌웨어 컴파일 옵션을 바꾸지 않는다. `.vscode/settings.json` 같은 사용자별
경로와 포트 설정은 프로젝트 동작 계약으로 취급하지 않는다.

호스트 핀 테스트는 ESP-IDF 환경과 무관한 호스트 C++ 컴파일러로 실행한다.

```bash
clang++ -std=c++17 -Wall -Wextra \
  -I host_tests/stubs -I main \
  -DCONFIG_IDF_TARGET_ESP32S3=1 \
  host_tests/test_camera_pins.cpp \
  -o /tmp/test_camera_pins_s3

/tmp/test_camera_pins_s3
```

Python 파일은 캐시 생성 없이 문법을 검사할 수 있다.

```bash
python -B -c 'import ast, pathlib; ast.parse(pathlib.Path(
"host_tools/serial_camera_viewer.py").read_text())'
python -B host_tools/serial_camera_viewer.py --help
```

변경 후 최소 검증 항목은 다음과 같다.

- 깨끗한 ESP-IDF 구성과 빌드
- 호스트 핀 테스트 컴파일과 실행
- Python 문법과 CLI 검사
- 실제 N8R8 플래시, OV2640 PID 감지와 첫 프레임
- 장시간 2,000,000 baud JPEG 무결성, 재연결, 전원 안정성
- `idf.py size` 및 최종 플래시 인자 기록

## 14. 문제 해결

### 카메라가 감지되지 않음

- 카메라 전원이 3.3 V인지 측정한다.
- GND 공통 여부와 SCL(GPIO4)/SDA(GPIO5) 순서를 확인한다.
- SCCB에 4.7 kΩ 풀업을 추가한다.
- RST가 계속 low 또는 PWDN이 계속 high인지 확인한다.
- 카메라가 OV2640이며 일반 7-bit SCCB 주소 `0x30`에 응답하는지 확인한다.
- 사진과 PCB 실크가 다른 모듈인지 확인한다.

### `EV-VSYNC-OVF`, 캡처 실패 또는 깨진 화면

- DCLK가 GPIO8의 PCLK 입력에 연결되었는지 확인한다.
- D0~D7을 실크 기준으로 확인한다. 물리 핀 번호와 데이터 bit 순서는 다르다.
- GPIO8 다음 J1-13/14의 GPIO3/46을 건너뛰고 D0을 GPIO9에 연결했는지 본다.
- 점퍼를 10 cm 이하로 줄이고 데이터/클럭선 길이를 맞춘다.
- 브레드보드를 제거하고 전원 커패시터를 추가해 본다.
- 실제 모듈이 N8R8이고 Octal PSRAM 초기화가 성공하는지 확인한다.

### 직렬 연결은 되지만 프레임이 없음

- 펌웨어와 호스트 baud가 모두 2,000,000인지 확인한다.
- `--port`로 실제 DevKit USB-to-UART 장치를 지정했는지 확인한다.
- 플래시한 파일이 `build/ov2640_serial_camera.bin`인지 확인한다.
- 다른 monitor 또는 터미널이 포트를 열고 있지 않은지 확인한다.
- `/status`의 `last_error`, `good_frames`, `bad_frames`를 확인한다.
- 헤더 `format`이 2이고 payload가 유효한 JPEG인지 확인한다.

### 빌드 또는 부팅이 불안정함

- ESP-IDF가 5.5 이상인지 확인한다.
- `idf.py fullclean`, `idf.py set-target esp32s3`, `idf.py build` 순서로 재구성한다.
- 실제 모듈 suffix가 N8R8인지 확인한다.
- `build/flasher_args.json`의 Flash 용량/주파수/모드를 확인한다.
- 전원과 USB 케이블을 바꿔 본다.
- 다른 ESP32-S3 메모리 변형에 현재 프로필을 강제로 사용하지 않는다.

## 15. 변경 전 체크리스트와 자료

변경 전후에 다음을 확인한다.

1. 실제 보드가 ESP32-S3-DevKitC-1U-N8R8인지 확인한다.
2. 카메라 PCB의 실크, 18핀 방향과 3.3 V 전원을 확인한다.
3. CMake 3.22/ESP-IDF 5.5 이상 환경인지 확인한다.
4. 생성 `sdkconfig`가 `sdkconfig.defaults`의 필수 PSRAM/OV2640 정책을
   반영하는지 확인한다.
5. 실제 직렬 포트와 펌웨어/호스트 baud를 확인한다.
6. 프로토콜 변경 시 펌웨어 헤더와 호스트 파서/UI를 함께 수정한다.
7. 핀 변경 시 코드, 테스트, README와 이 문서를 함께 수정한다.
8. 빌드 후 크기와 Flash 인자를 새로 측정해 검증 기록에 남긴다.

프로젝트 내부 자료:

- `README.md`: 실제 배선, 빌드, 실행과 기본 문제 해결
- `partitions.csv`: NVS와 factory 앱 레이아웃
- `sdkconfig.defaults`: 보드/메모리/센서/직렬 기본 정책
- `esp-dev-kits-en-master-esp32s3-2.pdf`: 로컬 DevKit 참고 문서
- 빌드 후 `build/flasher_args.json`: 실제 플래시 인자
- 빌드 후 `build/project_description.json`: 프로젝트명, IDF, 타깃 정보

외부 근거는 `README.md` 하단의 Espressif 카메라 드라이버, DevKitC-1 사용자
가이드, WROOM-1/1U 데이터시트, OV2640 데이터시트와 동일 카메라 보드 회로도
링크를 우선 참고한다. 최종 배선 판단은 판매명보다 실제 PCB 실크, 회로도와
보드 리비전을 우선한다.
