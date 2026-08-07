# KUGLASS 통신 계약

이 문서는 TabUI, ESP32_A, ESP32_B가 사용하는 현재 wire contract를 사람이 읽을
수 있게 정리한다. 파서와 serializer의 최종 동작은 각 제품 프로젝트의 코드와
host test가 검증한다. 계약을 변경할 때는 세 컴포넌트, 관련 테스트와 이 문서를
같은 변경에서 갱신한다.

## 공통 규칙

- 제어·상태 record는 한 줄에 하나의 compact JSON을 보내는 JSON Lines 형식이다.
- JSON object의 field 순서는 의미가 없다. 중첩 object parser는 자신의 닫는
  delimiter만 소비하고 다음 상위 field 또는 상위 object 종료를 남겨야 한다.
- 현재 protocol version은 `v=1`이다.
- 채널 ID는 0~3, MI는 유한한 0.0~1.0, enable/fault는 JSON boolean이다.
- CH0~CH3를 요구하는 frame은 네 채널을 정확히 한 번씩 포함해야 한다.
- 누락, 중복, 타입 오류, 범위 이탈 frame은 부분 적용하지 않고 전체 거부한다.
- 각 링크의 sequence 소유자와 재설정 조건을 혼동하지 않는다.

## 물리 링크

| 링크 | 전송 경로 | 용도 |
| --- | --- | --- |
| TabUI↔A | ESP32_A DevKit USB Serial/JTAG CDC | UI 명령, ACK, 상태, on-demand JPEG |
| A↔B | 외부 3선 UART, 115200 8-N-1, TX↔RX 교차 + 공통 GND | actuator command, control, B status |
| 브라우저↔TabUI | 동일 출처 HTTP | 화면, API, 최신 JPEG |

TabUI↔A USB의 JSON Lines와 `KUGLCAM1` JPEG frame은 한 byte stream에서
다중화된다. JPEG는 보조 경로이며 정책 계산과 A→B heartbeat보다 우선하지 않는다.

A↔B 물리 배선은 다음과 같다.

```text
ESP32_A GPIO39 TX -> ESP32_B GPIO44 RX
ESP32_A GPIO40 RX <- ESP32_B GPIO43 TX
ESP32_A GND       --- ESP32_B GND
```

Logic Carrier에는 A↔B UART가 라우팅되지 않는다. 별도 3선 harness를 사용하며
TX끼리 또는 RX끼리 연결하지 않는다.

ESP32_B application보다 먼저 GPIO43에 출력되는 알려진 ESP32-S3 ROM boot
banner는 JSON Lines record가 아니다. A는 이를 `B_RESTARTING`으로 분류해 이전 B
freshness와 reset context를 무효화하며, 유효한 B status로 online을 다시 확인한다.

## TabUI → ESP32_A

모든 frame은 `v=1`, `type=ui_command`, TabUI가 소유하는 단조 증가 `seq`와
명령별 필드를 포함한다.

```json
{"v":1,"type":"ui_command","seq":101,"command":"set_mode","mode":"driving"}
{"v":1,"type":"ui_command","seq":102,"command":"set_demo","demo_mode":"hot_summer"}
{"v":1,"type":"ui_command","seq":103,"command":"manual_channel","channel_id":2,"target_mi":0.42,"ttl_ms":30000,"enable":true}
{"v":1,"type":"ui_command","seq":104,"command":"return_auto","channel_id":2}
{"v":1,"type":"ui_command","seq":105,"command":"camera_stream","enable":true,"ttl_ms":15000}
```

| 명령 | 핵심 필드와 범위 |
| --- | --- |
| `set_mode` | `driving`, `stopped`, `camping`, `parked` |
| `set_demo` | `none`, `hot_summer`, `camping`, `parked`, `camera_saturation` |
| `manual_channel` | channel 0~3, MI 0.0~1.0, `enable` boolean, TTL 1~300초 |
| `return_auto` | 단일 채널 또는 전체 수동 override 해제 |
| `reset_fault` | 최신 B boot/challenge context를 사용한 reset 요청 |
| `camera_stream` | `enable`, 최대 15초 lease |
| `set_environment` | MOCK 또는 양쪽에서 허용된 HIL 전용 |
| `set_channel_fault` | MOCK 또는 양쪽에서 허용된 HIL 전용 |

LIVE 자동 모드에서 TabUI는 저수준 `ch` 배열이나 목표 MI를 계산하지 않는다.

## ESP32_A → ESP32_B actuator command

ESP32_A는 20 Hz heartbeat로 CH0~CH3 전체 목표를 보낸다. 기본 TTL은 250 ms다.

```json
{"v":1,"type":"actuator_command","seq":5501,"ttl_ms":250,"ch":[[0,0.72,true],[1,0.68,true],[2,0.42,true],[3,0.55,true]]}
```

- `seq`는 A가 소유하며 활성 lease 동안 wrap-safe forward 값이어야 한다.
- B의 `ttl_ms` 허용 범위는 50~1000 ms다.
- 활성 lease의 duplicate/stale `seq`는 heartbeat로 인정하지 않는다.
- timeout과 safe-off 뒤에는 첫 유효 full frame에서 sequence 기준을 다시 설정한다.
- invalid frame은 현재 command lease를 무효화하고 B를 safe-off한다.

## ESP32_B → ESP32_A status

B는 100 ms 주기로 독립 sequence를 증가시키며 status를 보낸다.

