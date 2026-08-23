# Stage 13-5 / Stage 9 오픈 월드 디버깅 가이드

파일 이름은 과거 13-4B 링크 호환을 위해 유지하지만 내용은 현재 13-5 단일 포트폴리오
씬과 단계 9 최적화 기준이다. 13-4C Local Inspector와 13-4D 단일 씬 이력은 변경 기록에 남아 있으며
현재 런타임에는 scene 전환, 문자 진단 키, F9~F12가 없다. 단계 9 View/Light 기본
최적화의 성능 비교 방법도 이 문서에서 함께 설명한다.

## 1. 첫 실행

1. 프로그램을 실행한다. F5 Hero/Building Depth 카메라와 `Stratus`가 시작 외형이다.
2. `F1`을 열고 맨 위 `Cloud Type Settings`의 현재 상태를 읽는다.
3. 숫자 `0`으로 Composite를 선택한 뒤 F5와 F6을 비교한다.
4. F1에서 `Dense Mixed Default`, `Stratus (층운)`, `Cumulus (적운)`을 차례로 누른다.
5. `F2`를 열어 Weather R 지도를 보고 G mode가 읽기 전용으로 바뀌는지 확인한다.
6. 최종 화면 판정은 사용자가 수행한다. 에이전트 자동 테스트의 finite/hash 통과는 구름의
   미적 품질 승인을 대신하지 않는다.

제목 표시줄에 `Stage 13-5`, 현재 Debug 출력과 `Unified 50km Portfolio Scene`이 보이면
정상이다. 일반 시작은 저장된 Custom을 자동 적용하지 않고 항상 결정적 Stratus를 쓴다.

## 2. 단일 scene과 카메라

- 지면은 Y=0, X/Z `-5000~5000m`의 10km×10km 실제 quad다.
- 원점의 검은 건물은 `20×60×20m`이며 3m×20층 Depth 기준이다.
- 구름층은 `1500~7500m`, View 50km, fade 40~50km, Weather 64km다.
- FOV 60°, near/far `0.1m/60km`이며 지면과 건물은 네 카메라에서 항상 렌더된다.

| 키 | 위치 → 타깃(m) | 무엇을 판정하는가 | 정상 | 실패 징후 |
|---|---|---|---|---|
| F5 | `(0,15,180) → (0,350,-1200)` | 건물 Depth와 hero 외형 | 건물이 뒤 구름을 가리고 좌우 하늘에는 구름이 존재 | 건물 주변 전체가 거대한 빈 Weather 구역 |
| F6 | `(40,2,0) → (40,700,-2200)` | 지면 수평선과 broken-sky | 좌·중·우 구름, 20~40% 푸른 틈 | 한 구역만 구름이 있거나 회색 얇은 띠 |
| F7 | `(40,3000,0) → (40,3300,-1600)` | 구름 내부 | 밀도/투과율이 근거리에서 연속 변화 | 검정, NaN 구멍, 고정 평면 |
| F8 | `(40,7800,0) → (40,6000,-1800)` | 상공 하향 | 가변 상단과 Weather 분포 | 하나의 평평한 천장 |

WASD는 1000m/s, Shift는 4배다. 마우스 왼쪽 드래그는 orbit, 휠은 거리 조절이다.
ImGui 텍스트/숫자 편집 중에는 전역 키가 차단되는 것이 정상이다.

## 3. 13-4E 밀도 계산

Weather는 “어디에 구름이 있는가”, Texture3D는 “안쪽 질량과 경계가 어떤가”를 맡는다.

```text
weatherSupport      = smoothstep(0.02, 0.20, Weather R)
weatherFactor       = lerp(0.70, 1.00, Weather R)
footprintFactor     = lerp(0.80, 1.00, typed footprint)
horizontalCoverage  = global coverage × weatherFactor × footprintFactor
noiseShape          = RemapCoverage(raw noise, horizontalCoverage)
baseDensity         = inside column × weatherSupport × noiseShape
                    × vertical profile × density × Weather B modifier
finalDensity        = DetailErosion(baseDensity)
```

