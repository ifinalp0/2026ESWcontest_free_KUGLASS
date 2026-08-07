# ESP32_A↔ESP32_B UART `BAD_JSON` 장애 기록

## 기록 정보

| 항목 | 값 |
| --- | --- |
| incident ID | `HIL-AB-UART-20260807-001` |
| 상태 | `OPEN` — software root cause 수정 완료, flash/HIL 재검증 대기 |
| 관측 구간 | 2026-08-07 00:56~01:00 KST |
| 관측자 | Codex의 read-only 소프트웨어/API 관측; 물리 작업자 미기록 |
| 저장소 branch / HEAD | `main` / `90a8946922cc8a6110a9d9b5c6024db48b6f5bb0` |
| 관측 당시 worktree | clean |
| 실행 중 A/B firmware commit | `unknown` — 저장소 HEAD와 플래시 이미지의 동일성 미검증 |
| Logic Carrier / Power Stage 식별자 | `unknown` — 이번 관측에서 실물 식별자 미수집 |
| ESP32_A DevKit 식별 | USB VID:PID `303A:1001`, serial `1C:DB:D4:9D:8E:14`, `/dev/cu.usbmodem3101` |
| ESP32_A/B DevKit revision | `unknown` |
| 전원 조건·current limit | `unknown` |
| 계측기·scope·logic analyzer | 사용하지 않음 |
| 관련 구조화 snapshot | [`observation.json`](observation.json) |

이 기록은 TabUI가 ESP32_A USB telemetry를 통해 보고한 A↔B 상태를 보존한
것이다. 사용자의 지시에 따라 외부 3선 UART harness 배선은 정상이라고 가정하지만,
이번 관측에서 연속성·핀 위치·GND 전위는 다시 측정하지 않았다. 따라서 배선 상태를
`PASS`로 승격하지 않는다.

## 관측 당시 결론

ESP32_A는 MacBook/TabUI와 정상적으로 통신했지만, ESP32_A↔ESP32_B 링크에서는
유효한 B status가 한 번도 확인되지 않았다. TabUI의 지속 상태는 다음과 같았다.

```text
ESP32_A USB                 CONNECTED
ESP32_A telemetry           LIVE / 계속 갱신
ESP32_A command ACK         정상 확인
ESP32_A↔ESP32_B healthy     false
ESP32_A가 보고한 오류       BAD_JSON
B boot_id/status seq        미수신(null)
B applied MI/E-Stop/Fault   확인 불가
```

현재 상태를 단순한 `B_STATUS_TIMEOUT` 또는 `UART_TX_READY/WAITING_B`로 기록하지
않는다. `BAD_JSON`은 ESP32_A의 UART line accumulator가 newline으로 끝난 한 줄을
완성한 뒤 B status parser가 그 줄을 거부했을 때 TabUI에 전달되는 오류다. TabUI는
다음 유효 B status가 올 때까지 이 오류를 latch한다. 다만 ESP32_A는 거부한 원문을
TabUI로 전달하지 않으므로, 한 번의 부팅 잡음인지 반복되는 손상 frame인지는 현재
snapshot만으로 구분할 수 없다.

## 코드 교차 검증으로 확인한 canonical 원인

후속 분석에서 raw UART를 추측하지 않고 canonical B formatter의 실제 출력을 A
parser에 직접 넣는 cross-project host regression을 추가했다. 이 검사로 canonical
source에서 관측을 완전히 설명하는 `BAD_JSON` software 결함을 재현했다. 다만 관측
당시 보드의 실제 flash image와 거부 원문은 미확인이므로, 이 결함이 실기 사건의
유일한 원인이었다고 확정하지는 않는다.

1. B의 `format_status_line()`은 정상 ADC 포함 status를 만들 때 `adc` object를
   마지막 top-level field로 둔다.
2. 기존 A `parse_adc()`는 ADC object의 `}`를 이미 소비한 뒤, 바로 뒤에 top-level
   `}`가 있으면 그것까지 중첩 parser 안에서 소비했다.
3. top-level status parser는 자신의 닫는 `}`를 찾지 못해 정상 B status 전체를
   `BAD_JSON`으로 거부했다. 따라서 `boot_id`, status `seq`, applied MI가 한 번도
   갱신되지 않은 관측 결과와 정확히 일치한다.