```json
{"v":1,"type":"status","controller_id":"B","seq":101,"boot_id":305419896,"reset_challenge":2271560481,"estop":false,"fault_code":"NONE","ch":[{"id":0,"mi":0.72,"fault":false},{"id":1,"mi":0.68,"fault":false},{"id":2,"mi":0.42,"fault":false},{"id":3,"mi":0.55,"fault":false}],"adc":{"initialized":true,"i_cali":true,"t_cali":true,"raw_valid_mask":255,"mv_valid_mask":255,"i_raw":[120,121,119,122],"t_raw":[2010,2002,2021,1998],"i_mv":[28,29,28,29],"t_mv":[1620,1614,1628,1611]}}
```

- `seq`는 actuator command sequence가 아니라 B status 자체의 순서다.
- `boot_id`는 B 부팅 동안 고정되는 nonzero u32다. A는 같은 boot 안에서만
  status sequence를 비교하고 새 boot를 받으면 기준을 즉시 재설정한다.
- `reset_challenge`는 nonzero u32 one-time reset 권한이다. safety trip 또는
  유효 reset 시도 후 교체된다.
- `ch[].mi`는 B가 실제 적용 중인 값이며 safe-off 시 0.0이다.
- `FAULT_N` falling edge는 해당 채널 출력을 즉시 차단한다. 1 ms 출력 주기에서
  5회 연속 LOW가 확인된 Power Stage Fault만 reset-required latch로 확정되어 해당
  `ch[].fault`에 표시되고 그 채널의 `mi`만 0.0이 된다. 그 전에 HIGH로 복귀한
  glitch는 자동 복구한다. `fault_code`는 E-Stop, 통신과 명령 오류처럼
  controller-wide 차단 원인을 나타내며, 정상인 나머지 채널은 활성 command
  lease를 계속 따른다.
- `diagnostic`, `adc`, `control_result`는 선택 필드다.
- canonical B formatter는 `adc`를 마지막 top-level field로 출력한다. A는 이
  순서를 포함해 유효한 JSON object field 순서에 의존하지 않는다.
- ADC validity mask의 bit 0~3은 CH0~CH3 current, bit 4~7은 temperature다.
  raw/mV를 보정된 A/°C로 해석하지 않는다.

ESP32_A는 version, type, controller, boot/challenge, E-Stop/Fault와 채널 집합을
모두 검증한 status만 TabUI 상태와 freshness에 반영한다.

## Fault reset

ESP32_A는 부팅마다 nonzero u32 `source_session_id`를 만들고 최신 B status의
`boot_id`와 `reset_challenge`를 대상으로 control frame을 보낸다.

```json
{"v":1,"type":"control","seq":9001,"source_session_id":2712847316,"target_boot_id":305419896,"reset_challenge":2271560481,"command":"reset_fault"}
```

B는 대상 boot, one-time challenge, 실제 `EN_GLOBAL/FAULT_N` 상태와 새 safety
event 부재를 확인한다. matching 요청이 안전하지 않아 실패하더라도 challenge를
소비하여 같은 frame이 나중에 replay되어 fault를 지우지 못하게 한다. E-Stop이나
controller-wide fault reset 성공 뒤에는 새 actuator full frame 전까지 전체 출력이
off다. 채널 Fault만 reset한 경우에는 정상 채널의 활성 lease를 유지하고, 복구된
채널은 그 lease의 다음 출력 주기부터 다시 적용한다.

결과는 다음 status의 `control_result`로 보고한다.

```json
{"command":"reset_fault","seq":9001,"source_session_id":2712847316,"ok":true,"error":"NONE"}
```

- A는 B `boot_id`, A `source_session_id`, request `seq`가 pending 요청과 모두
  일치하는 결과만 최종 ACK한다.
- UART write 완료는 reset 성공이 아니다.
- 현재 1,500 ms 내 일치하는 결과가 없으면 `B_RESET_TIMEOUT`으로 실패한다.
- B 결과 error는 `NONE`, `RESET_UNSAFE`, `TARGET_BOOT_MISMATCH`,
  `CHALLENGE_MISMATCH`다.

## ESP32_A → TabUI 상태

ESP32_A USB 링크의 주요 record는 다음과 같다.

| type | 의미 |
| --- | --- |
| `boot` | A 역할과 진단 명령 허용 여부 |
| `ack` | UI command sequence의 수락·거부 또는 완료 결과 |
| `state` | 센서 품질, 정책, CH0~CH3 target/commanded 상태 |
| `status` + `controller_id=B` | 검증을 통과한 B applied MI, Fault, ADC, reset 결과 |
| `protocol_error` | A 또는 B 링크의 형식·계약 오류 |

`target_mi`, `commanded_mi`, `applied_mi`는 서로 다른 상태다. TabUI는 유효한
B status를 받기 전까지 적용값을 추정하지 않는다.
TabUI의 현재 link error는 뒤이은 A `state.downstream`으로 갱신되며, 감사 로그처럼
과거 `protocol_error` 한 건을 영구 latch하지 않는다.

## Lease와 실패 처리

- 수동 명령 TTL과 A→B heartbeat TTL은 별개의 lease다.
- TabUI가 끊겨도 ESP32_A의 AUTO 정책은 계속 실행된다.
- ESP32_A heartbeat가 끊기면 ESP32_B가 local safe-off한다.
- LIVE telemetry가 stale이면 TabUI는 마지막 실제 값을 표시하되 명령을 거부한다.
- MOCK/REPLAY frame은 production USB나 Power Stage 명령 경로로 전달하지 않는다.
