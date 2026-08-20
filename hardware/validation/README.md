# As-built 하드웨어 검증

이 절차는 이미 제작된 Logic Carrier와 Power Stage를 현재 펌웨어에 안전하게 연결하기 위한 기록 기준입니다. 설계 변경 체크리스트가 아니라 as-built 보드의 연결, 동작과 계측값을 확인하는 절차입니다.

## 기록 형식

각 결과에는 다음 정보를 함께 남깁니다.

- `test_id`
- 날짜와 작업자
- Logic Carrier 식별 정보
- Power Stage 위치(CH0~CH3)와 보드 식별 정보
- ESP32_A/B DevKit revision
- firmware Git commit
- 사용 전원과 current limit
- 계측기 model/serial
- 시험 조건, 기대값, 측정값, 단위와 PASS/FAIL
- 관련 scope capture 또는 사진 경로

보드에 revision이나 serial 표시가 없으면 임의로 revision을 만들어내지 말고 `unmarked`로 기록한 뒤 조립 위치를 사용합니다.

권장 CSV header:

```csv
test_id,date,carrier_id,stage_channel,stage_id,firmware_commit,signal,condition,expected,measured,unit,instrument,result,evidence
```

## 0. 정적 계약 검사

```bash
python3 hardware/tools/validate_hardware_contract.py
cd ESP32_B_Algo
sh host_tests/run_tests.sh
cd ..
sh hardware/validation/BAD_JSON/host_tests/run_tests.sh
```

원본 hash, GPIO/J7/J10 정합 또는 host test가 실패하면 실기 출력 시험으로 진행하지 않습니다.

## 1. 무전원 연속성 및 조립

- U3의 DevKit 방향과 board model이 회로 기준과 같은지 확인합니다.
- J7/J10의 pin 1, odd/even 열, 모든 even pin GND를 실물 기준으로 확인합니다.
- J7 각 channel block과 대응 Power Stage J10의 8개 기능을 point-to-point로 확인합니다.
- J1/J2/J3/J6/J8/J9의 극성과 GND 단락 여부를 확인합니다.
- F1 정격과 J4 main switch 경로를 확인합니다.
- 네 Power Stage의 Q1-Q4 marking을 기록해 schematic의 `STP40NF20` 값과 Datasheet 속성 충돌을 해소합니다.
- A-B 외부 3선 UART harness의 `A GPIO39 TX → B GPIO44 RX`,
  `A GPIO40 RX ← B GPIO43 TX`, `A GND ↔ B GND` 연속성을 확인합니다. TX끼리 또는
  RX끼리 연결하지 않고 DevKit bridge 동시 구동 가능성을 확인합니다.

## 2. Logic Carrier 저전압

Power Stage와 72 V를 분리하고 current-limited 5 V/3.3 V 조건에서 수행합니다.

- ESP32_B 미장착, boot, reset, brownout과 power-down에서 J7 네 `CHx_ENABLE`을 측정합니다.
- 펌웨어 초기화의 첫 출력이 `ENABLE LOW`와 `PWM force-low`인지 확인합니다.
- J5 closed/open/disconnected에서 `EN_GLOBAL`과 네 U4 출력의 truth table을 확인합니다.
- GPIO19의 reset/power-up waveform이 U4 enable에 미치는 영향을 측정합니다.
- `EN_GLOBAL`을 LOW로 주입해 falling edge에서 네 U4 출력과 MCU enable이 즉시
  차단되는지 확인합니다. 1~9회 1 ms LOW 뒤 HIGH로 복귀한 경우에는 10회 연속
  HIGH와 새 full command 전까지 off를 유지한 뒤 reset 없이 복구되는지 확인하고,
  10회 연속 LOW에서는 E-Stop latch와 reset challenge 교체를 확인합니다.
- 각 `FAULT_N_CHx`를 한 번에 하나씩 LOW로 주입해 falling edge의 즉시 enable
  차단, 첫 두 번의 1~19회 output sample LOW pulse가 각각 10회 연속 HIGH 후 자동
  복구되는지, 5초 안의 세 번째 falling edge 및 20회 연속 LOW의 latch와 PWM
  force-low, reset 전 재활성화 금지를 확인합니다. 반복 event latch에서는 status
  `fault_code=POWER_STAGE_FAULT`와 해당 채널 `fault=true`도 확인합니다. 동시에 다른
  세 채널의 enable/PWM과 활성 command lease가 중단되지 않는지 확인합니다.
- GPIO3 temperature source가 연결된 cold boot를 반복하고 strapping failure 여부를 기록합니다.
- GPIO38 RGB LED/RMT가 초기화되지 않는지 확인합니다.

## 3. A-B 통신과 fault reset