4. 기존 A host fixture는 `adc` 뒤에 `future` field를 추가해 두었기 때문에 잘못된
   두 번째 `}` 소비 경로를 지나지 않았고, B formatter test는 결과를 A parser에
   넣지 않아 양쪽 test가 각각 PASS하면서도 결함을 놓쳤다.
5. TabUI는 첫 B `protocol_error`를 유효 B status가 올 때까지 latch했다. 모든
   ADC 포함 status가 위 결함으로 거부되었으므로 화면의 `BAD_JSON`도 계속 유지됐다.

ESP32-S3의 첫 단계 ROM boot banner가 GPIO43에 비-JSON으로 출력되는 사실은 별도
혼입 요인이다. canonical config는 second-stage bootloader와 app console을 끄지만
기본 ROM log는 eFuse 상태를 영구 변경하지 않는 한 출력된다. 이 banner는 위 ADC
parser 결함의 원인은 아니지만 B reboot 때 같은 오류를 다시 만들 수 있어 함께
분리 처리했다.

### 적용한 수정

- A의 nested channel/array/ADC/control-result parser가 자신의 닫는 delimiter만
  소비하도록 수정했다.
- 실제 B formatter의 ADC 포함 status를 A parser에 넣고, 앞에 ESP32-S3 ROM
  banner를 붙인 stream까지 검사하는
  [`host_tests/run_tests.sh`](host_tests/run_tests.sh)를 추가했다.
- A는 알려진 ROM banner를 `BAD_JSON`이 아닌 `B_RESTARTING`으로 분류하고 이전 B
  freshness와 Fault reset context를 즉시 무효화한다.
- 실제 parser 오류는 A state에도 보존하며, stale status가 아니라 다음 유효한
  forward status에서만 해제한다.
- TabUI는 과거 protocol error 한 건을 영구 상태로 고정하지 않고 다음 A
  `state.downstream`의 현재 health/error로 갱신한다.
- B build는 runtime console, second-stage bootloader log 비활성화와 기본 ROM log
  eFuse 상태를 compile-time에 확인한다. ROM log를 끄기 위한 영구 eFuse 변경은
  사용하지 않는다.

이 수정은 저장소의 software 재현을 해결했지만, 관측 장치에 새 A/B image를
플래시하고 logic-only 10분 실측을 완료하지 않았다. 따라서 incident 상태는
`RESOLVED`로 올리지 않는다.

### 수정 후 비실기 검증

2026-08-07 `Kill_Bad_JSON` branch의 미커밋 working tree에서 다음 결과를
확인했다.

| 검사 | 결과 |
| --- | --- |
| `python3 hardware/tools/validate_hardware_contract.py` | PASS (`hardware contract ok`) |
| `ESP32_A_Algo/host_tests/run_tests.sh` | PASS |
| `ESP32_B_Algo/host_tests/run_tests.sh` | PASS |
| `hardware/validation/BAD_JSON/host_tests/run_tests.sh` | PASS (`A-B UART ROM-noise/status compatibility ok`) |
| `TabUI`의 `npm run check && npm run build` | PASS (Python 33 tests, TypeScript, Vite build) |
| ESP-IDF 6.0.2 ESP32_A target build | PASS (`kuglass_esp32_algo.bin`, `0x47920` bytes, SHA-256 `3005b57215c847301c526ee6170ee1dbbd2f809b150f8e2763e39269cf968f89`) |
| ESP-IDF 6.0.2 ESP32_B target build | PASS (`kuglass_esp32_b.bin`, `0x3a340` bytes, SHA-256 `bbef9938fe8051a24925bcf5319850e4cee0cdd67776c792747d8877343292b1`) |

이 표는 source와 build artifact의 비실기 정합만 증명한다. 실제 보드의 flash
image, UART 파형, 전원·reset 상태와 장시간 error count는 여전히 미검증이다.

## 직접 관측 사실

1. TabUI Python backend가 PID `91739`로 TCP `*:8080`에서 실행 중이었다.
2. backend는 `/dev/cu.usbmodem3101`을 열고 있었고 transport는 `usb`,
   `hardwareConnected=true`였다.
