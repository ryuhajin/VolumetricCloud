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
확정하고 구형 shell 비교는 포트폴리오 범위에서 제외했다. 단계 9의 View/Light 기본
최적화도 2026-08-19 Balanced 기본값으로 승인했다. 단계 10 저해상도·업샘플링 계보를 포함한
단계 11 안정성 우선 Temporal 기준은 2026-08-23 승인했다. 단계 12는 2026-08-25
Balanced512 기본값으로 승인했으며 다음 목표는 단계 14다.

## 단계 12 Cloud Shadow Map·Deep Light Cache 계획과 게이트

단계 12는 `feature/stage12-shadow`에서 진행한다. 화면 해상도와 월드 cache를 분리하고,
Near `24km, 80 slices`와 Far `128km, 40 slices`의 `R32_FLOAT Texture2DArray`에 태양 방향
누적 광학 깊이 `tau`를 매 프레임 기록한다. Fast256은 30MiB, Balanced512는 120MiB다.
Full 1920×1080과 50% Axis 960×540은 같은 cache 중심·범위·해상도를 공유한다.

구름은 DeepCache 모드에서 반복 Light Ray 대신 높이별 tau를 읽고, 지면·옥상·벽은 Full Scene
Depth 월드 위치에서 bottom slice를 읽는다. 표면 계수는 `ambientFloor=0.35`, `strength=1.0`의
단계 12 진단식만 사용하며 법선·정식 태양/하늘 조명은 단계 14로 남긴다. DirectReference는
회귀와 성능 비교용이며 schema 33 이하의 복원 기본이다. cache 실패·AABB·태양 3° 미만은
구름 Direct/표면 중립으로 폴백한다.

2026-08-24 사용자 1차 검증에서 어두운 기존 Scene 색 때문에 표면 그림자 대비가 약했고,
Near/Far 진단이 카메라 레이의 중간 한 점을 `tau/9.21034`로 표시해 정상 cache도 검게 보였다.
지면·건물을 linear RGB `(0.5,0.5,0.5)`로 바꾸고, Near/Far는 선택한 array slice를
`1-exp(-tau*exposure)`로 직접 펼치는 texture preview로 교체했다. slice 0은 표면 Shadow Map이다.
Cascade는 geometry의 Full Scene 월드 위치와 하늘의 구름층 중간 교차 위치를 사용한다.

CPU gate는 basis 직교, 태양 레이 UV 불변성, texel snap, cascade, slice, Beer-Lambert,
18°/70° 경로 길이와 Full/50% 독립성을 검사한다. GPU gate는 세 외형×F5~F8×세 태양,
Direct/Cache/Surface, seam, 이동·wind, Full/50%, Temporal, resize/preset/hot reload와 finite/D3D11을
검사한다. Light/Surface `T MAE≤0.01`, `P99≤0.03`, Composite `SSIM≥0.99`, normalized
`RMSE≤0.01`, seam `P99≤0.03`을 사용한다. 성능은 1080p 120 warmup+600표본 일곱 장면에서
GPU Cloud Total p95 10ms 이하, Full Direct 대비 15% 개선, 50% 3% 이상 비회귀다.
두 해상도를 통과한 가장 높은 후보만 기본값으로 승격한다. Fast256 96×54 smoke는
Light T `MAE=0.001635`, `P99=0.029349`와 Full/50%/resize cache identity, Near/Far texture
structure를 통과했다. 2026-08-25 F5~F8·Full/50%·wind/이동 사용자 화면 검증에서 Surface T와
Composite 그림자 정렬, Direct/Cache 구름 내부 명암, 상단 black slice와 magenta/blue cascade도
승인했다. 같은 날 `--stage12-performance-test`로 1920×1080, 120 warmup+600표본을 측정했다.
일곱 장면 GPU Cloud p95 평균은 Full에서 Direct/Fast/Balanced
`11.288869/6.700032/6.907611ms`, 50%에서 `3.912119/2.062629/2.361637ms`였다.
두 cache 후보 모두 게이트를 통과했고 가장 높은 후보인 DeepCache/Balanced512를 시작
기본값으로 승인했다. DirectReference는 schema 33 이하 복원과 회귀 비교로 유지한다.

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
적용한다. CPU/HLSL의 LightCB/EnvironmentCB는 각각 80바이트였으며 단계 9 전체 snapshot은 schema 31,
외형 Custom 원자 저장은 schema 29를 유지한다. 자동 smoke의 노출 외곽/내부 Silver 평균은
`0.12536342/0.06037134`, `RGB peak≥0.98` 비율은 0이다. Cumulus F6 원시 GPU Cloud
p95는 변경 전/후 `15.639552/15.785984ms`로 약 0.94% 증가해 +5%와 16.67ms gate를
통과했고 2026-08-17 사용자 화면 승인을 받았다.

