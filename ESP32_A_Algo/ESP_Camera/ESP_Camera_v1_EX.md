# ESP_Camera v1 프로젝트 구조·기능·설정 가이드

> 문서 파일: `ESP_Camera_v1_EX.md`  
> 분석 기준일: 2026-07-30 (Asia/Seoul)  
> 분석 범위: 현재 소스, ESP-IDF/CMake 설정, 생성된 `sdkconfig`, 빌드 산출물,
> 호스트 뷰어, 호스트 테스트  
> 검증 범위: ESP-IDF 빌드 및 정적 호스트 검사 성공. 이 문서 작성 과정에서
> 보드 플래시와 실제 카메라 구동 시험은 수행하지 않음.

## 1. 프로젝트 한눈에 보기

이 프로젝트는 `ESP32-S3-DevKitC-1U-N8R8`에 18핀 T&D/MPJA 계열 OV2640
병렬 카메라를 연결하고, 촬영한 영상을 Wi-Fi가 아닌 USB-to-UART 직렬
연결로 컴퓨터에 전송하는 독립 ESP-IDF 펌웨어이다.

현재 활성 데이터 경로는 다음과 같다.

```text
OV2640
  │  D0..D7 + DCLK(PCLK) + HREF + VSYNC
  ▼
ESP32-S3 카메라 드라이버
  │  RGB565 QVGA 320×240, 프레임 버퍼 1개
  ▼
ESP32-S3 소프트웨어 JPEG 인코더
  │  JPEG 품질 85, 4:2:0
  ▼
UART0 / GPIO43(TX), GPIO44(RX) / 2,000,000 baud
  │  28바이트 헤더 + JPEG + FNV-1a 검사값
  ▼
host_tools/serial_camera_viewer.py
  │  직렬 프레임 검증·최신 프레임 보관
  ▼
로컬 HTTP 서버 http://127.0.0.1:8765/
  │
  ▼
컴퓨터 웹 브라우저
```

핵심 상태는 다음과 같다.

| 구분 | 현재 상태 |
|---|---|
| 대상 MCU/보드 | ESP32-S3-DevKitC-1U-N8R8 전용 |
| ESP-IDF | 현재 잠금 버전 6.0.2, 매니페스트 최소 버전 5.1 |
| 카메라 | OV2640 PID를 시작 시 확인 |
| 캡처 | RGB565, QVGA 320×240, RAW 153,600바이트 |
| 프레임 버퍼 | 1개, PSRAM이 있으면 PSRAM 사용 |
| 영상 전송 | RGB565를 품질 85 JPEG로 소프트웨어 변환 후 UART 전송 |
| 직렬 설정 | UART0, TX GPIO43, RX GPIO44, 2,000,000 baud, 8-N-1 |
| 호스트 화면 | Python 로컬 서버, 기본 주소 `127.0.0.1:8765` |
| 네트워크 | 애플리케이션에서 Wi-Fi를 초기화하거나 사용하지 않음 |
| 빌드 타깃 | `esp32s3` 외 타깃은 CMake/헤더 검사에서 거부 |
| Flash/PSRAM | 8MB Flash, 8MB Octal PSRAM 80MHz 프로필 |
| OTA/저장소 | 현재 없음. factory 단일 앱 구조 |

## 2. 현재 디렉터리 구조

```text
ESP_Camera/
├── CMakeLists.txt                  # 프로젝트 타깃/이름 선언
├── README.md                       # 배선·빌드·실행 중심 사용자 안내
├── ESP_Camera_v1_EX.md             # 본 구조·설정·유지보수 문서
├── sdkconfig.defaults              # 재구성 가능한 핵심 기본 설정
├── sdkconfig                       # 현재 생성 설정, Git 제외 대상
├── sdkconfig.old                   # 이전 Wi-Fi 구성 흔적, Git 제외 대상
├── partitions.csv                  # NVS + 3MB factory 앱 파티션
├── dependencies.lock               # 실제 해석된 IDF/컴포넌트 버전 잠금
├── .clangd                         # clangd 플래그 보정
├── .vscode/settings.json           # 로컬 IDF 타깃·포트·clangd 경로
├── .gitignore                      # build/managed_components/sdkconfig 제외
├── esp-dev-kits-en-master-esp32s3-2.pdf
│                                     # 로컬 ESP32-S3 DevKit 참고 PDF
├── main/
│   ├── CMakeLists.txt              # 실제 펌웨어 빌드 소스와 의존성
│   ├── Kconfig.projbuild           # baud/JPEG 품질 menuconfig 항목
│   ├── idf_component.yml           # esp32-camera 2.1.7 의존성
│   ├── app_main.cpp                # 펌웨어 진입점
│   ├── camera_pins.h               # 보드 전용 핀맵과 컴파일 타임 검사
│   ├── camera_service.cpp/.h       # 카메라 초기화·센서/초기 프레임 검증
│   ├── serial_frame_server.cpp/.h  # JPEG 변환·직렬 프레임 무한 전송
│   ├── wifi_service.cpp/.h         # 현재 미빌드 레거시 Wi-Fi 구현
│   └── http_stream_server.cpp/.h   # 현재 미빌드 레거시 MJPEG 서버
├── host_tools/
│   ├── serial_camera_viewer.py     # 직렬 수신 + 로컬 웹 뷰어
│   └── __pycache__/                # 생성 캐시, 현재 .gitignore에는 없음
├── host_tests/
│   ├── test_camera_pins.cpp        # 핀/메모리 프로필 static_assert 검사
│   └── stubs/sdkconfig.h           # 호스트 핀 테스트용 설정 스텁
├── managed_components/             # Component Manager 생성물, Git 제외
│   ├── espressif__esp32-camera/
│   └── espressif__esp_jpeg/
└── build/                          # ESP-IDF 빌드 산출물, Git 제외
```