3. 해당 USB 장치는 `USB JTAG/serial debug unit`, VID:PID `303A:1001`, serial
   `1C:DB:D4:9D:8E:14`로 열거되었다. 별도의 `/dev/cu.usbmodem502NTWG146572`는
   `LG Monitor Controls`이며 ESP32가 아니다.
4. A의 `lastTelemetryAt`은 2초 간격 재조회에서 계속 증가했다. 카메라 frame ID와
   DS18B20 내부온도도 갱신되어 A main runtime이 살아 있음을 확인했다.
5. 마지막 `camera_stream` command의 command seq와 ACK seq는 모두
   `3618857342`, `lastAckOk=true`였다. 이 사실은 TabUI↔A 양방향 USB 경로가
   동작했음을 지지한다.
6. A가 보고한 B 상태는 반복 조회 동안 `downstreamHealthy=false`,
   `downstreamError=BAD_JSON`으로 유지되었다.
7. `downstreamBootId`, `downstreamStatusSeq`, `downstreamResetChallenge`,
   `downstreamEstop`, `downstreamFaultCode`, `downstreamDiagnostic`, ADC와
   `controlResult`는 모두 유효한 B status에서 갱신되지 않았다.
8. UI의 네 채널은 `appliedMi=0.0`이었지만 동시에 `appliedKnown=false`였다.
   따라서 0.0을 B의 실제 출력 측정값이나 safe-off 완료 증거로 해석하지 않는다.
9. `downstreamOperationalFault=false`도 정상 안전 입력의 증거가 아니다. 유효한 B
   status가 없으므로 E-Stop/Fault 상태 자체가 알려지지 않았다.

## 정적 검증 결과

관측 당시 저장소 HEAD에서 다음 검사를 수행했다.

| 검사 | 결과 | 해석 범위 |
| --- | --- | --- |
| `python3 hardware/tools/validate_hardware_contract.py` | PASS (`hardware contract ok`) | 저장소 계약·핀맵 정합 |
| `ESP32_A_Algo/host_tests/run_tests.sh` | PASS | A parser, B status/session/reset correlation, JSON line accumulator 등 |
| `ESP32_B_Algo/host_tests/run_tests.sh` | PASS | B formatter/parser 및 app host link |

관측 당시 canonical source는 양쪽 UART1을 115200 8-N-1/no-flow-control로 구성하며
A TX/RX는 GPIO39/40, B TX/RX는 GPIO43/44다. B의 `sdkconfig.defaults`는 bootloader
log와 runtime console을 끄도록 설정되어 있고 B status formatter는 100 ms마다
JSON Lines status를 만들도록 구현되어 있다. 여기서 bootloader log는 second-stage
log이며 첫 단계 ESP32-S3 ROM banner는 별개다.

이 PASS는 현재 저장소 소스끼리 정합한다는 뜻일 뿐, 실제 A/B에 이 HEAD로 빌드한
이미지가 플래시되었다거나 실기 UART가 정상이라는 증거가 아니다.

## 관측 당시 추정 원인

아래 목록은 cross-project 재현 전, raw line이 없던 시점의 진단 후보를 보존한
것이다. 위의 canonical software 결함을 먼저 수정했지만, flash/HIL 재검증 전에는
나머지 실기 후보를 완전히 배제하지 않는다.

우선순위는 현재 증거와 canonical code의 알려진 경계에 따른 진단 순서이며,
발생 확률을 측정한 값은 아니다.

### P1 — 우선 확인

1. **ESP32_B에 오래되었거나 다른 firmware가 플래시됨**
   - 현재 A parser가 요구하는 `v/type/controller_id/boot_id/reset_challenge/ch`와
     다른 schema 또는 일반 로그를 B TX로 내보내면 현재 증상과 일치한다.
   - `For_Test/` 또는 과거 firmware가 올라간 경우도 포함한다.
   - 실제 플래시 이미지의 app description/commit/hash가 기록되지 않아 현재 가장
     먼저 배제해야 한다.

