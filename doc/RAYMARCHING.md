# High Planar Cloud 레이마칭

2026-09-22 명시적 진단은 같은 View 표본에서 `Σ(Tair(d_i)*c_i + Lair(d_i)*w_i)+Tcloud*background`를
현재 대표거리 합성과 비교한다. `c_i`는 앞 구름T/구간적분을 이미 포함하고 `w_i=Tbefore*(1-Tstep)`이다.
일반 수식 변경이 아닌 조건부 테스트이며 [직접 적분/범위](changes/stage15-cloud-aerial-composition.md)를 따른다.

현재 셰이더는 meter 단위 Planar layer 하나를 Full resolution에서 추적한다. Stage 1의 AABB 수학은 CPU 학습 테스트로만 남고 런타임 선택지가 아니다.

패스와 실제 심볼을 먼저 따라가려면 [렌더링 파이프라인 가이드](RENDERING_PIPELINE_GUIDE.md),
수식에 들어가는 b0~b9 필드는 [상수버퍼 참조](CBUFFER_REFERENCE.md), 값을 직접 바꿀 때는
[구름·빛 튜닝 가이드](CLOUD_LIGHTING_TUNING_GUIDE.md)를 함께 본다.

## 좌표와 단위

| 값 | 단위 |
|---|---|
| 월드 위치, domain 높이, ray 거리, step | meter |
| 바람 속도 | meter/second |
| extinction | `1/meter` |
| Weather UV, coverage, density, transmittance | 무차원 |
| Base/Detail Texture3D 좌표 | 반복되는 0~1 UVW |

광학 깊이의 핵심 곱 `density × extinction × distance`는 무차원이다.

## 카메라 ray와 Scene Depth

Fullscreen pixel의 UV를 NDC로 바꾼 뒤 `invViewProj`로 far point를 복원한다.

```text
ndc = (2u - 1, 1 - 2v, 1, 1)
worldFar = ndc × invViewProj
rayOrigin = cameraPosition
rayDirection = normalize(worldFar.xyz / worldFar.w - rayOrigin)
```

D32 scene depth가 1보다 작으면 같은 UV를 역투영해 카메라부터 첫 불투명 표면까지의 meter 거리를 얻는다. 구름 `tEnd`는 이 거리보다 멀어질 수 없으므로 건물과 지면 뒤의 구름이 새지 않는다.

## Planar layer 교차

```text
bottom = cloudBottomAltitude
top    = bottom + cloudLayerThickness
t0     = (bottom - origin.y) / direction.y
t1     = (top    - origin.y) / direction.y
tStart = max(min(t0, t1), 0)
tEnd   = min(max(t0, t1), externalLimit, domainLimit)
hit    = tEnd > tStart
```

View domain limit는 최대 50km, Light limit는 최대 20km다. 카메라가 layer 안에 있으면 `tStart=0`이다. `abs(direction.y) <= 1e-6`인 수평 ray는 origin Y가 layer 안일 때만 유한 trace limit를 반환한다. 모든 입력은 finite인지 먼저 검사한다.

View 끝의 직선 절단을 감추기 위해 formation이 정한 `fadeStart~maxDistance`에서 다음 값을 density에 곱한다.

```text
u = smoothstep(fadeStart, maxDistance, rayDistance)
distanceFade = 1 - u
```

## Weather와 local column

Weather Map은 월드 XZ와 공통 wind로 이동한다.

```text
stationaryWorld = worldPosition - windDirection × windSpeed × time
weatherUv = frac(stationaryWorld.xz / weatherWorldSize + offset)
```

RGBA 채널:

- R: 구름 support와 수평 coverage
- G: 0 Stratus, 0.5 Mixed, 1 Cumulus
- B: 0.5~1.5 density multiplier
- A: 타입별 min~max 두께를 고르는 potential

한 XZ 기둥의 형상은 다음처럼 만든다.

```text
thickness = lerp(typeMinThickness, typeMaxThickness, weatherA)
baseLift  = min(configuredLift × typeScale × (1-weatherA)^1.5,
                thickness × 0.25)
localBottom = globalBottom + baseLift
localTop    = localBottom + thickness
h = (worldY - localBottom) / thickness
```