Weather R=0은 비지만 약한 R도 작은 구름으로 남는다. vertical profile은 noise threshold에
다시 곱하지 않으므로 상단이 이중으로 잘리지 않는다. footprint는 20%만 threshold에 반영해
둥근 상단을 만들고, Detail은 이미 생긴 Base 경계만 깎는다.

## 4. Cloud Type Settings

F1의 상태는 다음 다섯 가지다.

| 상태 | 의미 |
|---|---|
| Dense Mixed Default | Full Open World 비교가 복원하는 결정적 혼합형 기준 |
| Stratus | 낮고 넓은 층운 preset |
| Cumulus | 높은 돔과 중심 질량을 가진 적운 preset |
| Custom | 마지막으로 명시 저장한 값 |
| Custom Unsaved | 외형 소유 slider를 바꿨지만 저장하지 않은 값 |

| 값 | Dense Mixed | Stratus | Cumulus |
|---|---:|---:|---:|
| Weather R non-zero/core | 79.62/49.11% | 87.31/56.60% | 73.82/42.07% |
| Coverage | 0.68 | 0.40 | 0.45 |
| Density | 1.15 | 1.20 | 1.25 |
| Extinction `/m` | 0.00035 | 0.00042 | 0.00038 |
| Erosion | 0.18 | 0.12 | 0.18 |
| 두께 | 혼합 1.5~6km | 층운 1.5~2.8km | 적운 3~6km |

Stratus는 낮고 넓은 층과 완만한 상단, Cumulus는 더 높은 돔과 뚜렷한 중심 질량을 보여야
한다. 타입 전환은 Weather seed/period, Texture3D, wind/offset, 카메라·도메인, 태양·Phase·
환경광, View `512×100m`, Light `80×250m`를 바꾸지 않는다.

2026-08-17 사용자 검증에서 기존 Coverage `0.72/0.68`은 양의 밀도 표본을 지나치게 넓혀
Stratus와 Cumulus를 하나의 큰 덩어리처럼 보이게 했다. 새 `0.40/0.45`는 Weather 배치와
광학값을 유지하면서 Base threshold만 좁혀 구름 덩어리 사이의 푸른 틈을 늘린다.

## 5. Custom 저장과 복원

1. F1 Shared cloud parameters에서 Coverage/Density/Extinction, Height profile에서 두께와
   타입 profile, Detail에서 Erosion을 조절한다. F2에서는 threshold/softness, coverage
   bias/contrast, density/thickness link를 조절할 수 있다.
2. 상태가 `Custom Unsaved`와 `Unsaved changes`로 바뀌어야 한다. 자동 저장은 하지 않는다.
3. `Save Current as Custom`을 누른다. 상태가 `Custom`으로 바뀌고
   `captures/noise-lab/custom/noise-settings.json`에 schema 29가 원자 저장된다.
4. Stratus나 Cumulus를 누른 뒤 `Custom`을 눌러 마지막 저장값을 복원한다.
5. 재실행하면 시작 화면은 Stratus다. 그 뒤 Custom을 눌렀을 때 저장값이 복원돼야 한다.

저장하지 않은 상태에서 다른 preset을 누르면 현재 수정값은 대체되며 경고가 표시된다.
파일이 없거나 구버전·손상·NaN/Inf·범위 밖이면 Dense Mixed를 적용하고 이유를 표시해야 한다.
저장 실패도 현재 화면 파라미터를 되돌리면 안 된다.

## 6. 숫자 Debug 출력

상단 숫자와 숫자 패드는 같은 명시적 표를 사용한다.