### Git 상태 주의

- Git 저장소 루트는 이 폴더가 아니라
  `/Users/mooyoung/Developer/KuGlass_dev`이다.
- 분석 시점에 `ESP_Camera` 디렉터리 전체가 저장소 기준 `?? ./`인
  **미추적 상태**이다.
- 현재 빌드의 프로젝트 버전 `856ff66-dirty`는 상위 저장소 상태에서
  파생된 값이므로, 이 카메라 프로젝트만의 신뢰 가능한 릴리스 버전으로
  사용하면 안 된다.
- 프로젝트를 보존하려면 이 디렉터리를 명시적으로 Git에 추가하고,
  별도의 태그 또는 버전 정책을 정하는 것이 좋다.
- `host_tools/__pycache__/`는 현재 `.gitignore`에 없으므로 프로젝트를
  추적하기 전에 `__pycache__/` 또는 `*.pyc` 제외 규칙을 추가하는 편이
  안전하다.

## 3. 실제 빌드에 포함되는 코드

`main/CMakeLists.txt`가 현재 포함하는 애플리케이션 소스는 정확히 세 개다.

```cmake
app_main.cpp
camera_service.cpp
serial_frame_server.cpp
```

따라서 현재 호출 흐름은 다음과 같다.

1. ESP-IDF가 `app_main()`을 실행한다.
2. `camera_service_start()`가 보드/카메라/프레임을 초기화하고 검증한다.
3. 초기화에 실패하면 오류를 반환하고 `app_main()`이 종료한다.
4. 성공하면 `serial_frame_server_run()`으로 진입한다.
5. 직렬 서버는 카메라 프레임을 계속 가져와 JPEG로 변환해 UART0으로
   전송한다.
6. UART 초기화 또는 전송이 실패한 경우에만 직렬 서버가 반환한다.

`wifi_service.cpp`와 `http_stream_server.cpp`는 파일은 존재하지만
`main/CMakeLists.txt`에 없으므로 컴파일·링크·실행되지 않는다.

## 4. 파일별 역할

### `main/app_main.cpp`

- `camera_service_start()`를 먼저 호출한다.
- 카메라 시작 실패 시 직렬 서버를 실행하지 않는다.
- 성공 시 준비 로그를 남기고 `serial_frame_server_run()`을 호출한다.
- 직렬 서버가 예상 밖으로 종료되면 오류 로그를 남긴다.
- 현재 전역 로그 레벨이 `NONE`이므로 일반적인 `ESP_LOG*` 메시지는 실제
  진단 수단으로 기대하기 어렵다.

### `main/camera_pins.h`

다음 조건을 컴파일 단계에서 강제한다.

- 타깃이 ESP32-S3인가
- Flash가 8MB 설정인가
- Octal PSRAM이 활성화되어 있는가
- PSRAM 속도가 80MHz인가
- 카메라 GPIO가 서로 중복되지 않는가
- DevKit/모듈 예약 GPIO와 충돌하지 않는가

보드 또는 메모리 변형을 바꾸면 이 파일의 검사와 실제 핀맵을 함께
수정해야 한다. 단순히 `sdkconfig`만 바꾸면 빌드가 의도적으로 실패한다.

### `main/camera_service.cpp`

카메라 설정과 시작 검증을 담당한다.

| 항목 | 값/동작 |
|---|---|
| 외부 XCLK GPIO | `-1` |
| 센서 기준 클럭 | 카메라 PCB 내장 12MHz 발진기 |
| 픽셀 형식 | `PIXFORMAT_RGB565` |
| 프레임 크기 | `FRAMESIZE_QVGA` = 320×240 |
| 카메라 JPEG 품질 필드 | 12이지만 RGB565이므로 무시됨 |
| 프레임 버퍼 수 | 1 |
| 버퍼 위치 | PSRAM 감지 시 PSRAM, 아니면 DRAM |
| grab 모드 | `CAMERA_GRAB_WHEN_EMPTY` |
| 센서 확인 | `sensor->id.PID == OV2640_PID` |
| 초기 안정화 | 500ms 대기 |
| 최초 캡처 | 최대 5회, 실패 사이 250ms |