Stratus/Mixed/Cumulus의 bottom fade, top fade와 cumulus upper mass를 G로 보간해 vertical profile을 만든다. footprint는 높이에 따라 연속적으로 변하며 `footprintCoverageInfluence`만큼 수평 threshold에 반영된다.

Formation apply 전에는 `maxThickness + maxBaseLift + 200m <= domainThickness`를 검사한다. 이 계약이 상단 절단과 네모난 domain 윤곽을 막는다.

## Base와 Detail density

Weather/local profile이 확실히 0이면 Base Texture3D도 읽지 않는다. 유효한 위치에서 Base density는 다음 순서다.

```text
baseUVW   = frac(stationaryWorld / baseWorldSize + offset)
baseNoise = remap(Base Texture3D RGBA)
coverage' = globalCoverage × weatherFactor × footprintFactor
threshold = remapCoverage(baseNoise, coverage')
baseDensity = insideColumn × weatherSupport × threshold
              × verticalProfile × densityMultiplier × weatherDensity
```

`baseDensity > 0`이고 detail erosion이 0보다 클 때만 Detail Texture3D를 읽는다.

```text
detailUVW = frac(stationaryWorld / detailWorldSize + detailOffset)
S = saturate(threshold)
A = insideColumn × weatherSupport × verticalProfile × densityMultiplier × weatherDensity
boundary = 1 - smoothstep(0.45, 0.90, S)
e = detailNoise × detailStrength × boundary × (1 - protection × core)
carved = e >= 1 ? 0 : saturate((S - e) / max(1 - e, 1e-6))
finalDensity = saturate(A × carved)
```

2026-09-22 사용자 채택식이다. `[e,1]` 형상 구간을 `[0,1]`로 펼치고 밀도 배율을 뒤에 적용한다.
Detail을 생략하면 기존 Base를 그대로 반환하며 마지막 공통 shaping은 유지한다.
과거 감산식은 명시적 진단 정의0에 남겨 비교한다. 근경 선명도는 미해결 상태다.

모든 거리에서 같은 Detail 경로를 사용한다. 거리 LOD나 fade는 없다.

## High View march

고정 수치:

```text
base step       = 100m
max iterations  = 512
early exit      = T <= 0.01
empty threshold = Base density <= 0.0001
coarse trigger  = 3 consecutive empty samples
coarse step     = min(normal step × 2, 200m)
far step        = 24~50km에서 1×~1.25×
```

거리 step은 다음과 같다.

```text
w = smoothstep(24000, 50000, sampleDistance)
step = 100 × lerp(1, 1.25, w)
```

각 구간의 밀도는 구간 가운데가 아니라 픽셀별 지터 위치에서 읽는다(2026-09-25).
모든 픽셀이 같은 거리에서 읽으면 구름층 경계에서 카메라 중심의 동심원 줄무늬가 생기기 때문이다.
지터는 화면 좌표의 interleaved gradient noise이며 시간에 따라 바뀌지 않는다. 구간 경계와 적분 길이는 같다.
자세한 문제·원인·해결은 [시선 표본 지터](changes/stage15-view-sample-jitter.md)를 따른다.

```text
jitter         = frac(52.9829189 × frac(dot(pixel, (0.06711056, 0.00583715))))
sampleDistance = cursor + jitter × marchLength   // 이전: cursor + 0.5 × marchLength
```

정상 표본에서 Beer–Lambert 적분:

```text
deltaTau = density × extinction × marchLength
stepT    = exp(-deltaTau)
opacityContribution = accumulatedT × (1 - stepT)
accumulatedT *= stepT
```

대표 cloud depth는 opacity contribution을 weight로 사용한 거리 moment다.

```text
cloudDepth = Σ(distance × opacityContribution)
             / Σ(opacityContribution)
```

빈 표본이 3개 연속이면 coarse search로 전환한다. coarse 위치에서 Base support를 찾으면 cursor를 직전 coarse 구간만큼 되돌리고 정상 step으로 다시 적분한다. 이 rewind가 얇은 구름을 건너뛰는 오류를 막는다.

## 태양광과 Deep Cache

