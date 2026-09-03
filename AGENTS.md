# WeldRev3 Project Instructions

This repository contains firmware and supporting material for a spot-welder
controller. Before changing firmware, read:

- `docs/PROJECT_GOALS.md`
- `docs/HARDWARE.md`
- `docs/DECISIONS.md`
- `docs/STATUS.md`

## Working rules

- PWM 극성, 위상 관계, 데드 타임, ADC 샘플링 위치, 게이트 구동 동작 및 보호 로직을 안전에 매우 중요한 요소로 간주하십시오.

- 전원부를 작동시키거나 작동시킬 권한이 있다고 가정하지 마십시오.

- 의도된 파형을 명시하고 보드 핀 매핑을 확인하지 않고 전원 출력 핀, 극성 또는 안전 시작 상태를 변경하지 마십시오.

- 가능한 경우 STM32CubeMX의 `USER CODE BEGIN/END` 영역을 유지하십시오. 생성된 영역을 변경해야 하는 경우, `docs/DECISIONS.md`에 변경 사항을 기록하고`weld3.ioc` 재생성 위험을 표시하십시오.

- 오류를 우회하기 전에 진단하십시오. SD 초기화 비활성화와 같은 임시 우회는 테스트 전용으로 표시해야 합니다.

- ARM 툴체인을 사용할 수 있을 때 빌드를 통해 펌웨어 변경 사항을 검증하십시오. 타이밍 변경 시에는 예상 오실로스코프 파형도 함께 명시하십시오.

- 관련 없는 사용자 변경 사항은 보존하십시오.

## 문서 업데이트

- 내구성이 있는 설계 선택 사항과 그 근거를 `docs/DECISIONS.md`에 기록하십시오.

- 의미 있는 구현 또는 검증 후 `docs/STATUS.md`를 업데이트하십시오.

- 안정적인 보드 매핑 및 전기적 정보를 `docs/HARDWARE.md`에 기록하십시오.

- 가정이나 제안된 목표를 검증된 사실로 묵묵히 변환하지 마십시오.
