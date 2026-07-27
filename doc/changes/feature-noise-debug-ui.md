# feature/noise-debug-ui 변경 기록

## 1. 목표와 배경

초기 AABB 안개 레이마처를 반복 가능한 구름 개발 환경으로 확장하는 브랜치다. 절차식 periodic noise, GPU 3D 볼륨 캐시, 영구 시작 캐시, ImGui 진단 UI와 단일 산란 조명을 한 단계에서 구축했다.

## 2. 기존 구현과 관찰된 문제

변경 전에는 AABB 안의 상수 밀도를 64회 적분하는 안개만 존재했다. 구름 형태, 셀프섀도우, 파라미터 조절, 노이즈 단면 검사와 빠른 재시작 수단이 없었다.

현재 결과는 구름이 렌더되지만 큰 흐린 덩어리로 보이고 내부 조명 대비가 약하다. base 볼륨은 128³ R8 단일값이고 detail은 64³ RGBA8이므로, base 형태의 여러 주파수를 렌더 시점에 다시 조합할 수 없다.

## 3. 원인 분석과 근거

- `CloudNoise.hlsli`는 periodic Value/Worley/FBM과 Perlin-Worley를 동일한 좌표 규약으로 평가한다.
- `NoiseVolumeCS.hlsl`은 base 실루엣 하나와 detail Worley 옥타브 네 개를 굽는다.
- 메인 셰이더는 64 view step과 최대 12 light step으로 Beer–Lambert 및 HG 단일 산란을 누적한다.
- `.cso`와 볼륨을 캐시하지 않으면 시작할 때 HLSL 컴파일과 3D compute dispatch 비용이 발생하므로 manifest 기반 영구 캐시를 도입했다.

## 4. 검토한 대안

- 매 픽셀 절차식 노이즈: 수정은 즉시 보이지만 ray step마다 고비용 noise를 반복한다.
- DDS 정적 에셋: 시작은 빠르지만 생성 파라미터와 셰이더 호환성을 프로젝트가 검증하기 어렵다.
- 현재 방식: compute로 생성한 볼륨과 shader blob을 하나의 버전된 bundle로 관리해 빠른 시작과 개발 중 재생성을 함께 지원한다.

## 5. 선택한 해결 방법

노이즈 수식은 공통 include에 두고 compute와 pixel shader가 공유한다. 정상 캐시 시작은 저장된 `.cso`와 Texture3D를 바로 올리고, 소스 변경 시 첫 Present 이후 hot reload한다. UI는 렌더 모드, 단면, 파라미터, 캐시 작업과 통계를 한 패널에서 제공한다.

## 6. 실제 수정 내용

- periodic Perlin-Worley base와 Worley detail, 높이 마스크, 침식, 바람을 추가했다.
- 128³ base와 64³ detail Texture3D 생성 및 SRV/UAV 전환을 구현했다.
- 캐시 저장·복원·재생성, shader reflection 검증과 원자적 bundle 교체를 구현했다.
- 태양 light march, Beer–Lambert 셀프섀도우, HG 위상과 ambient를 추가했다.
- F1 ImGui 패널, 4-MRT Noise Inspector, 프리셋과 8개 렌더 진단 모드를 추가했다.
- 캐시 smoke test, periodic seam, 레이아웃, round-trip, 산란 수치 테스트를 추가했다.

## 7. 캐시·호환성·성능 영향

캐시 v3은 base 128³ R8 약 2 MiB와 detail 64³ RGBA8 약 1 MiB를 사용한다. 생성 파라미터 hash와 전체 셰이더 소스 hash가 manifest에 들어간다. 정상 캐시 시작에서는 runtime compile과 noise dispatch가 모두 0회다.

## 8. 테스트 및 실행 결과

2026-07-27 기준:

- Debug 빌드 성공
- Release 빌드 성공
- `VolumetricCloud.CacheSmoke` 통과
- `VolumetricCloud.CodeTests` 통과

화면 품질은 자동 테스트 대상이 아니다. 사용자가 제공한 화면에서 구름 렌더 자체는 확인했지만 형태와 조명 대비 개선이 필요하다.

## 9. 남은 문제와 후속 개선

- base 다중 채널 형태 조합
- Cumulus 높이 프로파일
- dual-lobe phase, powder, 다중 산란 근사
- jitter와 가변 view marching
- 반해상도 temporal, weather map, 장면 depth 합성은 이후 단계