기본 직접광은 Balanced512 Near/Far cache에서 태양 방향 optical depth를 읽는다.

```text
Tsun = exp(-opticalDepth)
```

cache 밖이거나 사용할 수 없는 표본은 8개 deterministic cone interval을 사용한다.

- cone angle 2°
- far sample fraction 0.77
- 시작 bias 1m
- golden-angle disk offset
- 각 tap은 담당 interval 길이로 optical depth를 가중
- optical depth 9.21034037, 즉 `T <= 0.0001`에서 종료

Straight/Reference light ray와 런타임 light sampling preset은 없다.

## 산란과 합성

한 View step의 직접 단일 산란은 다음 관계를 사용한다.

```text
interaction = accumulatedT × incidentSun × albedo × (1 - stepT)
direct = interaction × shapedSunTransmittance × dualLobePhase
```

여기에 physical sky fill, ground bounce, AO와 optical-depth 기반 multiple scattering을 더한다. Cloud PS가 계산한 최종 HDR은 다음과 같다.

```text
cloudOpacity = 1 - cloudT
behindDistance = min(cloudDepth, sceneDistance)
background = HDR scene 또는 atmosphere sky
background = aerialPerspective(background, behindDistance)
hdrCloud = cloudScattering + cloudT × background
```

이 `hdrCloud` 하나를 Tone Map PS가 exposure, Bradford white balance, ACES, sRGB와 dither 순서로 Back Buffer에 출력한다.

## 수치 안전성

- 잘못된 교차, 0 두께, 비정상 방향은 `scattering=0`, `T=1` 중립 결과다.
- 분모는 물리 의미에 맞는 epsilon으로 보호한다.
- density와 optical depth는 음수가 되지 않게 한다.
- 디버그 출력은 NaN/Inf를 자홍색, 음수를 빨강, half-float 초과를 노랑으로 표시한다.
- CPU 테스트는 교차, early exit, skip/rewind, cone interval, formation domain-fit을 독립 검증한다.

## Stage 15 방향광 01: 보이는 구름의 태양 투과율

새 진단 ID 60은 ray 구간 중점 한 표본 대신 실제 적분 표본의 화면 기여를 사용한다.
표본 i 직전의 View 투과율을 V_i, 이번 step의 투과율을 S_i, 태양 투과율을 T_i라 하면
`w_i=V_i*(1-S_i)`, `T_visible=sum(w_i*T_i)/sum(w_i)`다.
모두 무차원이며 w에는 albedo·phase·간접광을 곱하지 않는다.
`sum(w_i)<=1e-6`이면 빈 기여로 0을 표시하고 그 외에는 결과를 [0,1]로 제한한다.
동일한 High step/skip/early exit를 사용한다. 기존 중점 진단 ID 24와 Composite 적분은 유지한다.
단순 화면 평균과 구분하기 위해 전체 화면 통계도 각 픽셀의 `1-ViewT`로 가중한다.

## Stage 15 방향광 02 — 밀도 곡선의 위치

기존 Weather/support/profile/Base → 기존 Detail 침식 → 공통 `F(q,s)` → 거리 fade → View 적분 순서다.
Light는 기존 Base 조립 → 같은 F → Near/Far optical-depth 적분 또는 cone fallback이다.
F는 `lerp(q,smoothstep(0,0.4,q),s)`, s=0이면 q 그대로다. q는 무차원이며 Base는 1을 넘을 수 있다.
단조이므로 raw Detail≤Base이면 F(Detail)≤F(Base)이고 F(0)=0이라 빈 support를 채우지 않는다.
기존 1e-4 이하 밀도는 커지지 않으므로 기존 tiny-support 생략이 새 밀도를 놓치지 않는다.
성긴 경계에서는 값이 감소할 수 있다. 따라서 화면 실루엣의 완전한 불변을 보장하는 함수는 아니다.

## 방향광 03 — Base 중간 옥타브 실험

02 사용자 승인: Urban 및 Stratus/Cumulus/Mixed 내장 Density shaping=.70. Meadow/Snow=0 유지.
03은 밀도 .70을 고정하고 Base R의 2/3번째 옥타브 진폭만 k=1/1.25/1.50배로 바꾼다.
[.5,.25*k,.125*k,.0625]를 합으로 정규화한다. 첫/넷째 진폭, seed, 주파수, 타일,
Base GBA Worley, Detail, Weather/profile과 High step/skip/cone 상수는 유지한다.

