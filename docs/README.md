# KUGLASS 문서 안내

공통 시스템 문서는 이 폴더에서 관리합니다.

| 문서 | 내용 |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 컴포넌트 책임, 제어·상태 흐름, 물리 연결과 runtime mode |
| [PROTOCOL.md](PROTOCOL.md) | TabUI↔ESP32_A↔ESP32_B frame, sequence, TTL과 Fault reset |
| [VALIDATION.md](VALIDATION.md) | 변경 범위별 자동 검사와 HIL 핵심 항목 |

문서 역할은 다음처럼 구분합니다.

- 루트 [`README.md`](../README.md): 프로젝트 개요와 빠른 시작
- 루트 [`AGENTS.md`](../AGENTS.md): AI 작업 계약
- 각 컴포넌트 `README.md`: 해당 컴포넌트의 설정, 빌드, 배선과 로컬 검증
- [`hardware/`](../hardware/README.md): as-built 하드웨어 원본, 계약과 실기 절차
- [`개발 계획서.md`](../개발%20계획서.md): 작품 배경, 목표와 제출용 개발 계획

같은 상세 계약을 여러 README에 복제하지 않고, 공통 내용은 이 폴더의 문서로
연결합니다.
