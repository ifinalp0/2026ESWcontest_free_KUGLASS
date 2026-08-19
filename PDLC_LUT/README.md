# KUGLASS PDLC LUT Lab

ESP32_A에 연결된 OV2640 카메라를 이용해 PDLC의 MI별 **카메라 상대 광응답**을
수집하고 LUT 후보를 만드는 독립 웹 도구다. CH0~CH3에서 PDLC_LUT 전용 Lab MI
`0.00~1.00`을 지원하며 기존 ESP32_A USB CDC 프로토콜을 그대로 사용한다.

ESP32_A의 실제 wire 범위는 controller MI `0.00~0.60`이므로 웹은 다음처럼
선형 매핑한다.

```text
controller MI = Lab MI × 0.60
```

따라서 웹의 Lab MI `1.00`이 ESP32_A에 전송되는 controller MI `0.60`에 해당한다.
PDLC_LUT 밖의 펌웨어·프로토콜 MI 범위는 변경하지 않는다.

이 도구가 계산하는 값은 표준 총광선투과율, 헤이즈 또는 Vrms가 아니다. JPEG 영상의
green 채널과 동일 프레임의 reference ROI를 이용한 상대 추정치다. 기준 계측기와
교차검증하기 전에는 production 물리값으로 사용하지 않는다.

## 제공 기능

- Chrome/Edge Web Serial로 ESP32_A USB CDC에 직접 연결
- ESP32_A의 `KUGLCAM1` JPEG binary frame 파싱과 FNV-1a 무결성 검사
- reference ROI와 PDLC ROI의 동일 프레임 비교
- 암전(dark) 및 무시료(blank) 보정, 1~15 frame 중앙값 집계
- CH0~CH3 Lab MI `0~1` 단일점 측정과 상승/하강 자동 sweep
- 상대 투과, clarity proxy, 반복편차, 포화/암부 경고 표시
- CSV, 재가져오기 가능한 JSON, C++ LUT header 내보내기
- PAVA 단조 회귀로 측정 노이즈가 포함된 응답을 단조 LUT 후보로 정규화
- 카메라 없이 화면 흐름을 확인하는 데모 데이터와 이미지 업로드

데모 점은 화면에서 회색으로 표시되며 CSV/JSON/C++ 실측 export에서 제외된다.

## 요구사항과 연결

- Node.js `>=22.13.0`
- Web Serial을 지원하는 데스크톱 Chrome 또는 Edge
- 현재 `ESP32_A_Algo` 펌웨어가 플래시된 ESP32_A
- 데이터 통신이 가능한 micro-USB 케이블
- PDLC 경로와 bypass reference 경로를 같은 프레임에 배치한 고정 광학 지그
- 물리 E-Stop과 저전압부터 검증된 출력 장치

USB 연결은 제품 계약과 동일하다.

```text
MacBook -> 데이터 micro-USB -> ESP32_A DevKit USB 단자
```

외부 GPIO UART를 추가하지 않는다. 브라우저와 TabUI는 같은 serial port를 동시에
열 수 없으므로 측정 중에는 TabUI LIVE를 종료한다.

## 실행

```bash
cd PDLC_LUT
npm install
npm run dev
```

브라우저에서 `http://localhost:3000`을 연다. 배포된 HTTPS 페이지에서도 Web
Serial을 사용할 수 있지만, 브라우저가 장치 선택 권한을 매번 요구할 수 있다.

검증 명령은 다음과 같다.

```bash
npm run typecheck
npm run lint
npm test
```

## 권장 측정 지그

한 장면 안에 광원 변동을 추적할 reference 경로와 PDLC를 통과하는 sample 경로를
동시에 둔다. 두 ROI의 조명, 거리, 렌즈 비네팅, 배경 재질은 가능한 한 동일해야 한다.

```text
확산 광원
   ├─ bypass/reference aperture ─┐
   └─ PDLC/sample aperture ──────┤ -> OV2640 고정 카메라
```

카메라의 AEC/AGC/AWB는 현 ESP32_A wire command로 잠글 수 없다. 같은 프레임의
reference 비율로 전체 노출 변화의 영향을 줄일 수는 있지만, 두 ROI의 국부 포화나
색 변화까지 제거할 수는 없다. 보정·sweep 동안 광원, 지그, 카메라 위치를 고정한다.