PSRAM 존재 여부는 단순 보드 이름이 아니라
`heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0` 결과로 판단한다. PSRAM이
없거나 초기화되지 않았으면 DRAM 버퍼를 시도하지만, 이 프로젝트의
컴파일 프로필 자체는 N8R8 Octal PSRAM을 필수로 요구한다.

### `main/serial_frame_server.cpp`

- UART0을 GPIO43/44, 2,000,000 baud, 8-N-1, 흐름 제어 없음으로 설정한다.
- UART 드라이버 RX 버퍼는 256바이트, TX 버퍼는 8192바이트이다.
- UART 전환 직후 런타임 로그 레벨도 `ESP_LOG_NONE`으로 설정한다.
- JPEG 크로마 서브샘플링은 4:2:0이다.
- RGB565 입력은 Big Endian으로 JPEG 변환기에 전달한다.
- 각 프레임의 형식, 크기, 포인터와 JPEG 시작/끝 마커를 검증한다.
- 정상 JPEG에 28바이트 헤더와 FNV-1a 32비트 검사값을 붙인다.
- 프레임 전송 완료를 최대 2초 기다린다.
- 캡처/변환 오류는 20ms 뒤 재시도하지만, UART 전송 오류는 서버를
  종료시킨다.

### `host_tools/serial_camera_viewer.py`

호스트 도구는 다음 세 역할을 한 프로세스에서 수행한다.

1. 직렬 포트 자동 재연결
2. 바이너리 프레임 동기화·검사·최신 프레임 저장
3. 로컬 HTTP 서버와 브라우저 UI 제공

의존성은 Python 3와 `pyserial`이다. ESP-IDF Python 환경에는 보통
`pyserial`이 포함되어 있다.

### `host_tests/test_camera_pins.cpp`

핀 번호, 12MHz 내장 발진기, 8MB Flash, Octal 80MHz PSRAM, 예약 GPIO 회피를
`static_assert`로 확인한다. 이 테스트는 하드웨어 통신, 카메라 캡처, JPEG
변환 또는 직렬 프로토콜을 시험하지 않는다.

## 5. 하드웨어 프로필과 배선

### 대상 장치

- 개발 보드: `ESP32-S3-DevKitC-1U-N8R8`
- 모듈: `ESP32-S3-WROOM-1U-N8R8`
- Flash: 8MB Quad Flash
- PSRAM: 8MB Octal PSRAM
- 카메라: 12MHz 발진기가 실장된 빨간색 2×9핀 OV2640 보드
- 카메라 전원: 3.3V 전용

이 핀맵은 18핀 보드 전용이다. 24핀 FPC OV2640, AI-Thinker ESP32-CAM,
외부 XCLK 입력형 Waveshare 보드에는 그대로 적용할 수 없다.

### 카메라 헤더 방향

렌즈를 정면으로 보고 헤더가 아래쪽일 때의 배치이다.

```text
                         렌즈

  안쪽(홀수)  [GND] [SCL] [SDA] [D0 ] [D2] [D4] [D6] [DCLK] [PWDN]
  바깥(짝수)  [3.3] [VSY] [HREF][RST] [D1] [D3] [D5] [D7  ] [NC  ]
                1/2   3/4   5/6   7/8  9/10 11/12 13/14 15/16 17/18

                         PCB 가장자리
```

### 실제 GPIO 매핑

| 카메라 신호 | ESP32-S3 GPIO | DevKitC-1 헤더 | 방향/용도 |
|---|---:|---:|---|
| 3.3 | 3V3 | J1-1 또는 J1-2 | 카메라 전원, 5V 금지 |
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

중요한 점은 모듈의 `DCLK`가 ESP32가 출력하는 XCLK가 아니라 카메라가
출력하는 PCLK라는 것이다. 카메라 PCB가 자체 12MHz 발진기를 사용하므로
ESP32의 `pin_xclk`는 `-1`이다. `DCLK`에 ESP32 출력 클럭을 연결하면 안 된다.

### 코드가 피하도록 강제하는 GPIO

| GPIO | 제외 이유 |
|---|---|
| 0, 3, 45, 46 | 부트 스트랩 |
| 19, 20 | ESP32-S3 native USB D-/D+ |
| 26~34 | WROOM 모듈 내부/헤더 미노출 |
| 35~37 | Octal PSRAM 사용 |
| 38, 48 | DevKitC 리비전별 RGB LED |
| 43, 44 | USB-to-UART 브리지 |
| 47 | 현재 보드 프로필에서 예약 처리 |

GPIO43/44는 카메라 핀에서는 제외하지만 영상 전송용 UART0에는 의도적으로
사용한다.

### 전기적·배선 주의사항

- 카메라에는 반드시 3.3V를 사용하고 5V를 연결하지 않는다.
- GPIO에서 카메라 전원을 공급하지 않는다.
- USB 전원과 외부 5V/3.3V 헤더 전원을 임의로 혼용하지 않는다.
- SCL/SDA에는 각각 4.7kΩ의 3.3V 풀업을 권장한다.
- 안정적인 부팅을 위해 RST에는 10kΩ 풀업, PWDN에는 10kΩ 풀다운을
  추가할 수 있다.
