# feature/rebuild-foundation 변경 기록

## 1. 목표와 배경

- 복잡하게 결합된 기존 구름 구현을 보존 브랜치에 격리하고, 단계별 계획의 단계 0부터 다시 구축한다.
- 단계 0의 월드 레이, Scene Depth, 월드 위치와 별도 합성 패스는 사용자 승인을 받았다.
- 단계 1은 상수 밀도 AABB의 교차·폐색·Beer-Lambert 적분을 독립 검증한다.

## 2. 기존 구현과 관찰된 문제

- 기준 `main`은 풀스크린 픽셀 셰이더에서 AABB 교차, 상수 밀도와 Beer-Lambert 적분을 한 번에 수행한다.
- 실제 불투명 장면과 깊이 버퍼가 없어 볼륨이 장면 물체에 가려지는지 검증할 수 없다.

## 3. 원인 분석과 근거

- 장면 색상, Scene Depth와 구름 결과가 분리되지 않아 이후 단계의 교차·밀도 오류를 독립적으로 관찰할 기반이 없다.
- 깊이 역투영이 검증되지 않은 상태에서 레이 마칭을 확장하면 폐색 오류의 원인을 구분하기 어렵다.

## 4. 검토한 대안

- 하늘의 far depth만 사용하는 안은 실제 깊이 복원을 검증할 수 없어 제외했다.
- 단일 도형만 사용하는 안보다 서로 다른 깊이의 평면과 박스가 폐색·복원 오류를 드러내기 쉬워 진단 장면을 선택했다.

## 5. 선택한 해결 방법

- 불투명 평면과 박스를 HDR 색상 타깃과 샘플 가능한 32비트 깊이 타깃에 먼저 렌더링한다.
- 두 타깃을 풀스크린 단계 0 패스에서 읽어 레이 방향, 선형 거리와 월드 위치를 복원한다.
- 최종 모드는 임시 반투명 원판을 `scattering + background * transmittance`로 합성해 별도 경로를 눈으로 검증한다.

## 6. 실제 수정 내용

- `Renderer`를 불투명 진단 장면 패스와 단계 0 풀스크린 패스로 분리했다.
- `R16G16B16A16_FLOAT` 장면 색상과 `D32_FLOAT`/`R32_FLOAT` 공유 깊이 리소스를 추가했다.
- `CloudParameters` 48바이트 구조와 `CloudResult` HLSL 인터페이스를 추가했다.
- 숫자 0~4 디버그 출력과 F5~F7 고정 카메라 프리셋을 추가했다.
- CPU 역투영 회귀 테스트와 숨김 창 D3D11 smoke test를 추가했다.
- 단계 1에서 평행축을 나누지 않는 slab 교차와 Scene Depth 제한을 추가했다.
- 전체 교차 구간을 다시 나눈 상수 밀도 레이 마칭과 고정 산란색을 추가했다.
- 숫자 5~9, F8, Q/W/E/R/T 검증 입력과 현재 상태를 보여 주는 창 제목을 추가했다.
- `Stage1VolumeMath`와 모드 0~9 `Stage1Smoke` 회귀 검사를 추가했다.

## 7. 캐시·호환성·성능 영향

- 기존 noise cache와 프리셋을 읽지 않는다.
- 단계 0은 full-resolution 장면 색상과 32비트 깊이 타깃을 각각 하나 사용한다.

## 8. 테스트 및 실행 결과

- Debug 빌드: 성공
- Release 빌드: 성공
- Release ctest: 2/2 성공 (`FoundationMath`, `FoundationSmoke`)
- Debug D3D11 smoke: 성공, error/corruption 메시지 없음
- HLSL 컴파일: Fullscreen VS, Foundation PS, Diagnostic Scene VS/PS 모두 성공
- 사용자 렌더 승인: 2026-08-02 수동 검증 체크리스트 전체 통과
- 단계 1 Debug/Release 빌드: 성공
- Release CTest: 4/4 성공 (`FoundationMath`, `FoundationSmoke`, `Stage1VolumeMath`, `Stage1Smoke`)
- 단계 1 HLSL: Fullscreen VS, Cloud PS, Diagnostic Scene VS/PS `fxc` 경고 없이 성공
- Debug D3D11: Foundation/Stage1 smoke 반환 코드 0, error/corruption 없음
- 단계 1 사용자 렌더 승인: 2026-08-02 수동 검증 전체 통과

## 9. 남은 문제와 후속 개선

- 단계 0은 `c94825b`로 커밋되어 원격 `feature/rebuild-foundation`에 보존됐다.
- 단계 1은 자동 검증과 사용자 수동 렌더 검증을 모두 통과했다.
- 3D noise, 태양광, phase function과 early exit는 이후 단계로 남긴다.