2. **ESP32_B build가 canonical `sdkconfig.defaults`와 다름**
   - 현재 canonical 설정은 B console과 bootloader log를 끈다. 실제 build가 UART
     console/log를 GPIO43에 출력하면 JSON Lines와 로그가 섞이거나 A가 일반 로그를
     B status로 해석해 `BAD_JSON`을 낼 수 있다.
   - 이전 build cache나 다른 `sdkconfig`가 재사용된 경우를 포함한다.

3. **ESP32_B의 부팅 실패·reset loop·brownout·watchdog reset**
   - B가 정상 status task까지 도달하지 못하면 nonzero `boot_id`와 증가하는
     `status seq`가 나오지 않는다.
   - 현재 B 코드는 safety input 설정 또는 UART 초기화가 실패하면 safe-off 후
     runtime task 생성 전에 반환한다. 전원 불안정이나 반복 reset도 같은 외형을
     만들 수 있다.
   - 부팅 과정에서 나온 일부 byte만 A가 newline frame으로 받아 `BAD_JSON`을
     latch했을 가능성이 있다.

4. **ESP32_B GPIO43/44와 DevKit USB-UART bridge의 push-pull contention**
   - 저장소 하드웨어 계약이 이미 실기 확인 필요 위험으로 명시한 항목이다.
   - harness가 정확해도 B DevKit bridge 또는 연결된 USB 장치가 같은 TX/RX 선을
     동시에 구동하면 byte corruption, idle-level 이상 또는 수신 불능이 생길 수 있다.

### P2 — P1과 함께 계측

5. **실행 중 firmware의 baud/data format 불일치**
   - canonical source는 양쪽 모두 115200 8-N-1이지만 실제 플래시 이미지가 다른
     baud, parity, stop bit 또는 UART 번호를 쓰면 wiring이 정상이어도 JSON이 깨진다.

6. **A/B firmware 버전 불일치로 인한 wire schema 차이**
   - 과거 B가 `boot_id`, `reset_challenge`, strict CH0~CH3 형식 또는 ADC 필드를
     다르게 출력하면 A가 frame을 거부할 수 있다.
   - 누락 필드는 보통 더 구체적인 parser error가 나지만, JSON 자체가 잘리거나
     형식이 과거와 크게 다르면 `BAD_JSON`도 가능하다.

7. **B status line의 손상·절단·byte 혼입**
   - 전원 잡음, 긴 jumper, 주변 switching noise, ground bounce, bridge contention
     또는 송수신 task/driver 문제가 newline 이전 데이터를 손상할 수 있다.
   - 이는 “핀과 선이 올바르게 연결됨”과 양립할 수 있는 신호 무결성 문제다.

8. **B UART 초기화 실패 또는 status task 미생성**
   - `uart_param_config`, `uart_set_pin`, `uart_driver_install`, mutex/event allocation,
     watchdog 설정 또는 `xTaskCreate` 실패 가능성이다.
   - 정상 firmware라면 status task가 100 ms마다 송신해야 하므로, logic analyzer에서
     지속 traffic 자체가 없다면 이 분기를 우선 조사한다.

9. **B status formatter 실패 또는 runtime memory 손상**
   - canonical formatter와 host test는 현재 schema를 정상 생성하지만, 실기에서
     buffer 손상·stack 문제·메모리 corruption이 생기면 잘린 JSON이 전송될 수 있다.
   - current code의 status buffer와 A receive buffer는 모두 1024 bytes이므로 정상
     formatter 출력 크기 자체가 원인일 가능성은 낮다.

### P3 — 위 항목 배제 후 확인

10. **ESP32_A의 실행 이미지가 현재 parser와 다르거나 손상됨**
    - TabUI↔A는 동작하지만, A의 B parser/line accumulator만 과거 버전이거나 runtime
      memory 문제가 있을 수 있다. 현재 HEAD host test PASS만으로 플래시된 A를
      증명할 수 없다.

11. **빌드 시 UART GPIO macro override 또는 잘못된 target/config 사용**
    - source default는 맞아도 build flag가 A/B UART GPIO를 재정의했거나 B가
      ESP32-S3 canonical profile이 아닌 설정으로 빌드되었을 수 있다.

