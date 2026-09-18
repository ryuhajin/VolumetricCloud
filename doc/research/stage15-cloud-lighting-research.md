# Stage15 구름 내부 명암: 실무 자료 조사와 현재 구현 비교

조사일: 2026-09-15. 범위: 공개된 게임 렌더링 발표·공식 엔진 문서·광수송 교재.
현재 비공개 상용 구현 전체를 일반화하지 않는다. 이번 변경은 조사/사용자 피드백 기록이며 셰이더를 수정하지 않았다.

## 사용자 관찰

- Shadow exponent1.35→1.62에서 차폐된 Direct가 어두워짐. 최대4까지 높이면 내부 그림자가 더 두드러짐.
- Sky/Ground fill은 전체를 조금 밝히는 정도로 영향이 작음.
- Multiple attenuation은 올리면 전체가 밝아지지만 내부 명암 변화는 체감되지 않음.
- Phase intensity는 광원에 가까운 쪽이 밝아지는 포인트 라이트 같은 느낌.
- Edge optical depth scale 영향은 작음.

이 결과는 각 항목의 화면 피드백이다. Shadow4 또는 다른 값을 기본값으로 채택하라는 지시나05 최종 승인이 아니다.
기준 Base1.50/밀도.70/Near0 및 기존 조명을 유지한다.

## 공개 자료에서 확인한 구성

1. [PBRT 4판, Equation of Transfer](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/The_Equation_of_Transfer):
   매질 내부 산란광은 입사 방향별 빛과 phase를 적분하고, 시선 경로의 투과율로 감쇠하여 관찰자에게 전달한다.
   구름 조명을 단순 표면 Lambert 조명으로 취급하는 모델과 구분해야 한다.
2. [Epic Volumetric Cloud 공식 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine):
   태양 자기 차폐에 secondary ray march 또는 cascaded Beer shadow map을 사용한다.
   다중 산란은 octave 근사이며 contribution/occlusion/eccentricity를 분리한다.
   Sky Light에는 구름 AO가 있고 지면에서 하부로 들어오는 빛도 고려한다. 밝기 배율 하나가 모든 역할을 대신하지 않는다.