| 키 | 출력 | 정상 판정 |
|---|---|---|
| 0 | Composite | 배경·구름·Depth가 자연스럽게 합성 |
| 1 | Raw Noise | 하늘 전역에 월드 고정 3D 질량 입력 |
| 2 | Weather Coverage | 20~40% 푸른 틈을 남기는 큰 연속 배치 |
| 3 | Base Density | Weather 안에서 둥근 세로 질량, 바닥/상단 연속 fade |
| 4 | Detail Noise | Base보다 작은 경계 주파수 |
| 5 | Final Density | Base 경계만 침식, 빈 영역은 검정 |
| 6 | View Optical Depth | 중심부가 경계보다 높은 τ |
| 7 | Accumulated Direct | 태양 방향에 따른 밝은 가장자리/어두운 내부 |
| 8 | View Transmittance | 밀집 중심은 어둡고 빈 하늘은 밝음 |
| 9 | Light Transmittance | 자기 그림자 방향이 연속적 |

F1/F3의 Debug View 콤보에는 추가 진단이 있다. `Typed Shape Profile`은
`vertical×footprint`, `Effective Shape Coverage`는 새 horizontal coverage를 표시한다.
문자 키와 Shift/Ctrl 진단 조합, F9~F12가 아무 동작도 하지 않는 것이 정상이다.

## 7. Optimization 버튼과 성능

F1 `Optimization`의 Master는 여러 행을 한 번에 안전한 조합으로 맞춘다. 개별 행의 버튼은
누른 행만 바꾸고 상태를 `Custom`으로 만들기 때문에, 성능 비교를 시작할 때는 먼저 Master를
눌러 기준을 초기화한다. 2026-08-19 단계 9 승인 뒤 일반 시작은 `Balanced`다.

### 7.1 Master preset

아래 p95는 RTX 4080 SUPER, 드라이버 `32.0.15.9186`, Release 1920×1080, VSync/UI/preview
Off, time 0, 120프레임 워밍업 뒤 장면별 GPU Cloud timestamp 600개를 측정한 값이다.
Fine Reference는 실시간 후보가 아니므로 이 측정에서 의도적으로 제외했다.

| Master 버튼 | View / Light 핵심 설정 | 용도 | 7장면 GPU Cloud p95 |
|---|---|---|---|
| Balanced | Search 2×, Exit 1%, `100→150m`, Cone 6·2°·77% | 자동 화질·성능을 통과한 가장 싼 실시간 후보 | 동결 재측정 `7.89~9.89ms`, 최대 `9.89ms` |
| Conservative | Search 2×, Exit 0.5%, `100m fixed`, Cone 12 | 품질 여유가 더 큰 비교 후보 | 동결 재측정 `8.14~16.78ms`, 최대 `16.78ms` |
| Approved Reference | 최적화 Off, `512×100m`, Straight `80×250m` | 단계 13 승인 화면과 비교하는 기준 | 동결 재측정 `12.01~26.72ms`, 최대 `26.72ms` |
| Fine Reference | 최적화 Off, `1024×50m`, Straight `320×62.5m` | 오프라인 화질 정답 생성 | 실시간 성능 측정 제외, 매우 무거움 |

버튼 순서는 계산 예산 기준이다. 실제 GPU ms는 장면의 빈 공간, 조기 종료와 측정 변동 때문에
모든 장면에서 완전히 단조 증가하지 않을 수 있으므로 같은 장면의 p95와 최대값을 함께 본다.

`Fine Reference`는 “더 좋은 실시간 옵션”이 아니다. View 간격을 절반으로 줄여 최대 횟수를
2배로 만들고 Light 표본도 80에서 320으로 4배 늘린 오프라인 기준이다. 따라서 Composite에서
사용하면 프레임 부하가 급증하는 것이 정상이다.

### 7.2 개별 View 비용 버튼

