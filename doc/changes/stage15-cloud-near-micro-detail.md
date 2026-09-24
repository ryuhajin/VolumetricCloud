# Stage 15 — E19 근경 미세 Detail (2026-09-24 런타임 채택, 타입별 F2 슬라이더)

2026-09-24. 사용자 승인 계획: 근경 선명·원경 흐림으로 원근감을 키운다. 원경 대기 2배는 사용자 결정으로 유지한다.
앞 절들은 시험 당시의 기록(당시 "일반 실행 불변/판정 대기" 표현 포함)이고, 현재 동작은 맨 아래 "런타임 채택" 절이 기준이다. 타입별 최종 값과 화면 승인은 사용자 대기다.

## 왜 이 실험인가 — 기존 E01–E18과 다른 점

- 현재 가장 가는 형상 주파수는 Detail fBm 4× 대역(2000m tile/20 cycle = 파장100m)이다.
  2km 거리 픽셀은 약2.1m이므로 가장 가는 굴곡도 약47px에 걸친다. 가까이 갈수록 형상 정보가 부족하다.
- E02(해상도)·E06(크기 전역 변경)·E08(step)·E18(주파수 구성)은 모두 **같은 2km tile 안의 정보량**을 바꿨다.
  E19는 가까운 표본에만 **더 작은 주기의 두 번째 조회**를 더하고, 화면 footprint가 커지면 0으로 사라지게 한다.
- 13-5의 옛 Detail 거리 LOD(원경 Detail→평균 대체, 성능 목적)와 반대 방향이다. 원경 계산은 바뀌지 않는다.

## 식 (시험 매크로 `VCLOUD_TEST_NEAR_MICRO_TILE`가 있을 때만 컴파일)

```
micro   = dot(Detail64.SampleLevel(frac(p/tile + (.37,.61,.13))), detailWeights)   // 새 리소스 없음
w       = 1 − smoothstep(tile/64, tile/32, t · pixelAngle)                          // 화면 footprint 기준
detail' = saturate(detail + s · w · (micro − mean))                                 // 평균0 섭동
```

- `mean`은 실행 중 Detail 텍스처 texel 가중합의 실제 평균이다(0.450834, std 0.056369).
- `pixelAngle`은 픽셀 중심과 한 픽셀 아래 ray의 방향 차이(1920×1080/FOV60 중앙 1.068mrad).
- `w=0`이면 추가 fetch가 없고 기존 연산과 같다. 가중치0 거리: tile500=14.6km, tile250=7.3km(화면 중앙).
- Light/Deep Shadow/NoiseLab/대표 진단 표본은 3인자 호출(가중치0)을 유지한다. 태양 차폐는 계속 Base다.
- 파일: `shaders/Noise.hlsli`(`SampleNearMicroDetail`, 4인자 `SampleCloudDensity`),
  `shaders/VolumetricClouds.hlsl`(`NearMicroWeight`, 시험 debugMode130 거리 덤프).

## 조건과 후보

Cumulus/환경3, 1920×1080, FOV60, time71, 바람0, 대기2배, Base 그림자, High 100m/512, RTX 4080 SUPER, Release.
Near75/F5/F6과 E16/E18과 같은 ROI(외곽/골/몸체).

| 후보 | tile | s | 근경 step |
|---|---:|---:|---|
| c0 | — (현재 일반) | — | 100m |
| c1 | 500m | 1 | 100m |
| c2 | 250m | 1 | 100m |
| c3 | 250m | 2 | 100m |
| c4 | 250m | 2 | 50m(5→15km 복귀, E08 매크로 재사용) |

계획 단계의 500m/.15·.30, 800m 후보는 실제 채널 주파수(2/3/4/5 cycle/tile, R 가중치 .5)와
Detail 분산(std .056)을 확인한 뒤 위 값으로 바꿨다. s=1은 미세 대역이 기존 Detail과 같은 분산을 더한다는 뜻이다.

## 자동 검증 결과 (VALIDATION=PASS)

- **원경 불변:** 진입 거리가 가중치0 거리보다 먼 픽셀은 c0과 같다.
  F5 c1/c2/c3 111,630/298,884/298,884픽셀 모두 완전 일치. F6 최대 차이 0.00031(99%+ 완전 일치).
  같은 식을 s=0으로 다시 컴파일해도 F6 차이 0.00031이 나오므로 컴파일 차이 바닥이다.
- 프리셋 8개 불변, Base/Detail 텍스처 해시 불변, 진입 상태 복원 일치.
- **ROI 고주파(3×3 잔차 RMS, AP-off 정규화)** Near75 외곽: c0 .00051 → c1 .00179 / c2 .00516 / c3 .01038 / c4 .00690.
  F5/F6의 원거리 ROI는 c2–c4에서 불변(c1은 F5 외곽 ROI가 14.6km 안이라 변함).
- **평균 Alpha**는 섭동이 평균0이어도 remap 비선형 때문에 소폭 증가(Near75 외곽 .371→.393 c1, .432 c3).
- **이동(Near75 인접 프레임 ROI Alpha MAE, 전진):** c0 .0072, c1 .0091, c2 .0136, c3 .0225, c4 .0158.
  의도한 이동을 포함하므로 깜빡임 점수가 아니다.

