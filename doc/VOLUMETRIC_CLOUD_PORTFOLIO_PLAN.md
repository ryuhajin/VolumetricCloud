# TA 포트폴리오용 대규모 볼류메트릭 클라우드 계획

## 목표

현재 사용자 승인 기준인 `stage8-approved`의 16×3×16m AABB 구름을 출발점으로 삼아,
지상과 구름 비행 시점에서 수평선까지 이어지는 km 규모 구름을 만든다. 목표 화면은 넓은
Weather 분포, 연결된 큰 적운 질량, 작은 표면 침식, 자기 그림자와 밝은 가장자리를 함께
보여 주어야 한다.

중단된 `stage13-paused-20260810`은 직접 병합하지 않는다. Y 평면 교차, km 광학값과
테스트 결과는 참고하되 단위 관계를 먼저 독립 검증한 뒤 새로 구현한다.

## 확정한 실행 순서와 승인 원칙

```text
0~8 사용자 승인 완료
→ 13 대규모 도메인 재구축
→ 9 기본 최적화
→ 10 저해상도·업샘플링
→ 11 Jitter·Temporal
→ 12 Cloud Shadow·Light Cache
→ 14 대기·지면 통합
→ 15 최종 품질·성능·포트폴리오 정리
```

각 단계는 에이전트 자동 검증과 사용자 수동 렌더 승인이 모두 끝나야 완료된다. 에이전트는
수식·빌드·테스트·성능만 합격 처리하며 구름 모양, 반복, banding, ghosting과 최종 화면은
사용자가 직접 승인한다.

## 공간 단위 계약

| 물리량 | 내부 단위 | 16m 장면을 S배 상사 확대할 때 |
|---|---:|---:|
| 위치·AABB·층 두께·추적 거리·step | m | `×S` |
| Base/Detail 주파수 | cycle/m | `÷S` |
| extinction | 1/m | `÷S` |
| Weather world size | m | `×S` |
| 바람 속도 | m/s | 같은 상대 애니메이션이면 `×S` |
| density·coverage·UV·step 개수 | 무차원 | 유지 |

다음 네 무차원 관계를 CPU 기준 테스트로 고정한다.

```text
noiseCoordinate       = worldPosition × noiseFrequency
weatherCoordinate     = worldPosition / weatherWorldSize
opticalDepth          = density × extinction × pathLength
samplesPerWavelength  = (1 / noiseFrequency) / stepLength
```

단위를 m에서 km로 표기만 바꾸는 경우와 장면 자체를 1,000배 확대하는 경우는 다르다.
이 계획은 셰이더 내부 meter를 유지하면서 실제 장면을 확대한다.

## 단계 13 세부 게이트

| 하위 단계 | 구현 목표 | 자동 게이트 | 사용자 화면 게이트 |
|---|---|---|---|
| 13-0 | Stage 8 기준선과 단위 계약 | 1/10/100/1000× 상사 불변식, 전체 CTest | 문서와 시험 절차 승인 |
| 13-1 | AABB/평면층 도메인 분리 | 아래·내부·위·수평·깊이·NaN 교차 | 옆면·검정 화면·거리 절단 없음 |
| 13-2 | 기존 noise의 단계적 확대 | 정규화 위치 밀도·광학 깊이 일치 | 확대 전후 실루엣·투과율 일치 |
| 13-3 | 오픈 월드 실제값 재조정 | 파장당 표본과 τ 진단 | banding 없이 거리 깊이 구분 |
| 13-4B | Weather 물리 두께와 Base/Detail 3D texture | 분포·타입 프로파일·GPU 단면·hash·seam | 1~6km 가변 상단과 작은 경계 침식 분리 |
| 13-4C | 개발 UI 역할 분리와 Local Cloud Inspector | FOV·WASD rig·단일 Weather·Type·상태 왕복·GPU smoke | F1~F4와 200m Depth·360°·Type·장면 복원 |
| 13-4D | 단일 포트폴리오 디버깅 씬 | 10km 지면·20층 건물·50km 계약, 입력/카메라/Compare/schema 28 | F5 Depth·F6 수평선·F7 내부·F8 상공, 숫자 0~9와 정리된 F1~F4 |
| 13-4E | Dense Broken-Sky와 구름 타입 프리셋 | Weather 점유율·분리 density 수식·결정적 Compare·Custom schema 29 | 전역 broken-sky, 층운/적운 차이, 명암, Custom 재실행 복원 |
| 13-5 | km 광학과 조명 | View/Light 단위 일치, LOD 수치 | 자기 그림자·silver lining·환경광 |

13-3의 시작값은 층 바닥 1,500m, 두께 3,000m, View 50km, fade 40~50km,
Weather 64km, View `100m/512`, Light `250m/80`, density `1.0`, extinction
`0.0005/m`다. 이는 최종 미적 값이 아니라 서로 다른 물리량을 한 번에 바꾸지 않기 위한
첫 검증값이다.