- 카메라 가까이에 100nF와 47~100µF를 3.3V-GND 사이에 병렬 배치하면
  전원 강하 완화에 도움이 된다.
- DCLK, D0~D7, HREF, VSYNC는 가급적 10cm 이하로 하고 길이를 비슷하게
  맞춘다.
- 전원을 끈 상태에서 배선하고 3.3V-GND 단락을 확인한 뒤 켠다.
- RST를 DevKit의 EN/RST 핀에 연결하지 않고 GPIO17에 연결한다.

## 6. 카메라 처리와 메모리

### RAW 프레임

```text
320 × 240 × 2 bytes(RGB565) = 153,600 bytes
```

프레임 버퍼 한 개이므로 RAW 영상 버퍼만 계산하면 약 150KiB이다. 정상
N8R8 환경에서는 이 버퍼가 PSRAM에 배치되고 PSRAM DMA를 사용한다.

### JPEG 변환

- 센서의 JPEG 출력 대신 안정적으로 받은 RGB565를 사용한다.
- `frame2jpg()`로 소프트웨어 인코딩한다.
- 실제 품질 값은 `CONFIG_CAMERA_APP_JPEG_QUALITY=85`이다.
- 크로마 서브샘플링은 `CHROMA_420`이다.
- 장면에 따라 JPEG 크기와 FPS가 달라진다.
- README의 실측 참고값은 약 15KiB/프레임, 약 4.8fps이다.

2,000,000 baud의 8-N-1 UART는 시작/정지 비트를 포함하므로 이론상 데이터
상한이 약 200,000byte/s이다. 15KiB JPEG만 고려하면 전송 상한은 약
13fps이지만, 실제 성능은 캡처, RGB565 읽기, JPEG 인코딩, 헤더/대기 비용에
의해 더 낮아진다.

## 7. 직렬 프레임 프로토콜

모든 멀티바이트 정수는 호스트의 `struct.Struct("<...")` 정의에 따라
Little Endian이다. 현재 ESP32-S3도 Little Endian이므로 펌웨어는 packed
구조체를 그대로 전송한다.

### 28바이트 헤더

| 오프셋 | 크기 | 필드 | 설명 |
|---:|---:|---|---|
| 0 | 8 | magic | ASCII `KUGLCAM1` |
| 8 | 4 | sequence | 0부터 증가하는 `uint32_t` 프레임 번호 |
| 12 | 2 | width | 현재 320 |
| 14 | 2 | height | 현재 240 |
| 16 | 1 | format | 1=RGB565BE, 2=JPEG; 펌웨어는 2 전송 |
| 17 | 3 | reserved | 0으로 채움 |
| 20 | 4 | payload_size | 뒤따르는 payload 바이트 수 |
| 24 | 4 | payload_fnv1a | payload의 FNV-1a 32비트 값 |

헤더 뒤에는 `payload_size`만큼의 JPEG 데이터가 바로 이어진다. JPEG는
`FF D8`로 시작하고 `FF D9`로 끝나야 한다.

### 호스트 파서의 복구 전략

- 입력 버퍼에서 `KUGLCAM1`을 검색해 프레임 경계를 다시 찾는다.
- 크기 범위는 가로/세로 1~1024, payload 최대 2MiB로 제한한다.
- JPEG 시작/끝 마커와 FNV-1a를 모두 검사한다.
- 잘못된 헤더/프레임은 1바이트씩 밀어 다시 magic을 탐색한다.
- 직렬 연결이 끊기면 1초 간격으로 재연결한다.
- 프레임은 누적 큐가 아니라 최신 한 장만 보관하므로 느린 브라우저가
  펌웨어 전송을 직접 막지 않는다.

부트 ROM 출력 등 불가피한 텍스트가 UART에 섞여도 magic 검색으로 다시
동기화할 수 있게 설계되어 있다.

## 8. 호스트 웹 뷰어

### 명령행 옵션

| 옵션 | 기본값 | 의미 |
|---|---|---|
| `--port` | `/dev/cu.usbserial-1120` | USB-to-UART 장치 |
| `--baud` | `2000000` | 펌웨어와 동일해야 하는 baud |
| `--listen` | `127.0.0.1` | HTTP 바인드 주소 |
| `--http-port` | `8765` | 로컬 웹 서버 포트 |

### HTTP 엔드포인트

| 경로 | 동작 |
|---|---|
| `/` | 브라우저 UI |
| `/status` | 연결, FPS, 최근 프레임, 정상/오류 수 JSON |
| `/frame?after=N` | N 이후 최신 프레임. 새 프레임이 없으면 HTTP 204 |
| `/snapshot.jpg` | 최신 프레임이 JPEG일 때 정지 이미지 |
| `/snapshot.bmp` | RGB565BE 프레임일 때 BMP 변환; 현재 펌웨어에서는 보통 503 |
| `/favicon.ico` | HTTP 204 |

기본 `127.0.0.1` 바인딩은 같은 컴퓨터에서만 접근 가능하다. `--listen
0.0.0.0` 등으로 변경하면 LAN에 노출되지만 인증과 TLS가 없으므로 신뢰할 수
있는 네트워크에서만 사용해야 한다.

