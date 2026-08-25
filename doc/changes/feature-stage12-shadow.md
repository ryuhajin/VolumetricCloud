# feature/stage12-shadow

## 목적

단계 11의 Full/50% 화면 경로와 Temporal 안정성을 유지하면서, 반복 Light Ray를 대체할 수 있는
월드 공간 Deep Optical-Depth Cache와 지면·건물 Cloud Shadow를 단계 12 범위로 추가한다.

## 구현 결정

- 화면/Cloud Data 해상도와 분리된 Near 24km×80, Far 128km×40 cache를 사용한다.
- Fast256은 30MiB, Balanced512는 120MiB의 `R32_FLOAT Texture2DArray`다.
- 태양에 수직인 light-space 중심을 texel 단위로 snap하고 매 프레임 전체 배열을 갱신한다.
- slice에는 투과율이 아니라 Base-only 누적 광학 깊이 `tau`를 저장하고 조회 시 `exp(-tau)`를 계산한다.
- Near 80~95%에서 Far로 전환하고 Far 90~100%에서 중립 `T=1`로 fade한다.
- 구름층 아래 표면은 bottom slice를 읽으며 이 값은 Cloud history에 저장하지 않는다.
- 승인 기본값은 DeepCache/Balanced512다. DirectReference는 회귀 비교로 유지하며 AABB,
  태양 3° 미만, 리소스/셰이더 실패는 구름 Direct Light Ray와 표면 중립값으로 폴백한다.
- `ShadowCB(b12)`는 160바이트, Near/Far PS 입력은 `t6/t7`, linear clamp는 `s2`다.
- snapshot은 schema 34/`implementationStage=12`이며 schema 33 이하는 DirectReference다.

## 범위 밖

- 일반 오브젝트 Shadow Map과 건물이 구름에 만드는 그림자
- 법선 기반 태양광, 하늘·지면·대기 정식 조명
- 최종 Low/Medium/High preset

## 검증 상태

- Stage12ShadowMath: basis, 태양 레이 UV 불변성, snap, cascade, slice, Beer-Lambert,
  18°/70° 경로, 메모리와 schema migration 통과.
- Fast256 96×54 D3D smoke: Light T MAE/P99 `0.001635/0.029349`, Full/50% 및
  resize 전후 cache identity 유지, 실제 Surface T 관측, 다섯 단계 12 진단 출력 finite와
  D3D11 debug error 0 통과.
- Debug/Release 전체 빌드, Release CTest `47/47`, 단계 12 HLSL 독립 컴파일을 통과했다.

## 2026-08-24 사용자 1차 검증 수정

- 지면과 박스 건물의 linear RGB를 모두 `(0.5,0.5,0.5)`로 바꿔 Surface Cloud Shadow가
  중간 회색을 기준으로 분명하게 밝아지고 어두워지게 했다.
- 검게 보이던 Near/Far 진단은 카메라 레이 대표점 `tau/maxTau` 표시를 제거하고 선택한
  Texture2DArray slice를 화면에 직접 펼치는 preview로 바꿨다. slice 0이 표면 Shadow Map이고
  기본 exposure 4의 `1-exp(-tau*exposure)`로 작은 광학 깊이도 보인다.
- F3에 Near/Far debug slice와 exposure를 추가했다. Cascade는 geometry의 실제 표면 위치와
  하늘의 구름층 중간 교차 위치를 사용하며 red=Near, blue=Far, magenta=blend다.
- GPU smoke에 Near/Far texture의 0이 아닌 공간 구조 검사를 추가했고 두 배열 모두 통과했다.

## 2026-08-25 최종 승인

- 사용자 F5~F8·Full/50%·wind/이동 검증을 통과했다.
- 1080p 일곱 장면 Direct/Fast/Balanced, 120 warmup+600표본 측정에서 일곱 장면
  GPU Cloud p95 평균이 Full `11.288869/6.700032/6.907611ms`, 50%
  `3.912119/2.062629/2.361637ms`였다. 두 후보가 모두 통과해 Balanced512를 기본값으로 승인했다.
- Debug/Release 빌드, Release CTest `47/47`, Stage12 smoke와 HLSL hot reload를 최종 통과했다.