## 성능 (정/역 순서 두 회차 중앙값 평균, Cloud ms)

| 시점 | c0 | c1 | c2 | c3 | c4 |
|---|---:|---:|---:|---:|---:|
| Near75 | 1.00 | 1.00 | 1.07 | 1.09 | 2.24 |
| F5 | 2.68 | 2.89 | 2.74 | 2.81 | 3.77 |
| F6 | 3.14 | 3.71 | 3.59 | 3.61 | 5.11 |

모든 case Cloud p95 ≤ 6.3ms, Frame p95 ≤ 6.8ms로 예산(10/16.67ms) 통과. 첫 실행의 한 방향 측정은
후보 순서 편향(c0이 c1보다 느리게 측정)이 있어 판단에 쓰지 않는다. 12 case 전체 성능 gate는 채택 시 별도 실행한다.

## 화면 관찰 (에이전트 기록, 사용자 판정 전)

- **c1:** 근경 외곽이 콜리플라워형 굴곡으로 또렷해진다. F5에서 중거리 구름에 질감이 생기고 수평선 구름은 그대로라 원근 차이가 커진다.
  일부 중거리에서 미세한 알갱이와 사선 번짐이 보인다.
- **c2:** 더 잘게 부서지고 천정 부근에 약한 방사 무늬가 생긴다.
- **c3/c4:** 천정점을 중심으로 한 강한 방사형 줄무늬. c4(50m step)에서도 줄지 않으므로 광선 방향 표본 부족이 아니다.
  광학적으로 얇은 층을 올려다보면 한 광선이 수평면에서 방사 방향 선분을 따라 미세 노이즈를 평균하기 때문으로 해석한다(가설).
  미세 굴곡이 표면에서만 보이려면 해당 위치가 충분히 불투명해야 한다. c3/c4는 기각 후보로 기록한다.

## 한계와 다음 판단

- Cumulus 한 타입, 정지·짧은 이동, 태양 한 각도. Stratus/Altocumulus/Custom과 태양각은 미검증.
- c1의 알갱이·사선 번짐, fade 경계(약7–15km) 이동 팝핑은 사용자가 재생 페이지로 판정해야 한다.
- 채택하면 tile/s/fade 계수를 `HighCloudQuality` CPU·HLSL 상수로 고정하고 CB/프리셋 스키마는 늘리지 않는다.
  그때 `doc/PERFORMANCE.md`의 "Detail은 모든 거리 유지" 문구와 렌더링/레이마칭 문서를 갱신한다.
- 형상은 생겼는데 명암이 평평하면 [근경 선명도 문서](stage15-cloud-near-clarity.md)의 미실행 제안4(tauHybrid)를 다음 후보로 둔다.

## 2026-09-24 사용자 exe 확인과 후속 스위치

사용자는 실행 중인 exe에서 `shaders/HighCloudQuality.hlsli`에 시험 매크로를 넣어 핫 리로드로 확인했다(TILE 1130, s .7–.8, 여러 타입).

| 관찰 | 확인 | 해석 |
|---|---|---|
| s1(500m)은 인위적으로 반복됨 | TILE 1130/s .8로 완화 | 작은 tile의 반복과 2000/500 정수비 격자 정렬 |
| 근경이 모션 블러처럼 앞/옆으로 흔들림(모든 타입) | 근경 step 25m에서도 육안상 비슷 | **광선 방향 표본 부족 가설 기각**. 같은 Worley 텍스처를 두 크기로 겹친 잔상, 얇은 층 투과 평균이 남은 가설 |
| 얇은 Stratus에서 격자선 | s 0이면 없음, .1–.2 괜찮음, .3부터 서서히, 1.0으로 진해짐 | **미세 대역 원인 확정**. 64³ 텍셀(약18m)을 크게 확대한 선형 보간 꺾임이 remap `÷(1−e)`로 증폭된다는 가설 |

추가 시험 스위치(기본 0, 일반 실행 불변):

- `VCLOUD_TEST_NEAR_MICRO_ROTATE 1`: 미세 좌표에 축과 어긋난 고정 직교 회전을 적용한다.
- `VCLOUD_TEST_NEAR_MICRO_SOURCE 1`: 텍스처 대신 quintic 보간 2옥타브 gradient noise(4·8 cycle/tile, PCG3D 해시)를 쓴다.
  텍셀 격자·타일 반복·Worley 겹침이 없다. 표준편차 0.142365(40만 표본, `tests/NearMicroNoiseStats.py`)를
  Detail 표준편차 0.056369에 맞추는 배율 0.395943을 곱해 strength 의미를 SOURCE0과 같게 둔다. MEAN은 쓰지 않는다.
- 네 조합과 무매크로 Cloud PS, Deep Shadow CS, NoiseLab PS를 fxc로 컴파일 확인했다. 화면 판정은 사용자 대기다.