| 행 / 버튼 | 바뀌는 계산 | 상대 비용과 사용법 |
|---|---|---|
| Empty Search `2×` | Base가 3회 연속 빈 뒤 최대 200m 간격으로 후보 탐색 | 빈 공간 Texture3D 조회를 줄이는 승인 후보 |
| Empty Search `Off` | 선택된 View 간격으로 끝까지 Full march | 2×보다 비싸지만 coarse 누락 비교 기준 |
| Early Exit `2%` | 누적 View T가 0.02 이하면 종료 | 가장 싸지만 마지막 2% 배경 기여 손실 가능 |
| Early Exit `1%` | T가 0.01 이하면 종료 | Balanced 승인값 |
| Early Exit `0.5%` | T가 0.005 이하면 종료 | 더 오래 계산하는 Conservative 값 |
| Early Exit `Off` | 불투명해져도 View 끝까지 진행 | 가장 비싼 View 종료 기준 |
| View `100→200m` | 16~48km에서 100m가 200m로 증가 | 가장 싼 거리 step, 단독 화질 승인은 아님 |
| View `100→150m` | 16~48km에서 100m가 150m로 증가 | Balanced 승인값 |
| View `100m Fixed` | 전 거리 100m | 원거리도 같은 품질, 더 많은 View 표본 |
| View `50m Fine` | 전 거리 50m, 최대 1024회 | View 오프라인 기준, 100m 대비 최대 2배 표본 |
| Support Precheck `On` | Weather/높이/profile이 확실히 0이면 Base/Detail을 읽지 않음 | 결과가 0인 곳만 생략하는 저비용 경로 |
| Support Precheck `Off` | 모든 후보에서 Base 경로 평가 | 더 비싼 비교 기준 |

Fast와 Empty Search 4×는 400m deterministic 표본이 Base 최단 파장을 놓쳐 등고선·물결무늬를
만들었으므로 2026-08-19 탈락했고 활성 버튼에서 제거했다.

### 7.3 개별 Light 비용 버튼

Light 계산은 최종 밀도가 양수인 각 View 표본마다 다시 실행된다. 따라서 Light 표본 수 차이는
화면 픽셀 전체에서 중첩되며 View 버튼보다 부하 차이가 크게 보일 수 있다.

| Light 버튼 | 표본 예산 | Cone 6 대비 대략적인 밀도 평가 수 | 용도 |
|---|---:|---:|---|
| Cone 5 | 5 | `0.83×` | 가장 싼 실험값, 자동 화질 승인 아님 |
| Cone 6 | 6 | `1×` | Balanced 승인값 |
| Cone 8 | 8 | `1.33×` | Light 품질 비교 |
| Cone 12 | 12 | `2×` | Conservative 값 |
| Straight 80 | 최대 80 | 최대 `13.3×` | 단계 13 승인 Reference |
| Straight 320 Fine | 최대 320 | 최대 `53.3×`, Straight 80의 4배 | 오프라인 Light 정답 |

`Straight 320 Fine`은 320개의 광선을 만드는 것이 아니라, 밀도가 있는 View 표본 하나마다
태양 방향 직선 광선 하나를 최대 320곳에서 평가한다. 현재 View 최대 512회와 결합한 이론적
상한은 픽셀당 `512×320=163,840` Light 밀도 평가다. Master `Fine Reference`는 View 최대가
1024회이므로 상한이 `327,680`까지 커진다. 실제 실행은 빈 View 표본, 구름층 이탈과 Light
광학 깊이 조기 종료로 줄지만 실시간용으로 사용하지 않는다.

Cone Angle `4/3/2/1°`와 Cone Far Fraction `75/77/85/95%`는 같은 tap 수에서 표본 위치만
바꾸므로 계산량이 같다. 각도와 비율은 그림자 모양·오차 비교용이며 Balanced 승인값은
`2°/77%`다.

성능을 눈으로 비교할 때는 우측 오버레이의 `GPU Cloud Total`을 읽는다. `CPU Frame`은 VSync 대기를
포함하므로 알고리즘 비교값으로 쓰지 않는다. 정식 판정은 VSync Off와 120프레임 워밍업 뒤
`captures/stage9/performance.json`의 p95를 사용한다.

### 7.4 단계 10 Low Resolution / Upsampling 사용자 검증 가이드

F1 `Low Resolution / Upsampling`은 **구름만** 몇 픽셀에서 레이마칭할지와 그 결과를 화면
해상도로 복원하는 방법을 따로 비교한다. 건물, 지면, Scene Depth와 UI는 항상 원래 창
해상도로 그린다. 각 행은 왼쪽에서 오른쪽으로 계산량 또는 품질 여유가 커지는 순서다.
자동 성능 승인 전 시작 Resolution은 `Full`이다. 2026-08-19 사용자 정지 화면 검증에서
`50% Axis + Nearest`를 잠정 최종 후보로 선택했다.

