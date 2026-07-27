# feature/cache-telemetry-hud 변경 기록

## 1. 목표와 배경

- 캐시 저장 실패가 기존 정상 bundle까지 손상시키지 않도록 저장 트랜잭션을 강화한다.
- F1 편집기를 열지 않아도 성능, 태양, 구름층, 마칭, 캐시 상태를 한눈에 확인하는 읽기 전용 HUD를 제공한다.
- 표시 단위와 CPU/GPU 측정 구간을 명확히 정의해 숫자를 잘못 해석하지 않게 한다.

## 2. 기존 구현과 관찰된 문제

- `bundle.tmp → bundle`, 기존 `bundle → bundle.old` 디렉터리 교체 과정은 Windows 파일 공유 잠금에 취약했다.
- 저장 실패 시 UI에는 `Error`만 남아 실패 단계와 운영체제 오류를 알 수 없었다.
- F1 Stats의 `CPU frame`은 실제 CPU 작업 시간이 아니라 VSync를 포함할 수 있는 프레임 간격이었다.
- GPU 시간은 구름 draw만 측정했지만 이름과 범위 설명이 상세하지 않았다.
- 구름 높이와 거리는 임의 월드 단위였는데 문맥 없이 km로 표시하면 물리 단위로 오해할 수 있었다.

## 3. 원인 분석과 근거

- 완성된 임시 bundle이 존재한 뒤 교체가 실패한 사례는 texel 직렬화보다 디렉터리 rename 단계가 주요 위험임을 보여준다.
- Direct3D 11 timestamp는 GPU 명령 실행 시간을 측정하지만 CPU의 `QueryPerformanceCounter`와 같은 시간축이 아니다.
- 현재 셰이더는 정규화된 ray의 `t`를 모든 높이·거리·적분에 공유하므로 하나의 월드 스케일 규칙을 정할 수 있다.

## 4. 검토한 대안

- PNG 슬라이스/아틀라스: 3D 볼륨 메타데이터가 없고 재조립 또는 수동 Z 보간이 필요해 런타임 캐시로 제외한다.
- DDS: Direct3D 리소스 컨테이너로 적합하지만 저장 마지막 단계의 파일 잠금을 해결하지는 않는다. 안정화 후 별도 포맷 브랜치에서 도입한다.
- 전체 bundle 디렉터리 교체: 큰 대상의 rename 실패 범위가 넓어 제외한다.
- 불변 세대 디렉터리와 작은 active 포인터: 이전 세대를 보존하면서 활성화 단계만 최소화하므로 채택한다.

## 5. 선택한 해결 방법

- content hash 이름의 불변 bundle을 완전히 기록·검증한 뒤 작은 active 포인터 파일만 교체한다.
- 활성화 실패 시 이전 포인터와 bundle을 그대로 유지하고 단계·오류 코드를 노출한다.
- HUD는 F1 창과 독립적으로 렌더링하며 F2로만 표시를 전환한다.
- `1 world unit = 1 km`를 광역 구름 장면의 표시 규칙으로 정의한다. 흡광·밀도 계수는 여전히 예술적 파라미터다.
- `Frame interval`, `CPU render`, `GPU cloud`를 서로 다른 측정값으로 표시한다.

## 6. 실제 수정 내용

- `NoiseCacheManager`가 generation별 임시 디렉터리에 기록하고 재로드 검증한 뒤 active 포인터를 교체한다.
- `MoveFileExW` 실패에는 Win32 코드와 단계가 포함되며 공유·접근·잠금 오류만 제한적으로 재시도한다.
- 기존 `bundle/` 읽기 폴백을 유지해 배포 기본 캐시와 기존 사용자 캐시를 그대로 사용할 수 있다.
- F1 Stats를 `Frame interval`, `CPU render`, `GPU cloud`, `GPU total`로 분리했다.
- GPU cloud/total은 한 disjoint 구간 안의 timestamp 네 개를 사용하고 결과에는 EMA를 적용한다.
- F2 HUD는 ImGui foreground draw list에 입력 없는 반투명 패널로 합성한다. DPI 환경에서는 D3D 백버퍼 픽셀이 아닌 ImGui `DisplaySize` 좌표로 오른쪽 위를 계산한다.
- 태양은 내부 `+X=0°` 값을 `+Z=북쪽` 나침반 방위각과 N/NE/E 표기로 변환한다.
- HUD에는 프리셋·렌더 모드, FPS/frame, CPU/GPU, 태양, km 구름층, coverage, view/light step, 최대 거리와 캐시 상태를 표시한다.
- 수정 파일: `src/NoiseCacheManager.*`, `src/Renderer.*`, `src/DebugUI.*`, `src/Window.cpp`, `README.md`, `doc/ARCHITECTURE.md`, `doc/RAYMARCHING.md`, `doc/SHADER_CACHE.md`, `doc/PROJECT_STATUS.md`.

## 7. 캐시·호환성·성능 영향

- 기존 `bundle/`은 읽기 호환성을 유지한다.
- DDS 도입 계획은 DirectXTex 정적 링크와 고정 버전 submodule을 사용해 실행 시 추가 DLL이 없도록 한다.
- DDS는 이번 브랜치에서 의존성을 추가하지 않았다. 포맷 변경과 트랜잭션 변경을 분리해 저장 오류의 원인을 독립적으로 검증하기 위해서다.
- HUD의 draw-list 비용과 timestamp query 네 개는 구름 ray march에 비해 작지만 Release 수치로 최종 확인한다.

## 8. 테스트 및 실행 결과

- 2026-07-27 Debug/Release 빌드 성공
- Release `ctest` 2/2 통과
  - `VolumetricCloud.CacheSmoke`: 2.80초
  - `VolumetricCloud.CodeTests`: 112.12초
- cache smoke는 GPU cloud draw를 수행하지 않고 초기화의 compile/dispatch 0회만 검사하도록 분리했다.
- round-trip 테스트에서 새 세대 저장·재로드·바이트 일치, 손상 manifest 거부, 불완전 입력 실패 후 active 포인터 보존을 확인했다.
- 관찰: F1을 열지 않은 상태에서도 구름 렌더는 유지됐다. 자동 캡처 환경의 창 위치 문제로 오른쪽 끝이 잘릴 수 있어 상시 HUD의 최종 위치·가독성과 수치는 사용자 환경에서 승인 대기다.
- 화면 품질 사용자 승인: 대기

## 9. 남은 문제와 후속 개선

- DDS 실제 전환과 `.vcnoise` 마이그레이션은 별도 브랜치로 남긴다.
- 전체 프레임 GPU 타이밍과 temporal percentile 통계는 필요 시 후속 구현한다.