사용자 2차 확인: ROTATE만 켜도 Stratus 격자선이 확실히 줄고, SOURCE1만 켜면 더 줄었다(텍스처 확대 보간 가설 지지).
대신 SOURCE1은 외곽이 스펀지·스프레이처럼 파편화된다. 그래서 `VCLOUD_TEST_NEAR_MICRO_FINE`(기본 .333)을 추가해
가는 옥타브(8 cycle/tile) 비중을 조절한다. 단일 옥타브 σ1=0.190687, 옥타브 상관 +0.003이므로
배율 = 0.056369/(σ1·√((1−f)²+f²))로 계산해 FINE을 바꿔도 strength의 의미(Detail 표준편차 대비 배수)는 유지된다(예측 오차 0.15% 이내).

사용자 3차 확인: FINE 0과 .15는 비슷하다. Worley 두 겹이 gradient noise보다 훨씬 자연스럽지만, 같은 텍스처를 회전해 쓰면
격자가 계속 보인다. TILE 830이 근경을 뚜렷하게 만든다는 의견이다. 사용자는 미세 굴곡 전용 Worley 생성을 제안했다.

- 판단: 격자선의 원인은 같은 노이즈 재사용보다 64³ 텍스처의 확대 보간이다. 같은 64³로 새 텍스처를 만들면 TILE 830의 텍셀이 약13m라 격자가 남을 수 있다.
- `VCLOUD_TEST_NEAR_MICRO_SOURCE 2`를 추가했다. Detail 생성기와 같은 Worley 거리 정의(3×3×3 이웃, sqrt(d²)/1.15)를
  셰이더에서 직접 계산하며 해시(PCG3D)가 달라 Detail과 다른 무늬다. 기저 3 cycle/tile, 1×/2×/4× .625/.25/.125.
  평균 0.452447, 표준편차 0.102790(15만 표본, `tests/NearMicroNoiseStats.py worley`)으로 평균0·Detail 표준편차에 맞춘다.
- fxc: Cloud PS 약 3,419 → 4,618 명령(+1,200), 컴파일 약 6초. Deep Shadow CS/NoiseLab PS 컴파일 확인. GPU 비용은 미측정이다.
- 모양이 확정되면 같은 식을 전용 Texture3D(128³ 이상)로 구워 비용을 줄이고, 그때 격자 재발 여부를 다시 확인한다.

## 2026-09-24 전용 미세 Worley 텍스처 64³/128³ 비교

사용자 4차 확인: SOURCE 2에서 격자선이 생기지 않았고 ROTATE는 없는 편이 낫다. 기준 TILE 830, STRENGTH .5, MEAN .450833833919.

**시험 전용 런타임 추가(채택 전 임시, 해상도 결정 후 하나로 줄인다):**

- `shaders/NoiseVolume.hlsl` `CSNearMicro`: SOURCE 2와 같은 식을 주기 Worley(`PeriodicWorleyDistance`, seed 1337+5003)로
  단일 채널 R8에 굽는다. u1 전용, b6 미사용, 해상도는 컴파일 정의 `VCLOUD_NEAR_MICRO_BAKE_RESOLUTION`.
- `src/Renderer.*` `GenerateNearMicroVolumes`: 첫 `Render`에서 GPU 계측 구간 밖에 한 번 64³(256KiB)·128³(2MiB)를 굽고
  texel 평균/표준편차를 디버그 출력에 남긴다. 구름 PS에 t14(64³)/t15(128³)로 묶고 해제 범위를 t0–t15로 넓혔다.
  실패해도 일반 렌더는 계속되며 일반 셰이더는 t14/t15를 선언하지 않는다. NoiseVolume 핫 리로드 때 다시 굽지 않는다.
- `shaders/Noise.hlsli` SOURCE 3(t14)/4(t15): 실측 평균 .456036/.456037, 표준편차 .107505/.107501로 평균0·Detail 표준편차에 맞춘다.
- 실행기: `VCLOUD_NEAR_MICRO_SET=bake`(c0 현재/c1 SOURCE2/c2 64³/c3 128³), `VCLOUD_NEAR_MICRO_TYPE=stratus`,
  `VCLOUD_NEAR_MICRO_STATS_ONLY=1`. 검증 셰이더 사본의 `HighCloudQuality.hlsli`에 활성 `VCLOUD_TEST_NEAR_*` define이 있으면 비교를 거부한다
  (사용자 exe 확인용 define이 빌드 때 사본으로 복사되면 c0이 현재 화면이 아니게 된다).

**결과(RTX 4080 SUPER, Release):** Cumulus/Stratus 캡처 VALIDATION=PASS. 원경 불변은 모든 후보 완전 일치(F5 39,088/84,233, F6 51,246/110,000픽셀),
s0 계약 max 0. Cumulus Near75 ROI 지표와 3배 확대 crop에서 64³와 128³는 거의 같고 격자가 보이지 않았다(Stratus Near75는 고정 ROI가 빈 하늘이라 crop으로 확인).

정/역 두 회차 Cloud 중앙값 평균(ms):

| 시점 | c0 현재 | c1 셰이더 Worley | c2 구운 64³ | c3 구운 128³ |
|---|---:|---:|---:|---:|
| Near75 | 1.00 | 5.78 | 0.96 | 0.98 |
| F5 | 2.60 | 11.89 | 2.74 | 2.92 |
| F6 | 3.14 | 15.59 | 3.74 | 3.97 |