중요한 예외가 하나 있다. `Full`에서는 저해상도 표본과 화면 픽셀이 1:1이므로 Filter를
`Nearest`, `Bilinear`, Joint로 바꿔도 셰이더가 의도적으로 동일한 1탭 경로를 사용한다.
따라서 **Filter와 임계값은 50%를 선택한 뒤 비교**해야 한다.

#### 7.4.1 Resolution 버튼 — 비싼 구름 레이의 개수

| 버튼 | 1920×1080 구름 타깃 | Full 대비 레이 수 | 이 버튼으로 확인하는 것 | 정상 결과 | 실패 징후 |
|---|---:|---:|---|---|---|
| `50% Axis` | 960×540 | 25% | 최대 픽셀 절감 후보 | Raymarch 시간이 가장 많이 감소 | 2×2 셀, 얇은 구름 소실, 이동 시 떨림 가능 |
| `Full` | 1920×1080 | 100% | 단계 9 Balanced 화질 기준 | 이전 단계와 같은 색·두께·폐색 | 전체 밝기나 구름 위치가 달라지면 split pipeline 오류 |

초기 비교에 있던 67%와 75%는 2:1 정수 확대가 아니어서 texel 영향 범위가 출력 픽셀 사이에서
주기적으로 달라졌고, 사용자가 50%보다 격자감이 크다고 판정했다. 비용도 50%보다 높아 활성
F1 후보에서 제외했다. enum 값과 크기 수학은 schema 32와 실패 이력 재현을 위해 보존한다.

해상도를 낮췄는데 건물이나 UI까지 흐려지면 실패다. 이 기능은 구름 중간 버퍼만 줄여야 한다.
창 크기가 1920×1080이 아니어도 축 비율은 같고 실제 타깃 크기는 현재 창에서 다시 계산된다.

#### 7.4.2 Filter 버튼 — 빈 픽셀을 복원하는 방법

| 버튼 | Full 픽셀당 읽기 | 상대 비용 | 화면에서 기대하는 변화 | 탈락 기준 |
|---|---:|---:|---|---|
| `Nearest` | 1탭 | 최저 | 가장 가까운 저해상도 결과를 그대로 확대한다. 블록 실패를 찾는 기준이다. | 셀 경계, 계단, 이동 시 딱딱한 팝이 보임 |
| `Bilinear` | 4탭 | 낮음 | 네 이웃을 단순 혼합해 Nearest의 블록을 부드럽게 한다. | 건물 위 구름색, 수평선 halo, 앞뒤 구름 혼합이 보임 |
| `Depth/Cloud/T Joint 4` | 4탭 | 중간 | 네 이웃 중 Scene/Cloud/T가 비슷한 표본만 섞어 경계를 지킨다. | 건물 누출, 얇은 구름 소실, 구멍 또는 파편화가 남음 |

같은 해상도에서 `Nearest → Bilinear → Joint 4` 순서로 누른다. 사용자 정지 화면 검증에서 세
필터의 차이가 크지 않아 가장 싼 Nearest를 잠정 최종 후보로 정했다. Joint 9는 Joint 4 대비
가시적 개선이 없어 활성 F1 후보에서 제외했으며 enum과 셰이더 경로만 호환용으로 보존한다.

#### 7.4.3 Scene Depth 버튼 — 건물과 하늘 경계

버튼은 `0.125% → 0.25% → 0.5% → 1%`이며 탭 수가 같아 계산량도 같다. 현재 UI에는
`0.125000%`처럼 소수 여섯 자리로 보일 수 있다. 기본값은 `0.25%`다.

이 값은 Full 픽셀의 Scene Depth와 저해상도 표본이 기록한 Scene Depth가 얼마나 달라도 같은
표본으로 인정할지 정한다. 왼쪽의 작은 값은 엄격하고, 오른쪽의 큰 값은 관대하다.