- A/B 각각 powered/unpowered, USB-UART bridge 연결/분리 상태에서 back-power와 push-pull contention을 확인합니다.
- 115200 8-N-1 장시간 송수신 오류율을 측정합니다.
- B boot 때 GPIO43에 나오는 알려진 ESP32-S3 ROM banner는 `B_RESTARTING`으로
  분류되고, 뒤이은 ADC 포함 JSON status에서 `downstreamHealthy=true`로 복구되는지
  확인합니다. ROM banner 자체를 malformed status로 세지 않습니다.
- B reboot 때 `boot_id`와 `reset_challenge`, A reboot 때 `source_session_id`가 바뀌는지 확인합니다.
- stale boot, stale challenge, replay, unsafe physical input에서 fault가 clear되지 않는지 확인합니다.
- E-Stop 등 전역 fault reset 성공 뒤 새 full actuator command 전까지 전체 출력이
  off인지 확인합니다. 채널 Fault만 reset할 때 정상 채널은 계속 동작하고 복구
  채널은 활성 lease의 다음 출력 주기부터 재적용되는지 확인합니다.
- UART 단선, malformed/oversize JSON, stale sequence, TTL timeout과 task watchdog에서 safe-off를 확인합니다.

## 4. Power Stage logic-only 및 저전압

한 번에 Power Stage 한 장만 해당 J7 channel block에 연결하고 72 V와 PDLC는 연결하지 않습니다.

- `PWM_MAG`, `DIR`, `CHx_ENABLE`의 J7-J10 연속성과 10 kohm input pulldown을 확인합니다.
- `FAULT_N` open-collector와 Logic Carrier pull-up 조합을 확인합니다.
- `RUN_OK = CHx_ENABLE AND FAULT_N`이 두 IRS2104 shutdown을 제어하는지 확인합니다.
- 16 kHz carrier, 60 Hz polarity, 최대 0.60 duty와 direction blanking을 확인합니다.
- 정상 MI 감소 12.0 MI/s와 증가 4.0 MI/s, 출력 task 지연 시 2 ms를 넘지 않는
  단일 slew step을 확인합니다. 이 값은 logic-level 파형 확인 뒤 저전압 dummy
  load에서 과도전류와 ringing을 통과해야 하는 firmware 후보입니다.
- IRS2104 complementary outputs, dead time과 bootstrap refresh를 측정합니다.
- 위 시험을 CH0~CH3의 네 보드에서 각각 반복합니다.

## 5. ADC와 protection 계측

- R9 양단 전압과 `ADC_I_RAW`를 여러 알려진 전류점에서 동시에 측정합니다.
- 명목 0.1 V/A와 실제 slope/offset을 보드별로 계산합니다.
- TLV1701 `FAULT_N` assert/deassert 전류와 hysteresis를 보드별로 측정합니다.
- TH1 근처의 기준 온도와 `ADC_TEMP_RAW`를 여러 점에서 기록합니다.
- 실제 장착 TH1이 설계상 catalog match인 `NTCG203NH103JT1`인지 BOM 또는 실물
  근거로 확인하고, 실제 3.3 V rail과 R14 저항값을 기록합니다.
- 기준 온도와 TabUI의 B25/85=3650 K `T °C (명목)` 차이를 네 보드에서 비교해
  offset/잔차와 허용 범위를 정합니다. 이 보드별 calibration과 HIL이 끝나기 전에는
  명목 °C 변환을 production protection에 사용하지 않습니다.
- ADC 0 V, 정상 최대, open, short-to-GND, short-to-3.3 V, Power Stage-only powered와 ESP32-only powered 조건을 확인합니다.
- Logic Carrier에는 ADC clamp가 없으므로 ESP32 pin 허용 범위를 넘는 주입 시험은 직접 pin에 가하지 않습니다.

## 6. 72 V 및 PDLC 단계

앞 단계가 모두 PASS인 보드만 별도 HV 안전 절차와 enclosure/interlock 아래에서 진행합니다.

- current-limited DC bus와 dummy load에서 한 채널씩 시작합니다.
- `0 -> 0.60`, `0.60 -> 중간 MI`, `중간 MI -> 0` 전환에서 bridge node, filtered
  output의 peak current·overshoot·ringing, rail droop와 Fault pulse를 확인하고
  MOSFET/driver/inductor 온도를 확인합니다.
- 네 채널 동시 시험에서 ground bounce, cross-channel fault/ADC coupling과 connector 온도를 측정합니다.
- PDLC는 filtered J9 `PDLC_A/PDLC_B`에만 연결합니다.
- E-Stop 동작 뒤에도 rail이 live임을 전제로 표시, 방전과 접근 통제를 유지합니다.

## 완료 기준

문서에 값이 없다는 이유로 PASS를 가정하지 않습니다. 각 단계의 실제 측정과 증거가 저장소 또는 연결된 시험 기록에 남아 있을 때만 해당 항목을 완료로 표시합니다.
