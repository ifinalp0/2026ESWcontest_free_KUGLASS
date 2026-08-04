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

카메라 영상 입력의 검증 기준은 루트의 [`../ESP_Camera/`](../ESP_Camera/)입니다. 이 디렉터리는 독립 빌드·플래시와 구현 참고용으로 보존하며, ESP32_A 통합 작업 때 레퍼런스 내부 파일을 수정하지 않습니다.

현재 `main/CMakeLists.txt`의 카메라 소스 경로는 루트 `ESP_Camera/`를 해석하지 못합니다. ESP32_A의 카메라 통합 구성은 대상 프로젝트 안에서 별도로 확정하고, `ESP_Camera/`는 편집 가능한 종속 컴포넌트로 취급하지 않습니다.

카메라 driver의 exposure/gain을 실제 단위로 검증하기 전에는 `ae_metadata_valid=false`를 유지하고 정책의 AE pressure 항에서 제외합니다. 임의 값은 실측 metadata로 보고하지 않습니다.

## 배선

| 링크/센서 | ESP32_A | 상대편 | 설정 |
|---|---:|---|---|
| TabUI backend | native USB GPIO19/20 | Docker host USB device | JSON Lines |
| ESP32_B TX | GPIO39 / UART1 TX | ESP32_B RX | 115200 8-N-1 |
| ESP32_B RX | GPIO40 / UART1 RX | ESP32_B TX | 115200 8-N-1 |
| 내부온도 | GPIO41 | DS18B20 DQ | 3.3 V, 외부 4.7 kΩ pull-up |
| 카메라 | GPIO4~18 | OV2640 | [`../ESP_Camera/README.md`](../ESP_Camera/README.md) 보드 프로필 준수 |

ESP32_A와 ESP32_B는 GND를 공유해야 합니다. DS18B20은 외부전원 방식만 사용하며 parasite power를 지원하지 않습니다.

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
- `reset_fault`: 파싱되지만 B reset 계약을 활성화하기 전에는 `B_RESET_UNSUPPORTED`로 거부
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
{"v":1,"type":"status","controller_id":"B","seq":5501,"ch":[{"id":0,"mi":0.72,"fault":false},{"id":1,"mi":0.68,"fault":false},{"id":2,"mi":0.42,"fault":false},{"id":3,"mi":0.55,"fault":false}]}
```

ESP32_A는 정확한 version/type/controller, CH0~CH3 full/unique set과 forward sequence를 만족하는 상태만 TabUI로 중계합니다. status timeout 뒤 첫 유효 full frame에서만 sequence 기준을 다시 설정합니다.

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
```

격리된 HIL 환경에서만 진단 입력 firmware를 별도로 빌드합니다.

```bash
idf.py -D KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS=1 build
```

## 검증

```bash
sh host_tests/run_tests.sh
```

Host test는 4채널 protocol, UI command parser, policy, RGB565 ROI, DS18B20 CRC, B status, JSON line accumulator와 master telemetry를 검사합니다.

HIL에서는 카메라·DS18B20 동시 동작, USB CDC, A↔B UART, 수동 TTL, stale sequence, A→B timeout과 target/commanded/applied MI 분리를 확인합니다.