| 관찰한 문제 | 누를 방향 | 이유 |
|---|---|---|
| 건물 위로 구름색이 번지거나 수평선 양쪽이 섞임 | 왼쪽, 더 작은 값 | 다른 깊이 후보를 더 강하게 거부 |
| 건물 윤곽에 투명 구멍, 검은 틈, 이동 시 경계 팝 | 오른쪽, 더 큰 값 | 같은 면의 이웃까지 거부하는 현상을 완화 |

`F5` 건물 실루엣과 `F6` 수평선에서 판정한다. 값이 작을수록 무조건 좋은 것이 아니다.

#### 7.4.4 Cloud Depth 버튼 — 앞뒤 구름 덩어리 분리

버튼은 `0.5% → 1% → 2% → 4%`, 기본값은 `1%`이며 계산량은 같다. 이 값은 가장 가까운
가이드 표본과 후보 표본의 대표 구름 깊이가 얼마나 달라도 섞을지 정한다.

| 관찰한 문제 | 누를 방향 | 이유 |
|---|---|---|
| 앞 구름과 뒤 구름이 한 덩어리처럼 번짐, 외곽이 뭉개짐 | 왼쪽, 더 작은 값 | 다른 깊이의 구름을 분리 |
| 얇은 구름이 끊김, 내부가 조각남, 작은 구멍이 생김 | 오른쪽, 더 큰 값 | 같은 덩어리의 깊이 변화 허용 |

`F6`의 겹친 구름과 `F8` 상단 실루엣에서 비교한다. 대표 깊이가 없는 맑은 하늘에서는 이
버튼보다 Transmittance와 fallback의 영향이 더 클 수 있다.

#### 7.4.5 Transmittance Sigma 버튼 — 불투명도 경계

버튼은 `0.05 → 0.10 → 0.15 → 0.20`, 기본값은 `0.10`이며 계산량은 같다. Transmittance `T`는
`1`이면 투명한 하늘, `0`에 가까우면 짙은 구름이다. Sigma가 작으면 서로 다른 투명도를 강하게
분리하고, 크면 더 부드럽게 섞는다.

| 관찰한 문제 | 누를 방향 | 이유 |
|---|---|---|
| 얇은 구름 주변 halo, 밝은 외곽이 씻겨 나감 | 왼쪽, 더 작은 값 | 맑은 하늘과 구름의 혼합을 줄임 |
| 점 잡음, 작은 구멍, 이동 시 반짝임 | 오른쪽, 더 큰 값 | 비슷한 투명도 이웃을 더 많이 확보 |

`F6 Stratus`의 얇은 층과 `F6 Dense/Cumulus`의 작은 하늘 틈에서 판정한다.

#### 7.4.6 Minimum Weight 버튼 — 전 후보 거부 시 fallback

버튼은 `0.00001 → 0.0001 → 0.001`, 기본값은 `0.0001`이며 계산량은 같다. Joint 가중치의
합이 이 값보다 작으면 불확실한 혼합을 버린다. 건물 픽셀은 투명 구름으로 처리해 누출을 막고,
하늘 픽셀은 가장 가까운 유효 저해상도 표본을 사용한다.

| 관찰한 문제 | 누를 방향 | 이유 |
|---|---|---|
| 아주 약한 잘못된 후보가 섞여 건물/구름 경계가 번짐 | 오른쪽, 더 큰 값 | fallback을 더 일찍 사용 |
| Nearest 같은 블록, 경계 팝, 투명 구멍이 잦음 | 왼쪽, 더 작은 값 | 유효한 Joint 혼합을 너무 빨리 버리지 않음 |

이 버튼은 마지막에 조정한다. 먼저 어떤 가중치가 문제인지 세 Debug View로 찾고 해당 임계값을
조정해야 원인을 숨기지 않는다.

#### 7.4.7 Main View Debug 읽는 법