13-4E Dense Mixed는 Weather non-zero/core `79.62%/49.11%`, global coverage `0.68`,
density `1.15`, extinction `0.00035/m`, erosion `0.18`을 사용한다. Weather support,
horizontal coverage와 vertical profile을 분리해 하늘 전역의 20~40% 푸른 틈, 둥근 상단과
밝은 가장자리/어두운 내부를 동시에 목표로 한다. Stratus/Cumulus 버튼은 같은 seed·wind·
camera/light에서 두께와 profile만 뚜렷하게 비교하며 Custom은 schema 29 원자 저장을 사용한다.

## 단계 11 Jitter·Temporal 계획과 게이트

단계 11은 단계 10의 저해상도 공간 복원 커밋 위에서 `feature/stage11-temporal-reprojection`
브랜치로 진행했다. 별도 `stage10-approved` 태그는 만들지 않았으며, 2026-08-23 승인 시 Stage 10
커밋 계보와 Stage 11 최종 구현을 함께 `stage11` 태그로 고정했다.

2026-08-19 사용자가 Stage 11 착수를 직접 지시해 전용 브랜치에서 아래 stability-first 기준
구현과 CPU/GPU smoke를 완료했다. 2026-08-23 F8 사선 평면과 F5 건물 경계 재검증을 통과해
사용자가 승인했으며, Stage 10 커밋 계보를 포함한 승인 기준은 `stage11` 태그로 고정한다.
승인 뒤에도 일반 시작값은 비교 기준 보존을 위해 Temporal Off로 유지한다.

첫 기준은 **안정성 우선**이다. 50% 축 Cloud Data Pass를 매 프레임 계속 실행하고 저해상도
texel 기준 `±0.25`의 2×2 네 위상 screen-space jitter만 추가한다. 네 프레임 동안 대응하는
Full 2×2 픽셀 중심을 모두 방문하지만 Horizon식 4×4 interleaved update로 새 ray 수를 더
줄이지 않는다. 안정적인 reprojection과 rejection을 사용자 승인하기 전에는 interleaved 후보와
ray-start random jitter를 추가하지 않는다.

```text
50% jittered Cloud Data
→ Full current 공간 복원
→ Full Scene class/depth hard source validation
→ 대표 Cloud Depth로 currentWorld 복원
→ Physical Wind × deltaTime 역이동
→ previous View-Projection reprojection
→ Scene/Cloud/T rejection
→ current 3×3 neighborhood history clip
→ EMA accumulation
→ Full history ping-pong과 현재 Scene 합성
```

History는 최종 Scene 합성색이 아니라 `RGBA16_FLOAT scattering/T`와
`RG16_FLOAT representative Cloud Depth/Scene Limit(m)` 두 장을 Full resolution ping-pong으로
저장한다. 현재 50km far plane은 half-float 최대 유한값 65,504m 안에 있어 meter 계약을
유지한다. future far plane이 이 범위를 넘으면 정규화 depth 또는 `RG32_FLOAT`로 다시 설계한다.

Reprojection은 `currentWorld = camera + rayDirection × representativeDepth`를 만든 뒤 Physical
Shape의 공통 바람 변위 `normalizedWindDirection × windSpeed × deltaTime`을 빼서 같은 밀도 특징의
이전 월드 위치를 구한다. 이전 UV가 화면 밖이거나 카메라 뒤인 경우, Scene geometry/sky class,
Scene Limit, Cloud Depth 또는 `T`가 맞지 않는 경우에는 history를 사용하지 않는다. Cloud Depth는
불투명 current Full 3×3 대표 깊이 범위와 상대 margin으로 검사하고, 유효 history는 같은 3×3
`scattering/T` 범위로 clip한 뒤 혼합한다.

2026-08-21~22 건물 경계 재검증에서 jitter 역보정만으로는 충분하지 않았다. 세 공간 필터가
공통으로 Full 픽셀과 다른 geometry/sky 또는 geometry surface의 low-res source를 받을 수 있었다.
Source Scene Limit/Cloud Depth finite·range와 class를 hard gate로 검사한다. 가까운 source는
`clamp(target×1%,1m,10m)` fast path로 통과시키고, 이를 넘는 F8 사선 표면은 Full D32의 작은
one-sided slope와 source jitter sample 위치로 plane residual을 검사한다. Nearest는 invalid 시 결정적 3×3 검색, Bilinear/Joint는 invalid weight를
제거·재정규화한 뒤 같은 fallback을 쓴다. valid current가 없으면 Full Scene surface/far anchor로
Scene 검사까지 통과한 history를 100% 유지하고, history도 없을 때만 투명 구름으로 시작한다.