12. **일시적인 B boot byte 한 줄 이후 B TX가 완전히 정지함**
    - `BAD_JSON`은 TabUI에서 latch되므로 현재도 잘못된 frame이 반복된다는 뜻은
      아니다. raw byte 또는 protocol error 발생 횟수를 기록하지 않았기 때문에,
      과거 한 번의 오류와 지속 오류를 아직 구분할 수 없다.

사용자 지시의 가정에 따라 TX/RX 역결선, 단선, GPIO 번호 오인, 공통 GND 누락은
원인 목록의 우선 후보에서 제외했다. 다만 해결 판정 전에는 연속성 결과를 별도
시험 기록으로 남겨야 한다.

## 다음 진단에서 반드시 수집할 증거

1. B에 실제 플래시된 project, ESP-IDF version, app version, build timestamp,
   image SHA-256와 대응 Git commit.
2. B reset reason, brownout/watchdog 여부, 3.3 V/5 V rail의 min/max와 reset 파형.
3. B GPIO43 TX의 idle 전압과 115200 decode 원본. 최소한 power-on부터 10초간 raw
   byte, timestamp, framing-error 수를 저장한다.
4. A GPIO40 RX에서 동시에 본 waveform/decoded byte. B GPIO43과 비교해 선로에서
   손상되는지 구분한다.
5. B DevKit USB-UART bridge 연결/분리 각각의 결과와 GPIO43/44 contention 전압.
6. A가 거부한 원문 line과 길이. 가능하면 진단 build에서 payload 자체 대신 길이,
   byte count, checksum, parser error count를 기록해 로그 혼입과 절단을 구분한다.
7. 유효 frame이 나오면 `boot_id`, `reset_challenge`, 최초/최종 `status seq`, 100 ms
   주기 jitter와 10분 이상 error count.
8. Logic Carrier, 두 DevKit과 전원 장치의 실제 식별자·revision·사진 경로.

## 권장 재진단 순서

1. Power Stage, PDLC와 HV를 분리하고 logic-only/current-limited 조건을 유지한다.
2. harness를 구동하지 않는 high-impedance logic analyzer로 B GPIO43과 A GPIO40을
   동시에 capture한다. 먼저 traffic 유무와 실제 baud를 확인한다.
3. B image metadata를 읽고 canonical `ESP32_B_Algo`와 다르면 ESP-IDF 6.0.2,
   clean build, canonical `sdkconfig.defaults`로 다시 빌드·플래시하며 image hash를
   기록한다.
4. B power/reset 원인을 기록하고 100 ms status 송신 여부를 확인한다.
5. B DevKit bridge를 안전하게 분리한 조건과 연결한 조건을 비교한다. GPIO43/44에
   두 push-pull source를 임의로 동시에 연결하지 않는다.
6. raw line을 current A parser 요구 필드와 비교한다. schema 차이는 A/B/TabUI와
   protocol 문서를 같은 변경에서 맞춘다.
7. `downstreamHealthy=true`, error `NONE`, nonzero boot/challenge, 증가하는 status
   seq와 `appliedKnown=true`를 확인한 뒤 fault/reset과 장시간 UART 시험으로 간다.

ESP32_B에서 native USB Serial/JTAG를 임의로 켜서 debug하지 않는다. GPIO19는
Logic Carrier의 `EN_GLOBAL` input-only 계약이므로, debug 편의를 위해 USB/JTAG로
재구성하면 안 된다.

## 해결 완료 기준

- B status가 100 ms 주기로 안정적으로 수신되고 `downstreamHealthy=true`와
  `downstreamError=NONE`이 유지된다.
- nonzero `boot_id`/`reset_challenge`가 있고 같은 boot에서 `status seq`가 증가한다.
- CH0~CH3가 full/unique로 수신되고 `appliedKnown=true`가 된다.
- 최소 10분 logic-only run에서 malformed/oversize/stale status와 framing error가
  0이며, 관측된 주기와 byte/error count가 증거 파일에 남는다.
- B power cycle 후 새 `boot_id`를 A가 받아 sequence 기준을 정상 재설정한다.
- 원인이 확정되고 수정 내용, 플래시 image hash, 계측 조건과 재검증 결과가 이
  incident에 추가된 뒤 상태를 `RESOLVED`로 바꾼다.