F1 `Noise / Weather / Shape / Sampling Debug View`의 `Main View Debug` 콤보에서 선택한다.
숫자 0~9 매핑은 바뀌지 않았다. 아래 세 Weight 출력은 **흰색일수록 후보가 잘 맞아 허용되고,
검정일수록 달라서 거부됨**을 뜻한다. 이름이 `Scene Rejection`이어도 흰색이 거부라는 뜻은 아니다.

`Scene Rejection`, `Cloud Depth Weight`, `Transmittance Weight`는 활성 UI의 Joint 4 판단을 보여 준다.
`Full`, `Nearest`, `Bilinear`에서는 Joint 계산을 하지 않으므로 이 세 화면이 흰색으로 나오는 것이
정상이다. 반드시 50%와 Joint 4를 선택한 뒤 읽는다.

| Debug View | 무엇을 표시하는가 | 정상 패턴 | 실패 패턴과 다음 조작 |
|---|---|---|---|
| `Low-resolution Grid` | 저해상도 texel 하나가 차지하는 영역 | 50%에서 균일한 2×2 대응 | 찌그러짐·단절·한 프레임 깨짐이면 UV/resize 오류. Composite에 격자가 남는 것과 구분 |
| `Scene Rejection` | Scene class/depth 허용도 | 건물/하늘 내부는 대체로 밝고 실루엣·수평선 경계는 어두운 띠 | 경계까지 흰색이며 누출되면 Scene 값을 왼쪽, 넓은 영역이 검고 구멍 나면 오른쪽 |
| `Cloud Depth Weight` | 후보의 대표 구름 깊이 유사도 | 같은 구름 내부는 밝고 앞뒤 겹침·윤곽은 어두움 | 전부 검고 조각나면 Cloud 값을 오른쪽, 모두 희고 깊이가 번지면 왼쪽 |
| `Transmittance Weight` | 후보의 투명도 유사도 | 같은 농도 내부는 밝고 얇은 외곽·하늘 틈은 어두움 | 전부 검고 반짝이면 T Sigma를 오른쪽, 모두 희고 halo가 생기면 왼쪽 |

디버그 색 자체는 최종 화질이 아니다. 원인을 찾은 뒤 반드시 `Composite`로 돌아와 실제 번짐,
소실과 팝이 해결됐는지 다시 확인한다.

#### 7.4.8 권장 사용자 검증 순서

1. 가능하면 Release, 1920×1080, VSync Off로 실행한다. 시간을 정지하고 F5 또는 F6처럼 같은
   카메라를 유지한다.
2. `Full + Composite`에서 Dense/Stratus/Cumulus 기준 모습을 확인한다. Full에서는 Filter 변경을
   비교하지 않는다.
3. `50%`에서 `Nearest → Bilinear → Joint 4`를 비교하고 F5 건물, F6 수평선, F7 내부,
   F8 상공을 확인한다. 차이가 없으면 Nearest를 유지한다.
4. 50%에서 해상도 자체의 손실·이동 떨림이 보이면 Resolution을 Full로 올린다. Nearest에서만
   경계 문제가 보이면 Bilinear, Joint 4 순으로 Filter만 오른쪽으로 이동한다.
5. Joint의 특정 문제가 있을 때만 Scene Depth, Cloud Depth, T Sigma를 한 행씩 조정한다.
   한 번에 두 행을 바꾸지 않는다. 마지막에 Minimum Weight를 확인한다.
6. WASD로 천천히 움직인다. 단계 10은 이전 프레임을 사용하지 않으므로 잔상은 없어야 한다.
   Full보다 두드러진 공간 떨림이나 경계 팝은 해당 후보의 실패다.
7. 오버레이에서 `Cloud Raymarch`, `Upsample/Composite`, `GPU Cloud Total`을 기록한다.
   `CPU Frame`과 FPS는 VSync·OS 대기의 영향을 받아 알고리즘 판정값으로 쓰지 않는다.

`Cloud Raymarch`는 해상도를 낮추면 감소해야 한다. `Upsample/Composite`는 Full 화면에서 항상
실행되는 복원 비용이며 대체로 Nearest가 가장 싸고 Joint 4가 가장 비싸다. 둘의 합인
`GPU Cloud Total`이 Full보다 낮아야 실제 최적화 이득이다. 정식 성능 판정은 120프레임 워밍업과
600개 timestamp의 p95로 별도 진행한다.