브라우저는 `/frame`을 반복 조회하고 `/status`를 1초마다 갱신한다. JPEG이면
`createImageBitmap()`으로 그리며, RGB565 호환 프레임이면 JavaScript 또는
BMP 변환 경로를 사용할 수 있다.

## 9. ESP-IDF, 의존성과 빌드 구성

### 버전 상태

| 구성 | 선언/잠금 상태 |
|---|---|
| ESP-IDF 매니페스트 조건 | `>=5.1` |
| 현재 잠긴 ESP-IDF | 6.0.2 |
| `espressif/esp32-camera` | 2.1.7 |
| `espressif/esp_jpeg` | 1.3.1, 카메라 컴포넌트의 공개 의존성 |
| 빌드 타깃 | `esp32s3` |
| CMake 프로젝트 이름 | `ov2640_web_camera` |

`dependencies.lock`은 현재 재현 가능한 버전의 기준이다. 의존성을 업데이트할
때는 lock 파일 변경, 드라이버 API, JPEG 동작과 실제 카메라 시험을 함께
검토해야 한다.

### `sdkconfig.defaults`의 핵심값

| 영역 | 현재 기본값 |
|---|---|
| CPU | 240MHz |
| FreeRTOS tick | 1000Hz |
| main task stack | 8192바이트 |
| Flash | 8MB, 80MHz, QIO 선택, Octal Flash 끔 |
| 파티션 | 사용자 `partitions.csv` |
| PSRAM | Octal, 80MHz, 부팅 초기화/메모리 테스트 |
| PSRAM malloc | 항상 내부 우선 임계 16KiB, 내부 예약 32KiB |
| 카메라 | OV2640, PSRAM DMA |
| 직렬 영상 | 2,000,000 baud |
| JPEG | 품질 85 |
| 콘솔/로그 | 애플리케이션 콘솔 없음, 기본/최대 로그 NONE |
| C++ | exceptions/RTTI 비활성 |

`sdkconfig.defaults`는 깨끗한 재구성 시 적용할 프로젝트 정책이고,
`sdkconfig`는 ESP-IDF가 생성한 현재 전체 설정이다. 두 파일을 혼동하지
않는다.

### 생성된 `sdkconfig`의 추가 주요값

| 항목 | 현재 값 |
|---|---|
| 컴파일 최적화 | Debug |
| assertion | 활성, 레벨 2 |
| 카메라 task stack | 4096바이트 |
| 카메라 task affinity | Core 0 |
| SCCB 드라이버 | 새 I2C 드라이버, port 1, 100kHz |
| Task Watchdog | 활성/자동 초기화, 5초, panic 비활성 |
| panic 동작 | 출력 후 즉시 reboot |
| monitor baud | 115200 |
| Secure Boot | 비활성 |
| Flash Encryption | 비활성 |
| NVS Encryption | 비활성 |
| ROM log | always on |
| 재현 가능 빌드 | 비활성 |
| 컴파일 시간 삽입 | 활성 |

보안 기능이 모두 꺼져 있으므로 현재 바이너리와 Flash 내용은 개발용
상태이다. 제품화 시 Secure Boot, Flash Encryption, 키 관리, 업데이트
정책을 별도로 설계해야 한다.

### Flash 모드 해석 시 주의

`sdkconfig.defaults`와 생성 심볼에는 QIO 선택이 보이지만,
`CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=y`이며 현재 빌드 산출물
`build/flasher_args.json`과 실제 빌드 완료 명령은 다음 값을 사용한다.

```text
--flash-mode dio --flash-size 8MB --flash-freq 80m
```

즉, 현재 보드에 기록되는 이미지 기준값은 **DIO / 8MB / 80MHz**이다. 설정을
바꾸거나 `fullclean`/`set-target` 후에는 `build/flasher_args.json`과 빌드
마지막의 esptool 명령을 다시 확인해야 한다.

## 10. Flash 파티션과 현재 빌드 크기

### 파티션

| 이름 | 종류 | 오프셋 | 크기 | 용도 |
|---|---|---:|---:|---|
| NVS | data/nvs | `0x9000` | `0x6000` = 24KiB | 일반 NVS |
| factory | app/factory | `0x10000` | `0x300000` = 3MiB | 단일 앱 |

현재 Flash 기록 파일은 다음과 같다.

| 오프셋 | 파일 |
|---:|---|
| `0x0000` | `build/bootloader/bootloader.bin` |
| `0x8000` | `build/partition_table/partition-table.bin` |
| `0x10000` | `build/ov2640_web_camera.bin` |

factory 파티션은 `0x310000`에서 끝난다. 8MB Flash의 끝 `0x800000`까지
약 4.94MiB가 미할당 상태이며, OTA나 저장소로 자동 사용되지는 않는다.
OTA를 추가하려면 bootloader/partition 설계, `otadata`, 2개 이상의 OTA 앱
슬롯과 업데이트 로직이 필요하다.

### 2026-07-30 검증 빌드

