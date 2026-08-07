# KUGLASS 검증 가이드

변경한 컴포넌트와 그 계약 경계까지 검증한다. 하드웨어가 없어도 정적 검사와
host test는 실행하며, 실기 결과가 필요한 항목을 소프트웨어 통과로 대신하지 않는다.

## 빠른 검증

저장소 루트에서 다음 순서로 전체 비실기 검증을 실행할 수 있다.

```bash
python3 hardware/tools/validate_hardware_contract.py

cd TabUI
npm run check
npm run build

cd ../ESP32_A_Algo
sh host_tests/run_tests.sh

cd ../ESP32_B_Algo
sh host_tests/run_tests.sh

cd ..
sh hardware/validation/BAD_JSON/host_tests/run_tests.sh
```

## 변경 범위별 최소 검사

| 변경 | 최소 검사 | 함께 확인할 문서 |
| --- | --- | --- |
| 문서만 | 링크·경로·명령 존재 여부, 중복 값 비교 | 루트 및 대상 README |
| TabUI frontend/backend | `npm run check`, `npm run build` | `TabUI/README.md`, protocol |
| ESP32_A 정책·센서·통신 | A host test | A README, protocol |
| ESP32_B protocol·출력 | B host test | B README, protocol |
| ESP32_B 핀·ADC·connector·안전 | hardware validator + B host test | hardware 전체 read order |
| wire schema | TabUI + A + B 관련 검사 | `docs/PROTOCOL.md` |
| A↔B status JSONL 경계 | BAD_JSON cross-project host regression | incident README |
| 하드웨어 실기 | 정적 검사 통과 후 단계별 HIL | `hardware/validation/README.md` |

## 문서 검증

- 모든 상대 링크의 대상이 실제로 존재하는지 확인한다.
- `For_Test/`를 제품 firmware 또는 기준 구현으로 설명하지 않는다.
- `Simul_Twin/`을 production 경로나 수정 대상으로 설명하지 않는다.
- 센서는 카메라 1대와 DS18B20 내부온도 1개, 채널은 CH0~CH3으로 유지한다.
- 하드웨어 핀·극성·수량은 machine contract와 비교한다.
- `unknown`, derived, measured, implemented, planned 상태를 섞지 않는다.
- 명령 예시는 현재 파서가 요구하는 필드와 타입을 모두 포함해야 한다.

## HIL 핵심 항목

실기 절차의 상세 순서와 기록 형식은
[`hardware/validation/README.md`](../hardware/validation/README.md)를 따른다.

- 카메라·내부온도 입력과 stale 처리
- CH0~CH3 full/unique frame과 duplicate/stale sequence 거부
- command ACK, manual TTL, AUTO 복귀와 TabUI 단절 중 A의 AUTO 지속
- A→B timeout, invalid frame, watchdog의 B safe-off
- target/commanded/applied MI 분리
- E-Stop, `FAULT_N` 1~4 sample glitch 자동 복구, 5회 연속 LOW Fault latch와 안전
  조건 확인 후 reset
- B reboot의 `boot_id`, A reboot의 `source_session_id`, one-time challenge와 replay 거부
- 정확한 `control_result` correlation과 1,500 ms reset timeout
- reset 직후 네 ENABLE LOW, PWM 0, 방향 blanking과 E-Stop hardware gate
- 외부 3선 UART harness의 `A GPIO39 TX → B GPIO44 RX`,
  `A GPIO40 RX ← B GPIO43 TX`, `A GND ↔ B GND` 연속성
- J7/J10 pin 1과 odd/even 방향, 모든 even pin GND
- ADC 8개 raw/mV, validity mask, 실제 입력 범위, open/short, 보드별 calibration
- GPIO3 source 연결 cold boot, GPIO19 reset glitch, GPIO38 LED contention

## 통과로 간주하지 않는 항목

- 파일이 없다는 이유로 회로나 조립 검증을 PASS 처리하지 않는다.
- schematic-derived 값을 measured 값으로 표시하지 않는다.
- host test 통과를 Power Stage/PDLC/HV 실기 통과로 해석하지 않는다.
- 실측 전 MI-Vrms-투과도 LUT, 온도 임계, 카메라 개선 수치를 확정하지 않는다.