사용자 결과는 다음 형식으로 남긴다.

| 카메라/외형 | Resolution | Filter | 보인 문제 | 조정한 임계값 | GPU Cloud Total | 판정 |
|---|---|---|---|---|---:|---|
| 예: F6/Cumulus | 50% | Nearest | 차이 없음 | 기본값 | 측정값 | 통과/실패 |

가장 싼 50%부터 무조건 채택하는 것이 목표가 아니다. **Full과 큰 차이가 없고 이동 중에도 안정적인
후보 중 GPU Cloud Total이 가장 낮은 조합**을 사용자 승인 후보로 보고한다.

## 8. Pipeline Compare

F1 `Open World Render Pipeline Compare`의 1~5는 Procedural/Uniform → Texture3D →
Periodic Weather → Physical Shape → Full Open World를 누적한다. 버튼으로 Compare에 들어가면
main pass의 샘플 time만 0으로 고정하므로 바람 때문에 위치가 바뀌지 않는다. 일반 Animation
상태, 카메라, 도메인, 지면과 건물은 바뀌지 않는다. 외형 preset이나 slider를 조절해 Compare를
벗어나면 정상 time으로 즉시 복귀한다. 5는 항상 Dense Mixed와 현재 13-5 sampling/lighting을
복원한다.

## 9. 사용자 최종 검증

- [ ] F5: 건물이 구름을 정확히 가리고 양옆 하늘이 하나의 거대한 공백이 아니다.
- [ ] F6 Dense: 좌·중·우에 구름이 있고 푸른 틈이 약 20~40%다.
- [ ] Dense/Cumulus: 밝은 가장자리와 어두운 내부가 함께 있다. 전체가 흰 수증기면 실패다.
- [ ] Stratus: 낮고 넓으며 상단 변화가 완만하다.
- [ ] Cumulus: Stratus보다 높은 돔과 중심 질량이 뚜렷하다.
- [ ] 전환 중 카메라와 Weather 공간 위치가 움직이지 않는다.
- [ ] F7/F8: 내부와 상공에서 NaN, 검정 frame, 평평한 천장이 없다.
- [ ] Custom: 수정→저장→다른 preset→복원→재실행 후 Custom 복원이 모두 된다.
- [ ] Compare 1~5에서 건물/지면/카메라는 고정되고 구름 하위 계산만 누적 변경된다.
- [ ] 제거된 F2 타입 버튼, 문자 키와 F9~F12는 동작하지 않는다.
- [ ] F1 Optimization의 Balanced가 가장 왼쪽 Master이고 Empty Search가 `2×/Off`만 제공한다.
- [ ] Fine Reference와 Straight 320 Fine은 오프라인 기준임을 이해하고 일반 플레이 후보로 사용하지 않는다.
- [ ] F1 Resolution이 `50%→Full`, Filter가 `Nearest→Bilinear→Joint4` 비용순이다.
- [ ] Full에서 단계 9와 같은 화면이며, Filter를 바꿔도 Full 1:1 경로는 변하지 않는다.
- [ ] 50%에서 건물 위 번짐·얇은 구름 소실·2×2 셀·이동 중 떨림을 카메라/외형별로 기록한다.
- [ ] Joint에서 Scene/Cloud/T Debug의 흰색은 허용, 검정은 거부이며 Nearest/Bilinear의 흰 화면은 정상이다.
- [ ] 같은 해상도에서 Nearest부터 올려 큰 차이가 없는 가장 왼쪽 필터를 찾고 GPU Cloud Total을 비교한다.

13-4E 렌더와 새 Dense Mixed F6 기준 13-5는 2026-08-17 승인됐다. 구형 shell은 제외했고 단계 9
Balanced는 Dense 버튼 복구와 Fast/4× 탈락 재검증 뒤 2026-08-19 최종 승인됐다.