셰이더 계산 Worley(SOURCE 2)는 F5/F6 Cloud p95 15.4–21.4ms로 예산 10ms를 넘는다. 기준 화면 용도로만 쓴다.
128³는 64³보다 F5/F6에서 약 .2ms 느리다(추정: 텍스처 캐시 적중 감소). 해상도 선택은 사용자 exe의 얇은 Stratus 근경 격자 판정 대기다.
결과: `build/captures/cloud-near-micro/7760-10286078`(Cumulus), `17116-10323750`(Stratus), `27008-10407062`(성능 전용).
확대 crop은 `tests/CropPngZoom.py`(표준 라이브러리 PNG 디코더)로 만들었다.

## 2026-09-24 격자 원인 재판단과 DUAL 조회

사용자 5차 확인(Release exe, TILE 830/s .5/SOURCE 3): **격자가 보이고 SOURCE 3/4 차이가 없다.** 64³ 채택 의견.

- 이전 가설(텍셀 확대 보간 꺾임)은 **기각**한다. 그 원인이면 128³에서 선 간격이 절반이 되거나 약해져야 한다.
- 남은 차이는 반복 여부다. SOURCE 2(비주기, 격자 없음) 대 SOURCE 0/3/4(주기 텍스처, 격자 있음). SOURCE 1(비주기)과 ROTATE에서
  격자가 줄었던 이전 관찰도 같은 설명과 맞는다. 구운 무늬의 세 옥타브(3/6/12 cycle)가 모두 같은 TILE의 정수배라
  830m마다 같은 무늬가 X/Y/Z 축 격자로 반복되고, 넓고 얇은 Stratus에서 그 반복이 격자로 보인다는 판단이다(가설, 선 간격 실측 전).
- `VCLOUD_TEST_NEAR_MICRO_DUAL 1`(SOURCE 3/4 전용): 같은 텍스처를 다른 직교 회전·0.731배 좌표·위상 이동으로 한 번 더 읽어 평균한다.
  두 주기가 서로 나눠떨어지지 않아 합친 무늬는 사실상 반복되지 않는다. 평균은 유지하고 두 무상관 값의 평균이므로 √2를 곱했다(상관 미측정 근사).
  추가 비용은 256KiB 텍스처 조회 1회이며 GPU 시간은 미측정이다. fxc로 SOURCE 3/4 × DUAL 0/1과 무매크로 셰이더를 확인했다.

사용자 6차 확인: DUAL 1에서도 격자가 미세하게 남는다. 사용자는 좌표를 비트는 방식을 요청했고, TILE을 570으로 줄였다
(830은 원경까지 미세 굴곡이 들어가 격자가 더 보인다는 판단. 570의 가중치는 화면 중앙 기준 약8.3km부터 줄어 약16.7km에서 0). 거리는 수정 뒤 재확정한다.

- 샘플마다 난수 offset(jitter)은 Temporal 누적이 없는 현재 런타임에서 정지 화면의 잔 노이즈와 이동 중 깜빡임이 되므로 쓰지 않는다.
- `VCLOUD_TEST_NEAR_MICRO_WARP`(offset 표준편차, tile 단위, 기본 0=끔)와 `VCLOUD_TEST_NEAR_MICRO_WARP_FREQ`(기본 .35 cycle/tile)를 추가했다.
  offset 필드는 quintic gradient noise(비주기, 2차 미분 연속)이고 모서리마다 해시 1회에 성분 순서만 바꾼 세 gradient로 x/y/z를 만든다
  (세 성분이 약간 상관될 수 있다). 같은 위치는 항상 같은 offset이라 정지·이동이 안정적이다. DUAL과 함께 쓰면 두 조회에 같은 비튼 좌표를 쓴다.
- fxc: SOURCE 3 기준 Cloud PS 3,420 → WARP .25 약 3,552 명령(+약130, SOURCE 2의 +1,200 대비 약1/9). GPU 시간과 화면 판정은 미측정/사용자 대기다.

## 재현

```powershell
cmake --build build --config Release --target VolumetricCloudTestRunner
cd build
$env:VCLOUD_NEAR_MICRO_MEASURE_PERFORMANCE=1; .\validation\Release\VolumetricCloudTestRunner.exe --cloud-near-micro-test
$env:VCLOUD_NEAR_MICRO_PERFORMANCE_ONLY=1; .\validation\Release\VolumetricCloudTestRunner.exe --cloud-near-micro-test   # 순서 편향 분리
python ..\tests\AnalyzeNearMicro.py captures\cloud-near-micro\<실행 폴더>   # 표준 라이브러리만 사용
```

결과 폴더(로컬): `build/captures/cloud-near-micro/7060-3654765`(촬영·비교 페이지 `comparison.html`),
`build/captures/cloud-near-micro/17960-3961484`(성능 전용). 둘 다 `tools/ArtifactRetention.json`으로 보호 중이다.

## 2026-09-24 F2 미세 tile 슬라이더·비교 미리보기, 흔들림 조사

사용자 7차 확인: DUAL은 1 고정(0이면 격자가 바로 생김). 사용 중인 값은 DUAL 1, WARP .15, WARP_FREQ .2다(WARP .5는 너무 뒤틀려 구름 같지 않음).
격자는 줄었지만 **근경이 모션 블러처럼 흔들리는 느낌은 남는다**. 사용자는 이것만 잡아도 근경 선명도가 크게 산다고 판단했다.

