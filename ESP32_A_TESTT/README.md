# ESP32_A continuous PWM command test

이 프로젝트는 ESP32_A의 UART1에서 아래 형식의 JSON Line을 50 ms(20 Hz)마다 전송합니다.

> **현재 `ESP32_B_Algo/`와 비호환:** 이 시험 코드는 `type=set`, `mi[]` frame을 전송합니다. canonical B는 `type=actuator_command`, CH0~CH3 `ch` full set을 요구하므로 이 frame을 거부합니다. 현재 상태로는 ESP32_B 제품 출력 검증에 사용하지 마십시오.

```json
{"v":1,"type":"set","seq":1,"ttl_ms":300,"enable":true,"mi":[0.80,0.65,0.40,0.55]}
```

`seq`는 매 전송마다 증가합니다. 이 프로젝트는 ESP32_A 자체에서 PWM을 생성하지 않고 B에 명령만 전송합니다.

## 배선

두 보드의 전원을 끈 상태에서 연결합니다.

| ESP32_A | ESP32_B 물리 UART(프로토콜 갱신 후) |
| --- | --- |
| GPIO39 (UART1 TX) | GPIO44 (UART1 RX) |
| GPIO40 (UART1 RX, 선택 사항) | GPIO43 (UART1 TX) |
| GND | GND |

명령 송신만 필요하면 A GPIO39 -> B GPIO44와 공통 GND만 연결해도 됩니다. 3.3 V UART 신호이며 TX끼리 직접 연결하면 안 됩니다.

## 빌드

ESP-IDF 5.x 이상 환경에서:

```bash
cd ESP32_A_TESTT
idf.py set-target esp32s3
idf.py build
```

## 플래시 및 모니터

연결 포트를 확인하고 ESP32_A에만 플래시합니다.

```bash
ls /dev/cu.*
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

모니터 종료는 `Ctrl+]`입니다. ESP32_B가 이전에 더 큰 `seq`를 받은 상태라면 A와 B를 함께 재부팅해야 `seq=1`부터 시작하는 시험 명령을 즉시 받아들입니다.

## ESP32_B에 맞게 갱신할 때의 안전 조건

- JSON frame을 `ESP32_B_Algo/README.md`의 `actuator_command` 계약으로 먼저 바꿔야 합니다.
- `EN_GLOBAL`(GPIO19)이 HIGH여야 합니다.
- `FAULT_N_CH0..3`가 모두 HIGH여야 합니다. LOW가 감지되면 fault가 latch됩니다.
- ESP32_B에는 `ESP32_B_Algo/` 펌웨어만 빌드·플래시합니다.
- A의 명령 주기는 50 ms이고 B의 timeout은 300 ms입니다. A 송신이 끊기면 B가 출력을 정지합니다.

전력단/HV가 연결된 시험은 감전 및 장비 손상 위험이 있습니다. 먼저 전력단을 분리한 상태에서 UART 수신과 B의 status JSON을 확인한 뒤 진행하십시오.
