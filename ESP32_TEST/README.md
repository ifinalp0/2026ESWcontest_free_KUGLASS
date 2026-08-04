# ESP32_TEST

ESP32-S3에 장착된 ESP32_B와 Logic Carrier 사이의 GPIO 연결을 디지털 멀티미터로 확인하기 위한 독립 ESP-IDF 프로젝트입니다. SPWM, PWM peripheral, UART 명령 수신 및 Power Stage 구동 알고리즘은 사용하지 않습니다.

## 먼저 지킬 안전 조건

- Power Stage, PDLC, +12 V, +24 V 및 고전압 경로를 모두 분리한 상태에서 저전압 GPIO만 측정합니다.
- Logic Carrier의 J6 +5 V와 DevKit USB 전원을 동시에 공급하지 않습니다.
- 멀티미터를 DC 전압 모드로 놓고 COM probe를 GND에 연결합니다. J7에서는 모든 짝수 핀이 GND지만, 커넥터 방향과 pin 1 실크를 먼저 확인하십시오.
- 이 펌웨어는 `ENABLE_CHx`도 시험을 위해 HIGH로 만듭니다. Power Stage가 연결되어 있으면 실제 출력 허가가 될 수 있으므로 반드시 분리해야 합니다.
- GPIO 출력 HIGH의 예상값은 약 3.3 V, LOW는 약 0 V입니다. 보드 전원과 계측 조건에 따라 실제값은 조금 달라질 수 있습니다.

## 시험 동작

부팅 직후 모든 출력은 5초간 LOW입니다. 이후 아래 12개 출력을 표의 순서대로 반복 시험합니다.

1. 모든 출력 LOW: 3초
2. 시험 대상 한 핀만 HIGH: 10초
3. 다음 핀으로 이동

한 바퀴는 약 2분 36초이며, 완료 후 다시 5초간 모두 LOW로 둔 다음 반복합니다. UART0 115200 baud 로그에 현재 측정할 신호가 표시됩니다.

| 순서 | 채널 신호 | ESP32 GPIO | U3 DevKit header | J7 측정 핀 |
| ---: | --- | ---: | --- | ---: |
| 1 | `PWM_MAG_CH0` | 10 | J1-16 | 1 |
| 2 | `DIR_CH0` | 11 | J1-17 | 3 |
| 3 | `ENABLE_CH0` | 12 | J1-18 | 5 |
| 4 | `PWM_MAG_CH1` | 14 | J1-20 | 17 |
| 5 | `DIR_CH1` | 15 | J1-8 | 19 |
| 6 | `ENABLE_CH1` | 16 | J1-9 | 21 |
| 7 | `PWM_MAG_CH2` | 18 | J1-11 | 33 |
| 8 | `DIR_CH2` | 21 | J3-18 | 35 |
| 9 | `ENABLE_CH2` | 38 | J3-10 | 37 |
| 10 | `PWM_MAG_CH3` | 40 | J3-8 | 49 |
| 11 | `DIR_CH3` | 41 | J3-7 | 51 |
| 12 | `ENABLE_CH3` | 42 | J3-6 | 53 |

`PWM_MAG`와 `DIR`은 ESP32 출력이 J7까지 직접 이어집니다. 반면 J7의 `CHx_ENABLE`은 Logic Carrier의 74HC08 출력이므로, 해당 GPIO가 HIGH인 10초 동안에도 `EN_GLOBAL(GPIO19)`이 HIGH일 때만 J7에서 HIGH가 측정됩니다. 원시 MCU `ENABLE_CHx`를 확인하려면 표의 U3 header 위치에서 측정하십시오.

## 입력 핀 처리

다음 핀은 회로상 입력이므로 펌웨어가 절대로 출력으로 구동하지 않습니다.

- `EN_GLOBAL`: GPIO19, 외부 pull-down 사용
- `FAULT_N_CH0~3`: GPIO13, 17, 39, 47, 외부 pull-up 사용
- ADC current/temperature: GPIO1, 2, 4, 5, 6, 7, 8, 3

각 HIGH 시험 시작 시 UART 로그에 `EN_GLOBAL`과 네 `FAULT_N`의 디지털 상태가 함께 출력됩니다. ADC 전압 측정과 보정은 이 프로젝트의 범위가 아닙니다.

## 빌드·플래시·모니터

ESP-IDF 5.x 환경에서 이 폴더를 프로젝트 루트로 사용합니다.

```bash
cd ESP32_TEST
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

GPIO19가 `EN_GLOBAL`이므로 native USB console은 사용하지 않습니다. 로그는 UART0(DevKit USB-to-UART bridge)로 출력합니다. Logic Carrier U3의 TX/RX(GPIO43/44)는 회로도상 NC이며 시험 대상 GPIO와 겹치지 않습니다.

정적 핀맵 검증은 다음 명령으로 실행할 수 있습니다.

```bash
cd ESP32_TEST
python3 -m unittest discover -s tests -v
```

## 기준 핀맵

핀 번호는 저장소의 `hardware/Logic carrier.pdf`(확인 SHA-256: `c6e7c129e7d5cd66f2e6cc850b9797e58e25d15bd59cb817267279b90fc0fa92`)를 기준으로 작성했습니다.