**UI(시험 전용, 세션 값):**
- F2 "Near micro detail (E19 test)"의 `Near micro tile`(200–2000m, 기본570)은 b6 `nearMicroTileMeters`(offset12, 옛 패딩)에 쓴다.
  셰이더의 `NearMicroTileMeters()`가 이 값(>1m)을 우선하고, 0이면 `VCLOUD_TEST_NEAR_MICRO_TILE` define 값을 쓴다. 비교 실행기는 실행 중 0으로 둔다.
  Formation 편집 플래그에 넣지 않아 재적용·Save Preset·재생성이 없다. `VCLOUD_TEST_NEAR_MICRO_TILE` define은 기능 on/off 스위치로 남는다.
- 같은 수평 월드 정사각형(`Preview extent` 500–8000m, 원점 기준, 층 중간 높이, 바람 없음)에서 세 이미지를 나란히 보여 준다:
  NoiseLab output105 Detail 가중합, 106 미세 텍스처 원본(t14, 회전·DUAL·WARP 없음), 107 실제 적용 섭동(`SampleNearMicroPerturbation`).
  각 값은 자기 평균=.5 회색, ±3σ=검정/흰색으로 표시해 무늬 크기를 비교한다. 미세 대역이 꺼져 있으면 107은 어두운 빨강이다.
- 검증: fxc(NoiseLab/Cloud PS 무매크로·사용자 매크로, Deep Shadow CS), Release/Debug 빌드, Release CTest 45/45. F2 UI 화면은 자동 테스트에서 그리지 않아 사용자 확인 대기다.