| 항목 | 결과 |
|---|---:|
| 애플리케이션 바이너리 | `0x3f760` = 259,936바이트 |
| 3MiB 앱 파티션 여유 | `0x2c08a0`, 약 92% |
| Bootloader | `0x3b70` = 15,216바이트 |
| Bootloader 영역 여유 | `0x4490`, 약 54% |
| 전체 이미지 size 도구 값 | 259,823바이트 |
| DIRAM | 74,013 / 341,760바이트, 21.66% |
| IRAM | 16,384 / 16,384바이트, 100% 표시 |

`idf.py size`의 IRAM 100%는 표시된 전용 IRAM 구간 기준이므로 기능 추가 시
링커/IRAM 배치와 빌드 성공 여부를 주의 깊게 확인해야 한다.

## 11. 빌드, 플래시, 실행 절차

### 1) ESP-IDF 환경 활성화

현재 개발 환경에서 확인된 스크립트:

```bash
source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh"
```

### 2) 최초 또는 설정 변경 후 빌드

```bash
cd /Users/mooyoung/Developer/KuGlass_dev/ESP32_A_Algo/ESP_Camera
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

`fullclean`은 `build/` 산출물을 지우는 작업이므로 진행 중 분석 결과가
필요하면 먼저 보관한다. 보드/메모리 프로필을 바꾸지 않았다면 일반 수정은
`idf.py build`만으로 충분하다.

### 3) 플래시

macOS의 현재 VS Code 기본 포트:

```bash
idf.py -p /dev/cu.usbserial-1120 flash
```

Linux에서는 보통 `/dev/ttyUSB0` 또는 `/dev/ttyACM0`이다. 실제 장치명은
연결 환경마다 다르므로 고정값을 그대로 가정하지 않는다.

### 4) 호스트 뷰어 실행

```bash
python host_tools/serial_camera_viewer.py \
  --port /dev/cu.usbserial-1120
