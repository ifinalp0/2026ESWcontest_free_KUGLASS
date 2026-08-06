# KUGLASS agent entrypoint

모든 작업에서 먼저 [`AGENT.md`](AGENT.md)의 제품 경계와 검증 규칙을 읽습니다. 이 저장소의 제품 펌웨어는 `ESP32_A_Algo/`와 `ESP32_B_Algo/`이며, `For_Test/`는 사용자가 특정 시험을 지목한 경우에만 참조합니다. `Simul_Twin/`은 수정하지 않습니다.

하드웨어 관련 작업에서는 다음 순서를 추가로 지킵니다.

1. `hardware/README.md`
2. `hardware/manifest.json`
3. `hardware/contracts/esp32_b_io.json`
4. `hardware/contracts/power_stage.json`
5. `hardware/contracts/safety.json`
6. 대상 보드 README와 KiCad/PDF 원본

Logic Carrier 1장과 단일 채널 Power Stage 4장은 제작 완료된 as-built 하드웨어입니다. 소프트웨어가 하드웨어에 맞춰야 하며, 사용자가 명시적으로 하드웨어 설계 변경을 요청하지 않은 작업에서는 KiCad 원본, 커넥터와 핀 계약을 변경하지 않습니다. 자료가 충돌하거나 값이 `unknown`이면 추측하지 않습니다.

ESP32_B 핀, ADC, connector 또는 하드웨어 안전 동작에 영향을 주는 변경 뒤에는 다음 검사를 실행합니다.

```bash
python3 hardware/tools/validate_hardware_contract.py
cd ESP32_B_Algo
sh host_tests/run_tests.sh
```