**흔들림 원인 조사(웹, 2026-09-24):**
- 실제 액체 구름은 가장자리에서 20–200m 안에 광학 깊이1에 도달한다([AMT 18, 3009, 2025](https://amt.copernicus.org/articles/18/3009/2025/)).
  현재 Cumulus는 σ .003065/m, 외곽 기여 가중 최종 밀도 약 .19(E16)이므로 τ=1까지 약1.7km로 추정된다(추정, 미측정). 사용자 Stratus(σ 1.47e-4/m)는 더 얇다.
  반투명한 층을 km 단위로 겹쳐 보면 깊이가 다른 무늬가 겹치고 이동 시 시차로 어긋나 번짐·흔들림처럼 보일 수 있다는 가설이다.
- HZD 계열 공개 구현은 태양 방향 cone 표본도 고주파 Detail을 포함한 밀도로 계산한다
  ([erickTornero 구현](https://github.com/erickTornero/realtime-volumetric-cloudscapes/blob/master/shaders/RayMarchingFragment.glsl)).
  현재 프로젝트의 태양 차폐는 Base만 사용하므로 Detail/미세 굴곡에 자기 그림자가 없다.
- 표본 부족(jitter+TAA가 표준 해법)은 사용자 25m step 확인으로 주원인에서 제외했다.
- 다음 후보(미실행): ① 불투명도 가중 깊이 분산(픽셀별 σ_depth) 측정으로 가설 확인, ② 소멸계수 배율 비교(F1 Extinction 슬라이더로 코드 없이 확인 가능),
  ③ 미세 대역이 켜진 근경만 태양 방향 짧은 구간을 Detail 포함 밀도로 차폐(기존 제안4 tauHybrid의 근경 한정판).

## 2026-09-24 흔들림 원인 재판단과 Extinction 상한

사용자 8차 확인(F5, 스크린샷 2장):
- Extinction을 .01로 올려도 흔들림이 사라지지 않았다. 반면 **미세 tile 크기**가 더 크게 작용한다. tile 약700m는 중거리 구름이 시선 방향으로 붓질처럼
  늘어나고, tile 약2000m는 거의 없다.
- 해석(가설): 미세 무늬가 "빛이 구름 속으로 들어가 보이는 깊이(광학 투과 깊이 ≈ 1/(σρ))"보다 작으면 한 픽셀이 그 무늬를 시선 방향으로 여러 겹 평균한다.
  수평층을 비스듬히 보면 인접 픽셀의 광선이 같은 방향으로 미끄러지며 평균 구간만 조금씩 바뀌어 무늬가 시선 방향으로 늘어나고, 이동하면 흔들린다.
  σ .01, ρ≈.19이면 투과 깊이 약500m로 tile 700의 굴곡(약90–350m)보다 크고, tile 2000의 굴곡(약250–1000m)과는 비슷하다. 이전 Near75 c3/c4의 천정 방사 줄무늬와 같은 현상이다.
  따라서 근경 미세 굴곡이 선명하려면 투과 깊이를 굴곡보다 작게(σ↑ 또는 가장자리 밀도↑) 하거나 굴곡을 표면에만 두어야 한다.
- `formationrange::extinctionMax`를 .01→.1로 올렸다(F1 슬라이더·Formation 검증·sanitize 공통, 기존 프리셋 범위 안). Release/Debug 빌드, CTest 45/45.

## 2026-09-24 Extinction 상한 복귀와 근경 Detail 태양 차폐 스위치

사용자 9차 확인(F5, Extinction .03, tile 700): 흔들림은 개선되지만 구름답지 않다(내부가 탁하고 깊이감 없는 덩어리).
- 판단: σ .03이면 100m 한 step의 τ가 약.57로 1~2 step 안에 불투명해진다. 다중 산란·환경광 근사는 얇은 광학 두께 기준으로 맞춰져 있고
  태양 캐시는 τ 9.21에서 포화되므로 내부가 어둡고 탁해진다. 높은 소멸계수를 아름답게 쓰려면 조명 모델(다중 산란 옥타브, 표면 근처 step, 캐시 범위)을
  함께 바꿔야 하는 구조 변경이다. 사용자 지시에 따라 **`extinctionMax`를 .01로 되돌렸다**(코드/주석/튜닝 가이드 원복).
- `VCLOUD_TEST_NEAR_MICRO_SHADOW 1`(기본 0): 미세 가중치 w>0이고 Tview>.02인 표본에서 태양 쪽 `SHADOW_LENGTH`(기본 360m)를
  `SHADOW_STEPS`(기본 6) 점으로 나눠 "최종 밀도(Detail·미세 포함) − Base 밀도"를 적분하고 σ·w를 곱해 캐시/cone 광학 깊이에 더한다
  (tauHybrid 근경 한정판, 기존 제안4). 파인 곳은 빛이 더 들고 남은 굴곡은 더 가려진다. 먼 차폐와 원경(w=0)은 그대로다.
  opticalDepth를 [0, stage12MaximumOpticalDepth]로 제한하고 transmittance=exp(−τ)를 다시 계산하므로 직접광·환경광·다중 산란에 함께 반영된다.
  일반 Detail 침식 차이도 포함되므로 STRENGTH 0이어도 근경 Detail 음영은 생긴다. fxc: 사용자 설정에서 3,562 → 3,918 명령(+356).
- 이전 시도와의 차이: 04 Near Detail 그림자(카메라 4–8km 혼합)는 옛 침식식·미세 대역 없음, `VCLOUD_TEST_CACHE_DETAIL`은 47m 캐시 해상도라
  미세 굴곡을 담기 어렵다. 이번 것은 근경 표본별 짧은 직접 적분이다.
- 실행기 `VCLOUD_NEAR_MICRO_SET=shadow`: c0 현재 / c1 사용자 미세 설정(570/.5/64³/DUAL/WARP .15/.2) / c2 = c1 + SHADOW.

**shadow 세트 결과(Cumulus, Release, 정/역 두 회차 Cloud 중앙값 평균 ms):** 캡처 `build/captures/cloud-near-micro/2996-15898687` VALIDATION=PASS,
원경 불변 F5 89,054 / F6 114,097픽셀 완전 일치, s0 계약 0.

| 시점 | c0 현재 | c1 미세(570/DUAL/WARP) | c2 +SHADOW(전 구간) | c2 +SHADOW(6km 제한) |
|---|---:|---:|---:|---:|
| Near75 | 1.00 | 1.75 | 5.40 | 4.88 |
| F5 | 2.78 | 4.20 | 11.94 | 5.10 |
| F6 | 3.24 | 5.44 | 17.19 | 9.55 (p95 약12.2) |

- 화면(에이전트 관찰, 사용자 판정 전): SHADOW는 굴곡별 음영보다 **전체가 밝고 평평해지는** 쪽이다. Detail 침식이 대부분 밀도를 줄이므로
  "최종−Base" 차이가 거의 음수이고, 기존 룩이 Base 차폐 기준으로 맞춰져 있어 차폐가 전반적으로 약해진다. 굴곡 음영을 얻으려면 조명 재조정이 함께 필요할 수 있다.
- 비용: 전 구간(tile 570 약17km)은 F5/F6에서 예산 초과. `VCLOUD_TEST_NEAR_MICRO_SHADOW_MAX_DISTANCE`(기본 6000m, 70–100% 구간 smoothstep)를 추가해
  F5는 예산 안, F6은 p95 약12ms로 아직 초과다. 더 줄이려면 STEPS 3/LENGTH 200/MAX_DISTANCE 3000 등을 사용자 exe에서 비교한다.

사용자 10차 확인: SHADOW 0↔1 차이를 잘 모르겠다(사용자 설정 MAX_DISTANCE 3000, WARP .5).
- 원인 점검: 사용자 셰이더는 오류 없이 컴파일된다. Cumulus 층 바닥 1,774m, F5는 지면 1.7m에서 약22° 위를 보므로 화면 중앙 시선의 층 진입 거리는 약4.7km,
  화면 상단도 약2.3km부터다. MAX_DISTANCE 3000은 2.1–3km에서만 보정하므로 F5에서는 화면 맨 위 옅은 층 바닥 일부에만 걸린다.
- 기본 6km 캡처의 c1↔c2 전체 화면 차이: Near75 평균 6.34/255(8/255 이상 32.1%), F5 4.41/255(21.6%), F6 2.05/255(6.3%). 대부분 전체 밝기 변화다.
- `VCLOUD_TEST_NEAR_MICRO_SHADOW_DEBUG 1`을 추가했다. 일반 화면(debugMode 0) 대신 보이는 불투명도로 가중한 보정량을 출력한다:
  빨강=태양 투과율 증가(밝아짐, ×10), 파랑=감소(어두워짐, ×10), 초록=보정을 계산한 보이는 불투명도. 격리 사본 fxc 컴파일 확인.

사용자 11차 확인(SHADOW_DEBUG, F5 MAX 3000/6000): 초록만 보이고 빨강·파랑이 없다. 초록이 동심원 띠처럼 보인다.
- 실행기 `VCLOUD_NEAR_MICRO_SET=shadow VCLOUD_NEAR_MICRO_SHADOW_DEBUG=1`(c2 Composite를 진단색으로 촬영, 원경 불변 검사는 진단색 때문에 F5에서 의도적으로 실패)
  `build/captures/cloud-near-micro/27492-17156453`: Near75 ROI HDR 빨강 평균 .12–.29(태양 투과율 약+1–3%), 파랑 0.
  F5 ROI는 모두 0, 전체 PNG 빨강 최대 8/255.
- 해석: 보정은 적용되지만 **밝아지는 방향만** 있다(Detail 침식이 밀도를 줄여 최종−Base가 거의 음수). F5처럼 지면에서 보는 근경은 이미 위쪽 구름에
  가려진 아랫면이라 태양 투과율이 0에 가까워 exp 포화로 변화가 거의 없다. 효과는 햇빛을 받는 옆·윗면에만 약하게 생긴다.
- 동심원 띠: 100m 시선 표본이 거리 제한 경계 안팎으로 한 칸씩 끊겨 들어가는 모양이다.
- 판단(에이전트 권고): 비용(F6 +4ms 이상) 대비 굴곡 음영 효과가 없어 SHADOW 0을 권고한다. 채택/제거는 사용자 결정 대기다.

## 2026-09-24 겉면 한정 미세 굴곡(SHELL) 시험

사용자 지시: 근경 Detail 그림자 대신 "미세 굴곡을 구름 겉면에만" 후보를 시험한 뒤 스위치 정리·상수 고정으로 넘어간다.
- `VCLOUD_TEST_NEAR_MICRO_SHELL`(m, 기본 0=끔): 시선이 현재 구름 구간에서 처음 최종 밀도>0을 만난 거리부터 SHELL m까지만 미세 가중치를 유지하고
  50–100% 구간에서 smoothstep으로 끈다. 빈 Base 표본을 지나면 시작점을 초기화해 다음 구름을 새 겉면으로 본다. 불투명도 기준은 얇은 구름에서 겉면을
  구분하지 못하므로 거리 기준을 택했다. 0일 때 smoothstep 0 나눗셈 경고를 막는 안전값을 둔다.
- 실행기 `VCLOUD_NEAR_MICRO_SET=shell`: c0 현재 / c1 미세 700/.5/64³/DUAL/WARP .15/.2 / c2 = c1+SHELL 300 / c3 = c1+SHELL 150.
  캡처 `build/captures/cloud-near-micro/26736-17644437` VALIDATION=PASS(원경 F5 59,345/F6 76,641픽셀 완전 일치, s0 계약 0), 성능 `6720-17677937`.

| 시점 | c0 | c1 미세 | c2 SHELL 300 | c3 SHELL 150 |
|---|---:|---:|---:|---:|
| Near75 | 1.01 | 1.80 | 1.34 | 1.35 |
| F5 | 2.64 | 4.37 | 3.15 | 3.18 |
| F6 | 3.14 | 5.66 | 3.86 | 3.85 |

(정/역 두 회차 Cloud 중앙값 평균 ms. SHELL은 속 표본의 미세 조회를 생략해 비용도 줄인다.)
- 화면(에이전트 관찰): F5 위쪽 얇은 구름에서 c1의 시선 방향 붓질 번짐이 c2/c3에서 거의 사라져 c0에 가까워진다. 대신 그 영역의 미세 굴곡도 대부분 사라진다.
  가까운 두꺼운 구름 외곽의 굴곡은 남는다. Near75 외곽 ROI Alpha 고주파 RMS는 c1 .00025 → c2 .00089로 늘었으나 c0 대비 Alpha MAE는 .004로 작다
  (겉면 경계가 100m 표본 단위로 정해지는 영향일 가능성, 가설). 이동 인접 프레임 MAE는 c0–c3 모두 약 .0072로 같다.
- 판단은 사용자 exe 확인 대기다. 이후 사용하지 않는 스위치를 정리하고 최종 값을 High 상수로 고정한다.

사용자 12차 결정: F2 `Near micro tile` 슬라이더는 고정하지 않고 계속 사용한다(런타임 b6 값 유지). SHELL은 150 전후가 좋다.
두꺼운 구름일수록 SHELL 효과가 티 나지 않는다는 관찰이다.
- 해석: 두꺼운 구름은 겉면 150m 안에서 시선이 거의 불투명해져 SHELL이 끄는 안쪽이 원래 보이지 않는다. SHELL은 속이 비치는 얇은 부분에서만 화면을 바꾼다.
- 기존 시작 조건은 최종 밀도>0이라 아주 옅은 가장자리에서 시작 거리를 세기 시작했다.
  `VCLOUD_TEST_NEAR_MICRO_SHELL_DENSITY`(기본 0)를 추가해 이 밀도를 처음 넘는 표본을 겉면 시작으로 삼는다(그 전 옅은 가장자리는 미세 그대로).
- `VCLOUD_TEST_NEAR_MICRO_SHELL_DEBUG 1`: 일반 화면 대신 근경 표본을 보이는 불투명도로 가중해 분류 출력한다.
  파랑=시작 전 옅은 가장자리(미세 유지), 초록=겉면 안(미세 유지), 빨강=SHELL이 미세를 끈 안쪽. 격리 사본 fxc 확인.

## 2026-09-24 런타임 채택: 타입별 근경 미세 Detail과 시험 스위치 정리

사용자 결정: SHELL은 폐기한다. F2에 tile/mean/strength/warp/warp freq 슬라이더만 두어 구름 타입별로 자유롭게 조정하고,
타입 저장 시 함께 저장한다. 범위는 수치에 맞춘다(예: warp 최대 1, tile 최대 약2000, strength 0~2). F2 tile 슬라이더는 고정하지 않는다.

**최종 구조(시험 매크로 없이 일반 런타임):**
- 텍스처: 전용 미세 Worley fBm 64³ R8(t14, 256KiB), 첫 Render에서 `CSNearMicro`로 한 번 굽는다(실측 평균 .456036/표준편차 .107505).
  굽기에 실패하면 Renderer가 업로드 사본의 strength만 0으로 올린다(사용자 값은 보존).
- 조회: `p=바람 위치/tile`, warp>0이면 gradient noise 벡터 domain warp, `avg(T(p), T(R·p·.731+o))` DUAL 평균 후
  `(v−mean)·(.056369/.107505)·√2`를 `detail`에 `strength·w`로 더한다. `w=1−smoothstep(tile/64, tile/32, t·pixelAngle)`.
  strength 0 또는 원경(w=0)이면 조회 없이 기존 값과 같다.
- 파라미터(모두 타입별 Formation, F2 슬라이더, `FormationSliderFloat` AlwaysClamp):

| 값 | 저장 위치 | 범위 | 기본 |
|---|---|---|---|
| Near micro tile | `CloudFormationSettings.nearMicroTileMeters` → b6 offset12 | 200~2000m(로그) | 570 |
| Near micro strength | `CloudShapeParameters.nearMicroStrength` → b7 offset20 | 0~2 | 0(끔) |
| Near micro mean | `...nearMicroMean` → b7 offset24 | .30~.60 | .456036 |
| Near micro warp | `...nearMicroWarp` → b7 offset28 | 0~1 | .15 |
| Near micro warp freq | `...nearMicroWarpFrequency` → b7 offset32 | .05~1 | .2 |

- 저장: 형성 JSON(schema 4 유지)에 선택 키 `nearMicroTileMeters/Strength/Mean/Warp/WarpFrequency`를 쓴다. 키가 없는 옛 파일은 기본값(strength 0)으로
  읽어 기존 화면을 유지하고, 범위 밖 값은 거부한다(`CloudFormationPresetStoreTests` 왕복·누락·거부 검사 추가). 원본 8프리셋은 자동 변경하지 않는다.
- F2 "Near micro detail": 다섯 슬라이더 + `Reset near micro`, 사라지는 거리 표시, Detail/미세 원본/적용 섭동 비교 미리보기(`Preview extent`).
  슬라이더 변경은 Formation 편집으로 처리되어 F1 Save Preset 대상이 된다.
- 제거: SOURCE 0/1/2/4(Detail 재조회, gradient noise, 셰이더 계산 Worley, 128³), ROTATE, FINE, 근경 Detail 태양 차폐(SHADOW·DEBUG·거리 제한),
  SHELL(·DENSITY·DEBUG), 128³ 텍스처(t15). 실행기는 c0 끔/c1 기본 미세를 런타임 값으로 비교하도록 바꿨다. 기각 근거와 수치는 이 문서 앞 절에 남긴다.
- 사용자 exe용 시험 define은 `shaders/HighCloudQuality.hlsli`를 커밋 버전으로 되돌려 정리했다.

**런타임 채택 검증(2026-09-24, RTX 4080 SUPER):**
- Release/Debug 빌드, CTest Release 45/45·Debug 45/45(디버그 레이어·HotReload 포함), `VolumetricCloud.HighPerformance` 12 case PASS
  (기본값 strength 0, 전체 Cloud p95 4.958ms/Frame p95 5.585ms).
- `--cloud-near-micro-test`(c0 끔 / c1 기본 미세 570/.5/.456036/.15/.2) `build/captures/cloud-near-micro/21564-19674375` VALIDATION=PASS:
  원경 불변 F5 89,054/F6 114,097픽셀 완전 일치, strength 0 계약 max 0(같은 셰이더, 값만 변경), 진입 상태·프리셋·노이즈 해시 복원.
- 성능 전용 `15724-19575671`(정/역 두 회차 Cloud 중앙값): Near75 .99→1.82ms(+.83), F5 2.47→4.14(+1.67), F6 3.12→5.29(+2.17), 최대 p95 6.3ms.
- 형성 JSON 왕복·누락(기본 strength 0)·범위 밖 거부 단위 테스트 추가.