```

다른 컴퓨터/포트에서는 `--port`를 바꾼다. 펌웨어의
`CONFIG_CAMERA_APP_SERIAL_BAUD`를 변경했다면 호스트의 `--baud`도 반드시
같이 변경한다.

### 5) 브라우저 접속

```text
http://127.0.0.1:8765/
```

터미널에서 다음 메시지를 확인한다.

```text
Serial connected: /dev/cu.usbserial-1120 @ 2000000
Camera viewer: http://127.0.0.1:8765/
```

웹 UI가 `실시간 수신 중`으로 바뀌고 정상 프레임 수가 계속 증가하면
카메라 캡처, JPEG 인코딩, 직렬 전송과 호스트 파싱이 연결된 것이다.

### 포트 독점 주의

플래셔, ESP-IDF monitor와 호스트 뷰어는 같은 USB-to-UART 포트를 동시에
열 수 없다. 다시 플래시하거나 monitor를 실행하기 전에 뷰어를 `Ctrl+C`로
종료해야 한다.

또한 다음 두 baud는 용도가 다르다.

- 생성 설정의 monitor 기본값: 115200
- 실제 영상 프로토콜: 2,000,000

현재 콘솔과 일반 로그가 꺼져 있고 UART0이 바이너리 전송으로 전환되므로
일반 `idf.py monitor`를 장시간 함께 쓰는 구조가 아니다. 진단 로그가
필요하면 별도 UART/USB Serial-JTAG 로그 채널 또는 디버그 전용 빌드
프로필을 설계하는 것이 좋다.

## 12. 설정 변경 지점

### 직렬 baud 변경

```bash
idf.py menuconfig
```

`OV2640 camera application → USB-to-UART frame baud rate`에서 바꾸거나
`sdkconfig.defaults`의 `CONFIG_CAMERA_APP_SERIAL_BAUD`를 수정한다.
호스트 `--baud`도 동일하게 맞춘다.

허용 범위는 115200~2000000이다.

### JPEG 품질 변경

`OV2640 camera application → Software JPEG quality` 또는
`CONFIG_CAMERA_APP_JPEG_QUALITY`를 변경한다.

- 허용 범위: 1~100
- 높을수록 화질과 payload가 증가한다.
- payload 증가는 UART 전송 시간과 FPS에 직접 영향을 준다.

### 해상도/픽셀 포맷 변경

`main/camera_service.cpp`의 다음 필드를 함께 검토한다.

```cpp
config.pixel_format
config.frame_size
config.fb_count
config.fb_location
config.grab_mode
```

해상도를 높이면 RAW 버퍼, JPEG 작업 메모리, 인코딩 시간과 UART 병목이
모두 증가한다. 호스트는 최대 1024×1024, payload 2MiB 제한을 두고 있으므로
그 범위를 넘기면 호스트도 수정해야 한다.

### 핀맵/보드 변경

다음 항목을 한 묶음으로 변경한다.

1. `main/camera_pins.h` 핀 및 보드 검사
2. `sdkconfig.defaults` Flash/PSRAM 모드
3. `host_tests/stubs/sdkconfig.h`
4. `host_tests/test_camera_pins.cpp`
5. `README.md`와 이 문서의 배선 표
6. 실제 보드 데이터시트와 헤더 리비전

## 13. 현재 미사용 Wi-Fi/HTTP 코드

`main/wifi_service.*`와 `main/http_stream_server.*`는 이전 Wi-Fi MJPEG
구현의 흔적이다. 현재 상태에서 단순히 CMake 소스 목록에 추가하면 안 된다.

이유는 다음과 같다.

- 현재 `Kconfig.projbuild`에는 `CONFIG_CAMERA_APP_WIFI_*`, SSID, 비밀번호,
  재시도 관련 항목이 없다.
- Wi-Fi 소스는 해당 매크로가 존재한다고 가정하므로 현재 설정과 맞지 않는다.
- `main/CMakeLists.txt`에도 Wi-Fi, NVS, netif, event, HTTP server 의존성이
  선언되어 있지 않다.
- HTTP 캡처/스트림 핸들러는 카메라 프레임이
  `PIXFORMAT_JPEG`라고 가정하지만 현재 카메라는 `PIXFORMAT_RGB565`이다.
- 현재 제품 방향은 무선 미사용과 USB-to-UART 전송이다.

`sdkconfig.old`에는 과거 SoftAP 설정과 평문 SSID/비밀번호가 남아 있다.
현재 `.gitignore`에 포함되어 있어도 외부 공유나 커밋 전에 반드시 확인하고,
실제 사용한 비밀정보였다면 변경해야 한다.

Wi-Fi 기능을 복구하려면 단순 활성화가 아니라 다음 중 하나로 새 경로를
설계해야 한다.

- 센서를 JPEG 모드로 되돌리고 HTTP MJPEG 경로 사용
- 현재 RGB565를 매 프레임 소프트웨어 JPEG로 바꾸어 HTTP로 제공
- UART와 Wi-Fi를 선택형 빌드 옵션으로 분리

이때 N8R8의 `1U` 모듈은 PCB 안테나가 없으므로 Wi-Fi를 실제 사용한다면
적절한 외장 안테나도 필요하다.

## 14. 개발 도구 설정

### VS Code

`.vscode/settings.json`에는 다음 로컬 절대경로가 포함되어 있다.

- 포트: `/dev/cu.usbserial-1120`
- clangd:
  `/Users/mooyoung/.espressif/tools/esp-clang/esp-20.1.1_20250829/...`
- compile commands:
  `/Users/mooyoung/Developer/KuGlass_dev/ESP32_A_Algo/ESP_Camera/build`
- OpenOCD: `board/esp32s3-builtin.cfg`

다른 컴퓨터/사용자 계정에서는 이 절대경로가 깨진다. 협업 저장소로 만들
경우 사용자별 VS Code 설정으로 옮기거나 `${workspaceFolder}` 같은
상대 표현을 검토해야 한다.

### clangd

`.clangd`는 ESP 교차 컴파일 명령에 들어 있는 `-f*`, `-m*` 계열 옵션을
clangd 분석에서 제거한다. 편집기 진단을 위한 설정이며 실제 펌웨어
컴파일러 옵션을 바꾸지 않는다.

### 호스트 핀 테스트의 컴파일러

ESP-IDF 활성화 후 단순 `clang++`가 ESP용 교차 컴파일러로 해석될 수 있다.
그 바이너리는 macOS 호스트에서 실행되지 않으므로 시스템 컴파일러를
명시하는 편이 확실하다.

```bash
/usr/bin/clang++ -std=c++17 -Wall -Wextra \
  -I host_tests/stubs -I main \
  -DCONFIG_IDF_TARGET_ESP32S3=1 \
  host_tests/test_camera_pins.cpp \
  -o /tmp/test_camera_pins_s3_host