3. [Guerrilla, Horizon 2015 발표](https://advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf):
   PDF p63~70에 Beer/HG와 깊이·시선 의존 powder 근사, p89에 높이 증가에 따른 ambient sky 기여와
   태양색 direct를 더하고 거리 대기를 반영하는 설명이 있다. Powder는 추가적인 외형 근사이며
   실제 태양 차폐의 대체물이나 보편적인 물리 법칙으로 취급하면 안 된다.
4. [Guerrilla, Nubis Evolved 2022 발표](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-NubisEvolved-NoVideos.pdf):
   검색 색인에서 확인한 Envelope Model / Modeling Lighting 코드에는 coarse density와 height fraction을 사용해
   하부 ambient 영향을 줄이는 예가 있다. 대용량 PDF 전체 직접 로딩은 실패했으므로 이 항목은 확인된 색인 발췌 범위에 한정한다.

## 합성의 핵심과 현재 코드

다음은 각 엔진의 원문 코드를 복사한 것이 아니라 현재 구현과 연결하기 위한 간략식이다.

```text
τsun(x) = ∫ density(x+t·sunDirection) · extinction dt
Tsun(x) = exp(-τsun(x))
W(x) = Tview(x) · (1-Tstep(x)) · albedo
ΔL(x) = W(x) · [ Sun·Tsun·phase + Sky·skyVisibility
                + Ground·groundVisibility + Multiple ]
Lpixel = 누적 ΔL + Tview_end · background
```

τ/W/투과율은 무차원, extinction은1/m, 적분 거리는m이다. 대기 및Tone은 이 HDR 산란 결과 뒤의 기존 경로를 따른다.
밝은 면/어두운 면의 공간 배치는 각 표본에서 태양까지의 누적 차폐가 담당한다.
다중 산란은 빛이 여러 번 방향을 바꾸어 두꺼운 내부를 비추는 항이며, 주된 목적이 그림자를 더 만드는 것은 아니다.

현재 CloudEnvironment.hlsli의 direct/sky/ground/multiple 합산 구조는 이 분리와 이미 유사하다.
문제 원인을 합성 방식 자체가 틀렸다고 단정할 근거는 없다.
CloudLighting.hlsli의 직접광은 Tsun^shadowExponent를 사용한다.
`Tsun^k=exp(-k·τsun)`이므로 지수를 올리는 것은 직접광 계산에서 광학 깊이를 강화하는 것과 같다.
이번 사용자 결과는 이미 존재하는 차폐 차이가 강화되었음을 보여 준다. 실제 밀도/캐시가 잘못되었다는 증명은 아니다.

PhaseFunction.hlsli는 정규화한 viewRay와 directionToSun의 내적을 사용한다.
거리 감쇠가 아니라 각도별 산란이다. 태양을 보는 방향 주변이 넓게 밝아져 포인트 라이트처럼 느껴질 수 있다.
이 설명은 현재 코드에 대한 해석이다. Phase는 구름 덩어리의 국소 굴곡 방향을 직접 계산하지 않는다.

## Height mask 제안의 적용 범위

**ambient의 위/아래 톤을 만드는 용도로는 실제 사례가 있다.** 현재 코드도 localHeightFraction으로
`skyWeight=lerp(1,height,ambientHeightInfluence)`와 `groundWeight=1-height`를 사용한다.
따라서 새 height mask를 더하는 것만으로 이번 문제가 크게 해결된다고 기대하기는 어렵다.
특히 사용자 보고처럼 ambient 기여가 작다면, 그 항의 공간 가중치를 강화해도 최종 영향은 제한적이다.

고정 월드Y mask는 태양 방위각을 구분하지 못한다. 태양 고도에 따라 위/아래 색만 섞는 mask 역시
같은 높이에서 태양을 향한 옆면과 반대 옆면을 구분하지 못한다.
태양 방향으로 mask 축을 돌리려면 각 구름 덩어리의 중심/크기 또는 국소 형상 정보가 필요하다.
무한 반복 PlanarLayer에서 단순 dot(worldPosition,sunDirection)을 쓰면 구름과 무관한 거대한 띠가 생길 수 있다.

선택지는 다음과 같다. 아래는 공개 자료의 구현을 그대로 재현하는 것이 아닌 이 프로젝트에 대한 제안이다.

| 목적 | 적합한 입력 | 한계 |
|---|---|---|
| 안정적인 위/아래 ambient 톤 | 기존 local height + ambient visibility | 태양 방위/돌출부 자기 차폐를 대신하지 못함 |
| 태양 방향에 따른 내부 명암 | 표본→태양 누적 optical depth | 실제 적분/캐시의 공간 정확도 확인 필요 |
| 더 스타일화된 돌출부 명암 | 부드러운 Base 밀도 기울기와 태양 방향의 약한 보정 | 부피의 물리 법선이 아니며, 강하면 고체 표면처럼 보임; 추가 조회 비용 |

밀도 기울기를 쓰더라도 최종 화면/투과율 전체에 곱하지 않고 선택한 조명 항의 약한 보정으로 분리해야 한다.
이 후보는 새 기능이므로 현재 승인된 High 비용/형상 조건과 성능을 따로 검증해야 하며 아직 구현하지 않았다.

## 권장하는 다음 진단 순서

1. 형상/밀도/카메라 고정. Phase0, Sky/Ground0, Multiple0으로 실제 태양 차폐만 관찰한다.
   이 모드는 진단용이며 최종 조명값으로 채택하지 않는다.
2. Shadow1/1.35/1.62/2/3/4를 비교한다. 4가 좋다는 느낌을 기본값 선택과 구분하고 과도하게 검어진 영역도 함께 본다.
3. 단순한 고정 밀도 덩어리에서 알려진 빛 경로 길이의 τ와 캐시 τ를 비교한다.
   정상 런타임의 step/skip/cone 상수는 바꾸지 않는 별도 진단으로 진행한다.
   CPU/GPU 태양 방향 전달이 맞는 것과 차폐 적분의 공간 분포가 정확한 것은 별개 검증이다.
4. 이 검증이 맞으면 직접광 기준을 먼저 확정하고, 기존 height 기반 ambient와 다중 산란을 필요한 만큼 복원한다.
5. 그래도 원하는 회화적 굴곡이 부족할 때만 저주파 밀도 기울기 기반 방향 보정이나 powder를 독립 후보로 검토한다.
   고정 height mask만으로 태양 방향 명암을 대체하는 방식은 주 경로로 권하지 않는다.

판정은 자동 수치와 사용자 화면 품질을 분리한다. Phase 상한 강화는 현재 관찰상 넓은 밝기 변화를 만들므로
내부 명암의 우선 해결책으로 채택하지 않는다. 세로줄 현상은 별도 원인 미확정 상태를 유지한다.

## 태양 차폐와 각도 의존 줄무늬 — 2026-09-15

사용자 추가 관찰: 특정 각도에서 내부 그림자가 대각선 또는 수평 평행 줄무늬로 보인다.
05 다음 단계로 진행하기보다 태양 차폐의 정확성/입력값을 먼저 확정하는 방향을 논의했다.
이번에는 원인 후보와 공개 사례를 조사했으며 줄무늬를 특정 원인으로 확정하거나 코드를 변경하지 않았다.

### 현재 코드의 사실과 가설

CloudDeepShadow는 태양 평면512²격자, Near80/Far40개의 월드Y slice를 사용한다.
Near24km/512=46.875m,Far128km/512=250m의 태양 평면 texel 폭이다.
높이 간격 Δh에 대해 태양광선 적분 간격은 Δs=Δh/max(sunY,minimumSunY)이다.
캐시가 활성인 낮은 태양에서 간격이 커진다. 예를 들어18°에서 약3.24Δh,70°에서약1.06Δh다.
특정 각도의 줄무늬에 대한 우선 조사 후보지만 현상과의 인과관계는 아직 확인하지 않았다.
Stage12SampleTau는 UV 선형 조회와 인접 높이2slice의 수동보간을 이미 한다.
따라서 보간이 빠졌다고 단정하면 안 된다. 보간은 이미 적분에서 놓친 고주파 밀도를 복원하지 못한다.
View 또한 구간 중점 표본과 거리별 step을 사용한다. 최종 화면 줄무늬는 이 경로에서도 생길 수 있다.
규칙적 줄무늬는 격자/샘플링 aliasing을 우선 의심할 단서다. 앞뒤 밀도 중첩만으로 정상 현상이라 판정하지 않는다.

### 공개된 처리 사례

- [Horizon SIGGRAPH2015 PDF p84](https://advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf):
  적은 빛 표본을 태양 방향 cone 안에 분산해 밴딩을 완화한다. 주변 밀도를 평균하므로 그림자도 부드러워질 수 있다.
- [Wronski SIGGRAPH2014 PDF p58~59](https://advances.realtimerendering.com/s2014/wronski/bwronski_volumetric_fog_siggraph2014.pdf):
  매질 격자의 jitter는 규칙적 aliasing을 고주파 noise로 바꾸고 공간/시간 필터로 완화한다.
  이것은 안개 사례이며 현재 구름 캐시 버그에 그대로 적용된다는 뜻은 아니다. Jitter 단독은 해결 완료가 아니다.
- [Real-Time Samurai Cinema, cloud density AA](https://advances.realtimerendering.com/s2021/jpatry_advances2021/index.html):
  해당 paraboloid cloud texture 모델에서 좌표 미분이 큰 곳의 밀도를 조절해 aliasing을 줄인다.
  이 프로젝트의3D density에 그대로 이식하는 처방이 아니라 표본 크기에 맞춘 신호 제어의 사례다.
- [Epic 공식 구름 문서](https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-in-unreal-engine):
  그림자/시선 샘플 예산과 shadow map 해상도·범위를 별도로 조절한다. 실제 원인에 해당하는 축을 조절해야 한다.

### 다음에 분리할 검사

1. 시간/바람/구름 고정. 태양만 회전한 비교와 카메라만 이동/회전한 비교를 나눈다.
2. 위상/간접광을 중립화하고 직접광, View 투과율, 가시 기여 SunT를 비교한다.
   Visible SunT도 View 적분을 공유하므로 거기에 줄이 있다고 캐시 원인으로 바로 확정하지 않는다.
3. 캐시 원시tau slice/높이 단면 및 고정 월드 표본의tau를 시선 적분과 독립적으로 확인한다.
   단일 slice가 매끄러워도 여러 높이 사이에서 생기는 문제는 남을 수 있다.
4. 별도 고정밀 참조에서 View 간격, 빛 적분 간격, 캐시UV/높이 해상도를 각각 분리 비교한다.
   기존8tap cone 역시 근사이므로 단독 정답 기준으로 사용하지 않는다.
5. 실패 위치를 수정하고 밴딩을 재검증한 뒤 shadowExponent/광학 두께의 최종값을 선택한다.
   강한 exponent가 작은 차폐 오차까지 더 잘 보이게 할 수 있으므로 값 확정 전에 줄무늬를 확인한다.

높이 마스크는 ambient의 기본 톤 보조로 유지한다. 새 mask나 blur로 줄무늬를 먼저 가리지 않는다.
런타임 Balanced512/High step·skip·early exit·cone 상수, Temporal 폐기 계약을 현재 유지한다.
고정밀 참조 검사는 제품 품질 상수를 바꾸는 작업과 분리하며, 아직 구현하지 않은 진단 제안이다.

## 06 림 계산의 근거와 적용 — 2026-09-15

[Guerrilla 원 발표 PDF](https://d3d3g8mu99pzk9.cloudfront.net/AndrewSchneider/The-Real-time-Volumetric-Cloudscapes-of-Horizon-Zero-Dawn.pdf)의 p54~55는 전방 산란/HG와 실버 라이닝의 관계를, p63~68은 Powder의 어두운 가장자리와 시선 의존성을 설명한다. 따라서 Powder를 밝은 윤곽선 공식으로 취급하지 않는다. [Epic 공식 속성 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-properties-in-unreal-engine)는 Phase와 Multi Scattering Contribution/Occlusion/Eccentricity를 분리한다. 계획에 있던 material URL은 이번 조회에서 열리지 않아 같은 엔진의 공식 속성 문서로 항목을 확인했다. Epic의 방향 부호는 본 프로젝트의 카메라→표본/표본→태양 내적 정의에 그대로 복사하지 않는다.

프로젝트 적용은 위 자료의 전체 모델 복제가 아니라 사용자 승인식에 따른 조절 분리다. Tsun으로 차폐한 직접광을 기본 부분과 양의 phase 증가분으로 나누고, 증가분에만 림 강도·깊이·별도 상한을 적용한다. Multiple은 기존 P0를 사용한다. 에너지를 재정규화한 완전한 물리 산란 모델은 아니므로 과한 gain의 백색화/그림자 소실은 사용자 기각 조건이다. 태양광이0이면 림도0이고 별도 화면 윤곽선이나 Bloom을 더하지 않는다.

현재 Phase intensity .20와 raw HG 상한16이면 적용 전 phase의 최댓값은 1+(16−1)×.20=4다. 그러므로 림 cap4와8은 이 고정 조건에서 동등하다. cap8의 효과를 억지로 만들기 위해 기존 Phase intensity를 올리지 않는다. F4 Silver의 L/(1+L) 표시는 실제 HDR 림 값과 다르므로 역변환 근사·Composite HDR·최종 sRGB 통계를 구분한다. 사진의 어두운 부분 주변 밝기를 반사광이라고 단정하지 않는다.

## 2026-09-16: 태양 노출과 얇은 경계 구분

### 공개 근거

- Guerrilla SIGGRAPH 2015, PDF p51~57: Beer 감쇠와 HG 전방 산란을 결합한다. p59~68의 Powder는 빛을 향한 어두운 가장자리와 내부 산란을 근사하는 별도 효과다. 밝은 외곽선 검출기로 그대로 도입할 근거는 아니다. https://d3d3g8mu99pzk9.cloudfront.net/AndrewSchneider/The-Real-time-Volumetric-Cloudscapes-of-Horizon-Zero-Dawn.pdf
- Epic Volumetric Cloud Material: Phase A/B/Blend는 방향성, Multiscatter는 별도 산란 근사이며 Noise/Bias/Strength는 밀도 형상을 제어한다. 이름이 phase라는 이유로 경계를 직접 검출하는 것이 아니다. https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-material-in-unreal-engine
- PBRT 4e Transmittance: 광학 두께는 경로상의 소멸계수 적분, 투과율은 exp(-tau)다. 두 구간의 투과율 곱은 연결 경로의 광학 두께 합에 해당한다. https://pbr-book.org/4ed/Volume_Scattering/Transmittance

### 프로젝트 분석과 가설 (공개 구현과 구분)

현재 common은 ViewT×(1-exp(-density×sigma×ds)), 직접광 차폐는 SunT^1.35, 림 추가 가중치는 (1-edgeInfluence)+edgeInfluence×SunT^(2×Depth1)이다. 따라서 물리적인 시선 감쇠는 이미 있다. 부족한 것은 림 전용 가중치가 보는 정보다. 같은 SunT이면 앞에 구름이 얼마나 놓였는지와 무관하게 같은 가중치가 나온다. 또 edgeInfluence<1이면 추가 가중치에 잔여값이 남는다. 이는 직접광이 무차폐라는 뜻은 아니다.

얇은 경계는 최소 세 개념을 구분한다: 낮은 국소 밀도, 짧은 View chord, 카메라→표본→태양 경로의 낮은 광학 두께. 낮은 밀도도 길게 겹치면 두껍고, 앞부분의 높은 ViewT도 뒷부분까지 얇다는 뜻이 아니다. 역광에서 정렬된 광로는 tauView+tauSun가 전체 chord에 가까워지지만 측광에서는 꺾인 경로다. 현재 View Detail/Shadow Base 차이와 캐시 오차도 이 근사를 제한한다.

샘플 후보: A 기존 가중치, B 추가 림 가중치 제거(기본 Beer 감쇠는 유지), C 추가 가중치의 입력만 midpoint ViewT×SunT로 교체. C는 추가 감쇠를 더하는 아트 조절 실험이며 물리 정답 또는 실무 표준으로 인용하지 않는다. 새로운 광선/밀도 조회/법선/화면 외곽선 없이 기존 표본값을 쓴다. 기본 직접광·Sky·Ground·Multiple은 유지한다. C가 단순 어둡게만 만들면 채택하지 않는다. 더 큰 phase/intensity로 즉시 보상하면 원인 비교가 불가능하므로 이번에는 고정한다.

실제 얇음만 따로 밝히려면 전체 chord를 얻거나 밀도 경계를 샘플링하는 비용/안정성 문제가 생긴다. 화면 불투명도 마스크는 앞뒤 구름을 합치고 카메라 의존성을 추가하므로 곧바로 일반 구현에 넣지 않는다. 우선 이번 통합 광로 후보의 효과와 한계를 확인한다.

## 2026-09-16 Powder 효과 조사

사용자 판정: 기본 직접광 배율 감소로 환경광/내부 그림자가 두드러지지 않아 k1을 유지한다. .75/.5 후보는 미채택이며 이번에는 조사만 수행한다. 렌더 코드 변경/새 캡처/07 진행 없음.

### 확인된 공개 구현

Guerrilla SIGGRAPH2015 PDF의 인쇄 슬라이드57~66(파일 페이지58~69 부근)은 구름의 둥근 돌출부·가장자리와 안쪽의 산란 차이를 powdered sugar look으로 설명한다. 내부로 조금 들어가면 주변에서 유입되는 산란 기회가 늘지만 깊어지면 감쇠한다는 현상을 저비용으로 근사한다. 태양을 보는 역광의 silver lining은 Beer+HG로, 태양을 등지고 보는 쪽의 어두운 가장자리는 Powder로 구분한다. PDF의 원시 노이즈/Perlin-Worley/Detail 텍스처 설명은 구름 밀도 형상 생성이며 Powder 전용 표면 텍스처를 붙인다는 설명이 아니다.
출처: https://d3d3g8mu99pzk9.cloudfront.net/AndrewSchneider/The-Real-time-Volumetric-Cloudscapes-of-Horizon-Zero-Dawn.pdf (PDF 페이지59~68의 조명 부분)

Unity HDRP17 문서는 erosion 세부 구조가 있을 때 Powder Effect Intensity가 가루 같은 인상을 강조한다고 설명하며 Multi Scattering과 별도 파라미터로 둔다.
출처: https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/volumetric-clouds-volume-override-reference.html

Unity 공식 Graphics 저장소 master의 VolumetricCloudsUtilities.hlsl, PowderEffect 및 EvaluateCloud 확인: 기존 표본 밀도로 증가·포화하는 지수 곡선을 만들고 태양-View 내적 및 강도로 중립값1과 혼합한다. 밀도가 낮을 때 태양 산란을 억제하고 밀도가 높으면 중립으로 돌아간다. 추가 텍스처 조회가 없는 함수다. 해당 구현은 태양 투과율 함수에 들어 있는 여러 산란 octave에 Powder를 곱하고 ambient에는 곱하지 않는다. 따라서 단순히 새 다중산란 RGB를 더하는 효과도 아니다. master는 변경될 수 있으므로 조사일과 함수명을 기록한다.
출처: https://raw.githubusercontent.com/Unity-Technologies/Graphics/master/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/VolumetricClouds/VolumetricCloudsUtilities.hlsl (PowderEffect 163~169, EvaluateCloud 393~416)

### 프로젝트 적용 판단 (아직 구현 아님)

이번 직접광 k는 모든 표본의 기본 직접광을 같은 배율로 줄였지만 Powder는 밀도와 방향에 따라 다른 배율을 준다. 따라서 내부 덩어리·Detail 굴곡의 상대 명암을 바꿀 가능성이 있다. 다만 이것이 실제로 좋아질지는 미검증이다. 밀도 모양 자체, 부족한 화면 해상도나 흐릿한 실루엣을 새로 만들어 주지는 않는다. 차가운 하늘 RGB를 생성하거나 역광 금빛 테두리를 직접 강화하는 기능도 아니다.

현재 local AO는 높은 밀도의 환경광을 억제하고, Multiple은 태양 tau로 내부 산란을 근사한다. Powder와는 제어 대상/입력이 다르지만 근사 효과가 겹칠 수 있어 무조건 기존 Multiple 위에 더하면 안 된다. 새 기능을 실제 산란 해처럼 설명하지 않는다.

후속 샘플 제안: 승인 k1/Rim2/Depth1/Sky.85/Multiple.15와 밀도 고정. 기존 밀도 표본을 재사용하는 Powder 강도0/.25/.5를 먼저 검사한다. 우리 cosTheta=dot(camera→sample,sample→sun)는+1이 역광이므로 그쪽은 중립1로 보내고 순광(-1)에서 효과를 크게 한다. PDF/엔진별 방향 부호를 그대로 복사하지 않는다. 순광·측광이 주 비교이며 역광은 기존림 유지 검증이다. 실제 Unity 계수는 밀도 범위가 다른 이 프로젝트에 그대로 맞는다고 가정하지 않는다. density에 ds를 임의로 곱해 step 길이에 따라 효과가 달라지지 않게 한다.

초기 원인 분리 후보는 BaseDirect에만 Powder 가중치를 적용하고 Rim/Sky/Ground/Multiple을 고정하는 최소 실험이다. 이는 Unity의 태양 산란 전체 적용과 다르며 원인 분리용 설계다. 그 효과가 기존 Multiple에 가려지는 경우에만 태양 유래 산란 전체 적용을 별도 비교한다. 두 방식을 섞어 한 결과로 제시하지 않는다. 네온 윤곽·얇은 구름 소실·검은 껍질·과장된 노이즈가 나타나면 기각한다. 일반 기본값으로 채택하기 전에 gallery.html에서 사용자 비교가 필요하다.
