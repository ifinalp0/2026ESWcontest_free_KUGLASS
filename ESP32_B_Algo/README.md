# ESP32_B_Algo

ESP32_B는 ESP32_A가 계산한 CH0~CH3 목표 MI를 받아 Power Stage PCB를 직접 제어하는 4채널 actuator 펌웨어입니다.

```text
ESP32_A
  -> UART1 JSON Lines, CH0~CH3 full frame + TTL
ESP32_B
  -> 4채널 16 kHz SPWM + enable/direction
Power Stage PCB
  -> PDLC CH0~CH3
```

## 책임

- `v=1`, `type=actuator_command`, 단조 증가 `seq`와 CH0~CH3 full/unique set 검증
- 250 ms heartbeat TTL과 stale/duplicate sequence 거부
- 16 kHz carrier, 60 Hz 상당 Simplified Unipolar SPWM 생성
- 부팅, 통신 timeout, E-Stop과 Power Stage Fault 시 safe-off
- 실제 적용 MI와 채널 Fault를 `controller_id=B` 상태 frame으로 회신

ESP32_B는 카메라나 내부온도 정책을 계산하지 않습니다. Power Stage 출력과 안전 상태만 소유합니다.

## 명령

```json
{"v":1,"type":"actuator_command","seq":5501,"ttl_ms":250,"ch":[[0,0.72,true],[1,0.68,true],[2,0.42,true],[3,0.55,true]]}
```

누락·중복·범위 이탈 channel, 잘못된 MI/boolean 또는 active lease 안의 stale sequence는 frame 전체를 거부합니다.

## 상태

```json
{"v":1,"type":"status","controller_id":"B","seq":5501,"estop":false,"fault_code":"NONE","ch":[{"id":0,"mi":0.72,"fault":false},{"id":1,"mi":0.68,"fault":false},{"id":2,"mi":0.42,"fault":false},{"id":3,"mi":0.55,"fault":false}]}
```

`mi`는 B가 현재 적용 중인 값입니다. A가 보낸 목표값으로 대체하지 않습니다.

## 직접 Power Stage 핀맵

`main/power_stage_pinmap.h`의 GPIO는 ESP32_B에서 각 Power Stage PCB로 직접 연결됩니다. 실제 보드 연결 전에 PWM, direction, enable, active-low fault와 E-Stop 핀을 반드시 재검증하십시오. enable은 초기화 시 off입니다.

## 빌드와 검증

```bash
idf.py set-target esp32s3
idf.py build
sh host_tests/run_tests.sh
```

HIL에서는 부팅 기본 off, 네 채널 출력, A→B timeout, stale sequence, E-Stop, Power Stage Fault와 B→A 상태 회신을 확인합니다.