| CPU / HLSL 필드 | 슬롯·크기·offset | 범위·소유권 | 반영 |
|---|---|---|---|
| `NoiseVolumeParameters::baseMidOctaveExtra` / `baseMidOctaveExtra` | b6 총96B, offset28, float4B | extra=0/.25/.5; F2 세션 실험 | 생성 시 k=1+extra |

기존 padding0를 사용하며 offset12의 uint padding은 유지한다. CPU sanitize는 비정상 입력을0으로,
나머지는 후보에 양자화한다. CPU offsetof/HLSL reflection으로 packing을 검사한다.
`F2 → Base mid octaves (03 test)` 선택 시 기존 Base/Detail 재생성 경로로 새 텍스처를 만들고
성공할 때 교체한다. 실패하면 이전 extra와 텍스처를 유지한다. 재생성 직후 첫 프레임은 성능 표본에서 제외한다.
Type/Concept 선택은 세션 후보를 유지한다. Custom에는 저장하지 않고 snapshot42의 noiseVolumes에
baseMidOctaveExtra를 기록한다. 초기 1.00배이며 아직 후보 채택은 하지 않았다. 06에서 임시 실험 UI를 정리한다.

`--base-octave-test`/CTest BaseOctaves는 72조건(세 후보×Urban/세 Type×세 고도×Detail Off/On)의
HDR finite/진단 범위를 검사한다. 생성 RGBA8 R은 CPU 기준과 1/255+1e-5 이내 비교한다.
GBA와 Detail의 비트 동일성, Weather 해시 유지, 1.00배로 복원 시 원래 Base 해시를 검사한다.
`--high-performance-test --base-octaves-125` 또는 `--base-octaves-150`은 기존 12 case를 직렬 측정한다.
밀도 인자가 없으면 승인된 Concept 기본값(Urban .70, Meadow/Snow 0)을 사용한다.
00 baseline 검증은 실행기에서 강도0을 명시적으로 복원하며 원본을 다시 촬영하지 않는다.

### 04 — 폐기된 Near Detail 그림자

06-A에서 사용자 요청으로 UI·설정·거리 가중치·Detail 조회·전용 테스트를 제거했다. Near/Far/cone는 모두 Base 그림자 밀도를 사용한다. 실험 원인·결과는 [06 림 변경 기록](changes/stage15-cloud-rim-lighting.md)에 보존한다. ShadowCB 160B의 offset152/156은 uint padding이며 0으로 초기화한다. 미사용 surfaceShadowEnabled도 offset8의 uint padding으로 바꿨다. 실제 지면 그림자 strength/floor는 유지한다.

## 05-B: 저장 격자와 적분 간격을 따로 다루기

고도 a, 수평 기준 폭 W, 층 두께 H일 때 태양 평면 세로 폭은 Wup=W*sin(a)+H*cos(a)다.
월드Y 고정 시 같은 up texel의 수평 간격은 (Wup/N)/sin(a)다. 정사각 폭W를 그대로 사용하면
낮은 태양에서 매우 큰 수평 간격이 되어 밀도의 규칙적인 과대/과소 추정이 나타날 수 있다.
생성과 조회의 폭을 함께 바꿔야 같은 태양 광선을 같은 texel에서 읽는다.

높이 저장 간격 dy=H/(slices-1), 태양 경로 간격 ds=dy/sin(a).
현재 후보는 n=max(ceil(ds/250m),1)개 중점에서 density를 읽고 sigma*ds/n을 곱해 tau를 누적한다.
저장 slice 사이에는 기존 tau 선형 보간을 한다. 적분 n을 늘려도 저장/보간 오차는 남으므로
높이 구간2배(80/40→159/79)는 별도 후보로 비교한다. 태양 최소고도3도 및 cone 정책은 유지한다.

## 06 — 직접광의 기본 부분과 림 분리