/tmp/test_camera_pins_s3_host
```

## 15. 확인된 검증 결과

2026-07-30 현재 다음 항목을 다시 실행해 확인했다.

- ESP-IDF 6.0.2 `idf.py build`: 성공
- ESP-IDF `idf.py size`: 성공
- 시스템 clang으로 `host_tests/test_camera_pins.cpp`: 컴파일/실행 성공
- Python `ast.parse()`로 `serial_camera_viewer.py`: 문법 검사 성공
- 카메라 핀 중복/예약 GPIO 검사: 성공

아직 자동화되지 않았거나 이 문서 작성 중 확인하지 않은 항목:

- 실제 ESP32-S3 플래시 성공 여부
- OV2640 PID 감지와 최초 프레임 캡처
- 실제 2,000,000 baud 장시간 무결성
- JPEG 색상/방향/화질
- 전원 안정성 및 brownout
- 호스트 프로토콜 단위 테스트
- 케이블 분리/재연결 장시간 시험
- macOS 외 Windows/Linux 호스트 동작

## 16. 문제 해결 체크리스트

### 카메라가 감지되지 않음

- 카메라가 정확히 3.3V인지 측정한다.
- GND 공통 여부를 확인한다.
- SCL(GPIO4)/SDA(GPIO5)가 뒤바뀌지 않았는지 확인한다.
- SCCB에 4.7kΩ 풀업을 추가한다.
- RST가 계속 low, PWDN이 계속 high인지 확인한다.
- OV2640 일반 7-bit SCCB 주소 `0x30`에 응답하는지 별도 진단한다.
- 사진/실크가 다른 카메라 모듈인지 확인한다.

### `EV-VSYNC-OVF`, 프레임 실패, 깨진 화면

- DCLK가 GPIO8의 PCLK 입력에 연결되어 있는지 확인한다.
- D0~D7을 실크 기준으로 다시 확인한다. 물리 핀 번호 순서는 데이터 bit
  순서와 다르다.
- GPIO8 다음 J1-13/14의 GPIO3/46을 건너뛰고 D0을 GPIO9(J1-15)에
  연결했는지 확인한다.
- 점퍼를 10cm 이하로 줄이고 데이터/클럭선 길이를 맞춘다.
- 브레드보드를 제거하고 직접 점퍼로 시험한다.
- 전원 커패시터를 추가하고 전원을 완전히 껐다 켠다.

### 직렬 연결은 되지만 프레임이 없음

- 펌웨어와 호스트 baud가 모두 2,000,000인지 확인한다.
- 플래시한 바이너리가 현재 `build/ov2640_web_camera.bin`인지 확인한다.
- 다른 monitor/터미널이 포트를 잡고 있지 않은지 확인한다.
- `/status`의 `last_error`, `good_frames`, `bad_frames`를 확인한다.
- 잘못된 프레임이 계속 증가하면 baud/USB 케이블/USB-UART 브리지 안정성을
  의심한다.
- 정상/오류가 모두 0이면 카메라 시작 실패 또는 잘못된 펌웨어일 수 있다.
  현재 로그가 꺼져 있으므로 디버그 로그 빌드가 필요할 수 있다.

### 빌드는 되지만 플래시/부팅이 불안정함

- `idf.py fullclean && idf.py set-target esp32s3 && idf.py build`로 재구성한다.
- 빌드 마지막 flash mode가 `dio`, size `8MB`, freq `80m`인지 확인한다.
- 실제 모듈 suffix가 N8R8인지 확인한다.
- 전원과 USB 케이블을 바꿔 본다.
- 다른 ESP32-S3 메모리 변형이면 현재 프로필을 강제로 사용하지 않는다.

## 17. 변경 작업 전 체크리스트

작업을 시작할 때 최소한 다음을 확인한다.

1. 현재 폴더 전체가 Git에 추적되는지 확인한다.
2. `git status`에서 다른 상위 프로젝트 변경과 섞이지 않았는지 확인한다.
3. 실제 보드가 `ESP32-S3-DevKitC-1U-N8R8`인지 확인한다.
4. 카메라 PCB의 실크와 18핀 방향을 확인한다.
5. `dependencies.lock`의 IDF/카메라 버전을 확인한다.
6. `sdkconfig.defaults`와 생성된 `sdkconfig`의 차이를 이해한다.
7. 포트와 baud를 확인한다.
8. 변경 전 `idf.py build`와 호스트 핀 테스트를 통과시킨다.
9. 영상 경로 변경 시 펌웨어 포맷과 호스트 파서/웹 UI를 함께 수정한다.
10. 핀 변경 시 코드, 테스트, README와 이 문서를 동시에 갱신한다.

## 18. 권장 후속 개선

우선순위가 높은 유지보수 항목은 다음과 같다.

1. `ESP_Camera` 전체를 Git 추적 상태로 전환하고 기준 커밋/태그 생성
2. `__pycache__/`, `*.pyc`를 `.gitignore`에 추가
3. 직렬 헤더/FNV/재동기화에 대한 자동 호스트 단위 테스트 추가
4. 펌웨어 시작 실패를 확인할 별도 진단 로그 채널 또는 디버그 빌드 옵션 추가
5. 사용하지 않는 Wi-Fi/HTTP 코드를 삭제하거나 `legacy/`로 명확히 이동
6. QIO 선택과 실제 DIO flash 인자 차이를 보드 기준으로 확정·문서화
7. 장시간 프레임 무결성/FPS/온도/전원 시험 자동화
8. 제품화할 경우 Secure Boot, Flash Encryption, OTA/rollback 파티션 설계

## 19. 관련 자료

프로젝트 내부:

- `README.md`: 실제 배선, 실행, 기본 문제 해결
- `esp-dev-kits-en-master-esp32s3-2.pdf`: 로컬 DevKit 참고 문서
- `dependencies.lock`: 현재 의존성 버전과 hash
- `build/flasher_args.json`: 현재 빌드가 실제 사용하는 플래시 인자
- `build/project_description.json`: IDF 경로, 프로젝트 이름, 타깃과 빌드 정보

외부 근거는 `README.md` 하단의 Espressif 카메라 드라이버, DevKitC-1 사용자
가이드, WROOM-1/1U 데이터시트, OV2640 데이터시트 및 동일 카메라 보드
회로도 링크를 우선 참고한다. 배선의 최종 판단은 판매명보다 실제 PCB 실크,
회로도와 보드 리비전을 우선한다.