Resize, Stage 10 Resolution/Filter, Temporal toggle, F5~F8 camera cut, 큰 FOV/time 변화,
Weather/Appearance/Domain/Light/Environment preset, snapshot load, shader hot reload와 history
리소스 실패는 history를 즉시 invalid로 만든다. Reset 프레임은 검은 clear와 섞지 않고 current를
100% next history에 써서 flash 없이 새 세대를 시작한다.

대표 Cloud Depth 하나는 여러 깊이에 퍼진 volume을 한 점으로 근사하므로 가까운 구름과 F7
InsideLayer에서 오차가 커진다. 시작 후보는 History Weight `0.80/0.90/0.95`, Near History
`Off/1km/3km`다. motion과 near fade를 최종 weight에 곱하고, 자동·사용자 gate를 통과한 후보만
기본값으로 승격한다. 승인 뒤에도 비교 기준을 보존해 Temporal 시작값은 Off이며 schema 33이 설정을 저장하고 schema 32는
Temporal Off로 호환 복원한다.

| 단계 11 검증 | 자동 게이트 | 사용자 화면 게이트 |
|---|---|---|
| Jitter·수학 | 4-phase 네 위치·평균 0·반복, C++/HLSL b11 ABI | 정지 4/8/16프레임에서 4-cycle 맥박 없이 격자·shimmer 감소 |
| Reprojection | 정지 identity, camera 이동·회전, `previousWorld=currentWorld-wind×dt` CPU/HLSL 일치 | F6 회전·줌과 바람에서 구름 꼬리·화면 가장자리 구멍 없음 |
| Rejection·reset | Scene/Cloud/T mismatch, bounds/finite, 모든 reset reason과 ping-pong smoke | 건물 누출 없음, preset·resize·hot reload 뒤 한 프레임 안에 과거 화면 제거 |
| Current source | class, jitter Full guide pixel, D32 affine slope/depth step, 필터 fallback, F5 건물·F8 사선 평면 | Current Source Validity와 beauty에서 건물 왕복·지면 수평 빗금 없음 |
| 근접 volume | near fade·motion confidence 수치와 F7 smoke finite | F7 이동에서 화면 부착·늘어짐·이중 실루엣 없음 |
| 화질 | Full 대비 `SSIM≥0.99`, normalized `RMSE≤0.01`, `T MAE≤0.01`, `P99≤0.03` | 얇은 경계와 큰 질량·밝기 보존 |
| 성능 | Temporal Resolve p95 `≤2ms`, 일곱 장면 GPU Cloud Total p95 `≤10ms` | 자동 합격 후보 중 가장 반응성이 좋은 조합 승인 |

GPU smoke는 F5~F8, `DepthOccluded`, `CumulusHorizonStress`, 정지/바람, resize, preset과 shader
hot reload를 포함한다. Release 1920×1080 성능은 VSync/UI/preview Off, 120프레임 워밍업 뒤
600프레임을 수집하고 Raymarch, Spatial/Temporal Resolve와 GPU Cloud Total을 분리 기록한다.
2026-08-22 Stratus/F5·F8 source-validation 최종 회귀의 Resolve p95는
`1.255424/1.314816ms`, GPU Cloud Total p95는 `3.897344/2.793472ms`로 두 장면 gate를 통과했다.
F8 다섯 각도·세 필터의 invalid/scanline/Off-hole 지표도 0이었다. 2026-08-23 사용자는 F8의
Current Source Validity 초록, 세 필터의 사선 평면 무빗금, F5 건물 경계 안정과 Weight의 연속
scanline 부재를 확인해 Stage 11을 승인했다. 계획했던 Full-reference 4/8/16프레임 정량화는
통과한 것으로 소급 기록하지 않고 Stage 15 최종 품질 측정으로 이관한다.
세부 구현 순서와 사용자 조작별 판정 기준은 로컬
`notes/11단계-Jitter-Temporal-Reprojection.md`를 따른다.

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
- 2026-08-17 자동 후보 측정에서 Balanced `6탭/2°/원거리 77%`가 세 외형 화질 gate와
  1080p 일곱 장면 성능 gate를 통과했다.
- 2026-08-19 사용자 검증에서 Fast/Empty Search 4×는 400m 표본 alias로 탈락했고 활성
  후보에서 제거했다. Dense 버튼 요청 경계도 복구했으며 Balanced를 단계 9 시작 기본값으로
  최종 승인했다. Cumulus Horizon p95는 Reference `18.68ms`에서 `8.64ms`로 약 53.8%
  개선됐고 일곱 장면 최대 p95는 `9.41ms`다.