시선 기여 W=Tview×albedo×(1−Tstep), 태양 입사광 Lsun, 원본 태양 투과율 T를 재사용한다. C=W×Lsun×T^shadowExponent. S0=lerp(1,T^edgeOpticalDepthScale,edgeInfluence), Srim=lerp(1,T^(edgeOpticalDepthScale×rimDepthScale),edgeInfluence).

```text
base = C × (1 + min(P0−1,0) × S0)
rim  = C × rimIntensity × max(Prim−1,0) × Srim
Direct = base + rim
Composite cloud scattering = Direct + Sky + Ground + Multiple
```

P0는 기존2.5 상한의 phase이고 Prim만 림 상한을 사용한다. Multiple은 P0를 계속 사용한다. Silver Lining 진단은 rim만 보여주며 Composite에 다시 더하지 않는다. Rim1/1/상한2.5에서 기존 식과 대수적으로 같다. 추가 밀도·법선·빛 레이·Bloom은 없다. 밀도와 Tsun은 림 설정의 소비자가 아니다.

CPU F3/Concept → LightParameters sanitize → b3 → PhaseFunction의 P0/Prim → CloudLighting의 base/rim → CloudEnvironment의 Direct 및 Silver 진단 → VolumetricClouds 적분 → 기존 Tone Map 순서다. F4 성분은 L/(1+L) 표시이므로 진단의 HDR 버퍼 값 자체를 물리 입사광으로 오해하면 안 된다. 림 테스트는 이 표시를 역변환한 HDR 근사와 실제 Composite HDR/sRGB를 구분한다.

06 화면 승인 후07에서 전체 Concept/F5~F8 회귀·12case 성능·Custom·최종 비교 촬영을 한다. 기존06-final은 림 이전 승인 자료이고 최종 산출물은07-final이다. 하부 평탄화 분석은 보류한다.

06 테스트 전용 광로 가중치: VCLOUD_TEST_RIM_PATH=2일 때 rimWeight=(1-e)+e*(Tview_mid*Tsun)^(edgeScale*rimDepth)이다. Tview_mid=Tview*exp(-density*sigma*ds/2). 이는 꺾인 광로의 추가 감쇠 실험이며 전체 View chord/물리적 실루엣 검출이 아니다. 기본 산란/View 감쇠는 이미 유지되고, macro 없는 일반 실행에는 적용하지 않는다. =1은 추가 림 가중치만1로 둔다. 결과/한계는 stage15-cloud-rim-lighting 변경 기록을 따른다.

2026-09-16 하늘광 차폐 분리: EnvironmentCB48B/모든 offset은 유지한다. ambientShadowCoupling/ambientShadowExponent는 이제 구름의 지면 반사광 가시성에만 사용하며 하늘광에는 적용하지 않는다. Sky는 기존 LUT RGB×fill×height×local AO다. Direct/Rim/Multiple·LUT 생성은 그대로다. F4 Ground Ambient Visibility가 남은 지면 가시성 의미를 표시한다. 실제 변경과 수식은 stage15-cloud-rim-lighting 변경 기록 참조.

06 테스트 전용 VCLOUD_TEST_BASE_DIRECT_SCALE=k는 Direct의 basePhase만 k배로 합산한다. Direct=interaction×shapedSunT×(k×basePhase+rimPhase). 태양광 자체나 Multiple을 줄이는 식이 아니며, macro 없는 일반 식은 기존 그대로다.

### 테스트 전용 Powder

VCLOUD_TEST_POWDER_STRENGTH가 있을 때만 기본 직접광에 weight를 곱한다. curve=saturate(2*(1-exp(-4*finalDensity))), angle=1-smoothstep(-.5,.5,cosTheta), weight=lerp(1,curve,strength*angle). finalDensity는 기존 Detail 침식과 density shaping 이후 샘플 밀도이며 ds를 곱하지 않는다. cosTheta는 카메라→표본과 표본→태양의 내적이다. 순광에서 낮은 밀도 부분을 감광하고 역광에서는 중립이다. Direct=BaseDirect*weight+Rim이며 Sky/Ground/Multiple과 밀도·투과율은 그대로다. 곡선4와 강도는 미승인 실험값으로 일반 렌더에는 적용하지 않는다.