## 측정 순서

1. `USB 연결`을 눌러 ESP32_A port를 선택하고 `Camera lease 요청`으로 영상을 확인한다.
2. 광원을 끄거나 두 ROI를 완전히 가린 뒤 `암전 기준 측정`을 실행한다.
3. 광원을 켜고 sample 경로에서 PDLC를 제거한 뒤 `무시료 기준 측정`을 실행한다.
4. PDLC를 sample 경로에 설치하고 ROI가 서로 겹치지 않도록 조정한다.
5. 하드웨어 안전 조건을 확인하고 `안전 조건 확인`을 선택한다.
6. Lab MI `0~1` 단일점을 측정하거나 상승/하강 자동 sweep을 실행한다.
7. 경고와 반복편차를 검토한 뒤 CSV/JSON을 보관하고 필요하면 C++ header를 만든다.

자동 sweep은 각 점에서 `manual_channel` 명령의 ACK를 기다리고 settle time 이후 여러
frame의 중앙값을 기록한다. 종료 또는 중단 시 해당 채널에 MI 0, `enable=false`를
보낸다. 그러나 USB 단절이나 브라우저 정지는 하드웨어 차단 장치가 아니므로 물리
E-Stop을 항상 사용한다.

## 계산 의미

각 ROI의 8-bit sRGB green 값을 설정 gamma(기본 `2.2`)로 선형화한 뒤 다음과 같이
계산한다.

```text
ratio       = (PDLC_green - PDLC_dark) / (REF_green - REF_dark)
relative_T  = ratio / blank_ratio
```

`blank_ratio`는 PDLC를 제거한 무시료 보정의 같은 비율이다. 따라서 `relative_T=1`은
무시료 경로와 같은 카메라 응답이라는 뜻이지, 절대 투과율 100%를 보증하지 않는다.
clarity proxy는 ROI의 Michelson contrast를 무시료 contrast로 나눈 참고값이다.
헤이즈 측정이 아니며 장면 패턴과 초점에 의존한다.

LUT 화면은 같은 Lab MI의 반복 측정 중앙값을 취하고, MI 증가에 따라 응답이 감소하지
않도록 PAVA 단조 회귀를 적용한 후 실측 최소/최대 사이를 `0..1`로 정규화한다.
C++ export는 `{optical_response, lab_mi, controller_mi}` 후보 테이블이며 기존
ESP32_A LUT를 자동으로 변경하지 않는다. CSV와 JSON도 두 MI 값을 함께 기록한다.

## ESP32_A 프로토콜 사용 범위

웹은 newline-delimited `v=1`, `type=ui_command`, 단조 증가 `seq`를 전송한다.

- 영상: `command=camera_stream`, 최대 15초 lease를 약 10초마다 갱신
- 출력: 웹의 Lab MI `0..1`을 `target_mi=0..0.60`으로 변환한
  `command=manual_channel`, `channel_id=0..3`
- 복귀: `command=return_auto`

카메라 binary header는 magic `KUGLCAM1`, 28-byte little-endian header, JPEG payload,
FNV-1a 32-bit checksum으로 처리한다. 프로토콜이 변경되면
`lib/esp32-serial.ts`와 이 문서를 함께 갱신한다.

## 주요 파일

- `components/LutLab.tsx`: 연결, 보정, sweep, 결과 UI와 session 저장
- `lib/esp32-serial.ts`: ESP32_A mixed JSONL/binary serial parser
- `lib/optics.ts`: ROI 통계, 상대 응답, 반복 집계, 단조 LUT 계산
- `tests/rendered-html.test.mjs`: 빌드 결과와 핵심 프로토콜 제약 검증

측정점, 설정, ROI는 해당 브라우저의 `localStorage`에 저장된다. 기존 v1 세션과
schema v1 JSON의 controller MI는 불러올 때 Lab MI `0..1`로 자동 변환한다.
암전·무시료 보정은
광학 조건이 달라진 뒤 재사용되지 않도록 페이지를 열 때마다 다시 측정해야 한다.
JSON export를 측정 원본과 함께 별도로 보관한다.