13-4B는 전역 층을 1.5~7.5km로 확장하고 Weather A를 `Local Thickness Potential`로
사용한다. 타입과 A로 XZ 기둥마다 1~6km의 물리 두께를 구한 뒤 Typed Shape Profile을
Weather Coverage와 Base Noise threshold에 결합하고 Detail 3D Erosion 순서로 밀도를 만든다.
Profile은 opacity 곱이 아니라 높이별 XZ 존재 경계를 정한다. 결정적 seed의 Base `128³ RGBA8`는
XYZ 12km와 주파수 `{4,9,17,23}`, octave별 seed 간격 173을 사용하고, Detail `32³ RGBA8`는 XYZ 2km와 `{2,3,4,5}`를 사용한다.
기존 256² Weather는 64km 배치를 담당한다.

진행 상태: 13-0/13-1은 2026-08-10, 13-2/13-3은 2026-08-11, 13-4B는
2026-08-14 사용자 승인을 받았다. F1~F4 역할 분리, 200m `Local Cloud Inspector`, 양쪽
도메인 WASD·상태 복원과 최신 Weather/Base/Thickness 품질 재조정을 포함한 13-4C도
2026-08-16 사용자 승인을 받았다. 이 승인 이력은 보존하되 13-4D가 Local Inspector
런타임을 50km 단일 평면 구름 씬으로 대체한다. 13-5의 기존 자동 결과를 보존하면서
새 F6에서 같은 허용 오차로 재검증했다. 13-4D 사용자 검증에서 공간 점유율·납작한 형상·약한
명암 문제가 발견되어 13-4E가 최종 기본 외형을 대체했다. 13-4D/13-4E와 새 Dense Mixed
F6의 13-5는 2026-08-17 사용자 승인을 받았다. PlanarLayer를 최종 대규모 도메인으로
확정하고 구형 shell 비교는 포트폴리오 범위에서 제외했다. 이제 단계 9의 View/Light 기본
최적화를 진행한다.

13-5의 Open World 품질 기본은 View `100m/512`, Light `250m/80`이다. Light
`62.5m/320` fine reference와 `125m/160` 이전 품질을 96×54 float readback으로 비교하며,
Dense/Stratus/Cumulus의 기본 Composite MAE는 각각
`0.00002579/0.00000912/0.00001599`로 자동 gate를 통과했다. Light 전용 경로는 확실한
Weather/높이/profile 공백을 Base fetch 전에 거르고 `T≤0.0001`에서만 종료한다.
HLSL Light bias는 CPU와 같은 `0~100m`를 사용해
1000× 상사 프리셋의 10m를 보존한다. Detail은 실제 `32³` weighted mean
`0.44994098`로 32~48km에서 수렴하고 끝 거리 밖에서 texture fetch를 생략한다.

13-5 외곽광 보완은 Light `250m/80`과 texture fetch 수를 유지한다. `Tsun` 거듭제곱으로
직접광 대비와 Phase 적용 폭을 분리하고, 환경광 AO와 다중 산란도 같은 `Tsun`을 재사용한다.
Portfolio Hero는 따뜻한 Low East 직접광, 표면 범위 Silver Lining과 차가운 내부 fill을 한 번에
적용한다. CPU/HLSL의 LightCB/EnvironmentCB는 각각 80바이트이며 전체 snapshot은 schema 30,
외형 Custom 원자 저장은 schema 29를 유지한다. 자동 smoke의 노출 외곽/내부 Silver 평균은
`0.12536342/0.06037134`, `RGB peak≥0.98` 비율은 0이다. Cumulus F6 원시 GPU Cloud
p95는 변경 전/후 `15.639552/15.785984ms`로 약 0.94% 증가해 +5%와 16.67ms gate를
통과했고 2026-08-17 사용자 화면 승인을 받았다.

13-4E Dense Mixed는 Weather non-zero/core `79.62%/49.11%`, global coverage `0.68`,
density `1.15`, extinction `0.00035/m`, erosion `0.18`을 사용한다. Weather support,
horizontal coverage와 vertical profile을 분리해 하늘 전역의 20~40% 푸른 틈, 둥근 상단과
밝은 가장자리/어두운 내부를 동시에 목표로 한다. Stratus/Cumulus 버튼은 같은 seed·wind·
camera/light에서 두께와 profile만 뚜렷하게 비교하며 Custom은 schema 29 원자 저장을 사용한다.

## 후속 성능과 최종 목표

- 단계 9 최적화 전후 화질은 고정 입력에서 `SSIM ≥ 0.99`, 정규화 `RMSE ≤ 0.01`을 지킨다.
- Release High 1920×1080, VSync Off, 현재 개발 PC에서 GPU Frame p95 16.67ms 이하,
  GPU Cloud p95 10ms 이하를 목표로 한다.
- 측정은 120프레임 워밍업 뒤 600프레임을 기록한다. GPU 어댑터·드라이버·seed·카메라·
  설정과 평균/p95를 JSON/CSV에 함께 남긴다.
- 최종 고정 장면은 `GroundZenith`, `GroundHorizon`, `InsideLayer`, `AboveLayer`,
  `FlightTraversal`, `DepthOccluded`, `CumulusHorizonStress`다.

## 단계 중단과 복구

- 사용자 화면 게이트가 실패하면 다음 단계 기능을 추가하지 않고 같은 단계에서 원인을 분리한다.
- 단위·교차·형태·광학·조명을 한 변경에서 동시에 조정하지 않는다.
- 실패 실험은 별도 브랜치와 태그에 보존하되 승인 기준에 섞지 않는다.
- 단계 9에서는 early exit와 coarse march만 다루고 저해상도와 temporal은 단계 10~11까지 미룬다.