### Stage15 거리별 View 적분 시험
일반 High의 s(d)=100*lerp(1,1.25,smoothstep(24000,50000,d))m는 유지한다.
시험 매크로 VCLOUD_TEST_NEAR_STEP_METERS가 있을 때 s_test(d)=lerp(s_near,s(d),smoothstep(5000,15000,d))이며 s_near=50 또는25m. d는 ray cursor이고 구름 대표깊이가 아니다. 밀도는 그대로 두며 Beer-Lambert의 실제 marchLength 가중도 유지한다. 시험 반복한도4096은 작은step에512를그대로 적용할 때 추적거리가 줄어드는 혼동을 피하기 위한 여유다. 일반512는 불변이며 채택은 화면·성능 비교 뒤 결정한다.

### Base 재매핑 테스트 전용 (2026-09-19)
--cloud-base-remap-test는 기존 d=saturate((n-(1-c))/c)에서 문턱이동 d=saturate((n-(1-c)-0.05)/c), 또는 raw 대비 n'=saturate(0.65+2*(n-0.65))를 독립 비교한다. c는 기존 Weather/footprint가 적용된 유효 coverage다. 중간 옥타브는 생성기의 주파수9/17 가중치배수1.5를2.5로 바꾸되 총가중치 정규화는 유지한다. View/Light/Deep이 같은 n과 d를 쓰고 Beer-Lambert의 sigma/ds는 바꾸지 않는다. macro 없는 일반경로는 동일하다.

Detail 코어보존(테스트전용): k=smoothstep(.15,.40,Weather문턱통과밀도)*smoothstep(.15,.75,높이profile)*WeatherSupport. 가중치후보E=Eold*lerp(1,.35,k),상한후보E=min(Eold,Base*lerp(1,.35,k)). 이후기존saturate(Base-E)/shaping. 코어추정은SDF가아니며카메라거리와독립. Base0은0유지. 일반High/그림자/기본식불변.


2026-09-21: 런타임Detail몸체보호는 E=Eold*(1−p*core), p∈[0,1](기본0). core근사는기존비교식과동일하며.65가이전35%감산후보를재현한다. 밀도전이폭재매핑은사용자요청으로제거했다. 거리보정/형상두께수정/태양Detail차폐는없다.


2026-09-22 승인된 구름 대기 합성은 d_air=2*d_rep에서 T_air/L_air를 함께 조회한다. 실제 ray 거리와 Cloud T는 바꾸지 않는다. 구름 전용 예술적 보정이며 대기 제외는 그대로 C+Tc*B다.

근경진단: S=coverage remap형상, A=높이·Weather·밀도곱. 후보1은Shape(saturate(A*saturate(S-e))), 후보2는Shape(saturate(A*saturate((S-e)/(1-e)))); e≥1이면0. e는S로계산한경계가중치/기존몸체보호를쓴다. 태양비교는High누적불투명도의25/50/75%실제표본을고르고같은태양광선을12.5/6.25m로적분한다. 25m시선계보는분석전용이며일반Viewstep후보가아니다.

E17 시험 전용 대역 결합: q=strength·boundary(S)·(1−protection·core), 기존 e=q·Σ(w_i n_i),
연속 remap의 유효 문턱 e=1−∏(1−saturate(k q w_i n_i)). 동일한 S/A/shaping을 적용한다.
k=1과 고정 단면의 shaping 후 평균 제거량을 맞춘 k를 비교하며 일반 수식은 가중합 remap을 유지한다.

E18 시험 전용 원본 주파수: n'_i=.625 W(f_i)+.25 W(2f_i)+.125 W(4f_i), f_i=2/3/4/5 cycle/2km.
각 W는 같은 채널 seed/주기 wrap을 사용한다. e=k q saturate(Σw_i n'_i), 나머지는 승인 remap 그대로다.
최대20cycle/tile은 voxel Nyquist32보다 작지만 Worley는 대역 제한 함수가 아니고,
High100m 적분의 화면 안정성도 보장하지 않는다. 2026-09-22 사용자 채택으로 일반은 위 fBm에 k=1이다.
실험2의 제거량 보정 k≈.981983208은 일반에 적용하지 않는다.
