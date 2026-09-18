# Stage 15 — 태양 방향에 따른 공통 구름 명암 개선

## 상태와 범위

이 문서는 00~05 내부 명암·태양 차폐 개선 기록이다. 05 승인 완료. **06 림 개선 착수 이후 기록은 [별도 문서](stage15-cloud-rim-lighting.md)로 이전했다.** 06 진단 진행 중이며07은 미착수다.
각 단계는 사용자 화면 확인 후 다음 단계로 진행한다.

Urban에서 먼저 검증하고 Urban 조명의 Stratus/Cumulus/Mixed에도 공통 계산을 적용한다.
일반 High, PlanarLayer, Balanced512 규격과 큰 배치는 유지한다. 높이159/79 후보는 별도 비교 실행이며 미채택이다.

## 00 — 변경 전 기준을 남기는 이유

사용자가 태양 고도 18/45/70°에서 명암 변화가 약하다고 보고했다.
원인은 아직 확정하지 않았으므로 먼저 입력과 원본 화면을 고정한다.
기존 미커밋 소스와 첨부 원본을 보존하고, 기존 셰이더의 UI 없는 화면을
한 번의 baseline 촬영 세트로 남긴다. 01/02의 반복 촬영은 필수가 아니다.

### 보존 내용

- `captures/stage15-directional-lighting/00-baseline/originals`: 사용자 첨부 원본 4장.
- `source-state`, initial git diff/status/HEAD, SHA-256 manifest: 시작 소스 143개와 미커밋 변경.
- `clean`: 고정 조건의 PNG, 선형 HDR RGBA16F 원본, 설정·해시·재현 비교 결과.
- captures는 로컬 전용이다. 공식 문서가 이미지의 Git 백업을 의미하지 않는다.

### 검증 조건

1920×1080 OFF Release, F5/60° FOV, time=71s, wind=0, exposure=0EV, WB=6500K.
Urban 원래 외형 및 Urban 조명+세 Type, 각 여섯 태양 조건을 사용한다.
방위 -108.5°의 고도 18/45/70°와 수평 시선 기준 순광/측광/역광(고도 18°)이다.
반복 확인은 새 PNG를 남기지 않고 저장된 원본과 메모리에서 비교한다.
재현 오차 기준은 정규화 HDR MAE≤1e-5/max≤1e-3, LDR RGB MAE≤1/255/max≤2/255이다.
파일 누락·finite 실패·D3D 오류는 별도 실패이며, 캡처 시간은 GPU 성능 지표가 아니다.

### 결과

- 시작 소스와 원본 4장 보존 완료. 기존 파일 덮어쓰기 없음.
- OFF Debug/Release 빌드 성공. 두 구성의 HighCloudSmoke·FormationSmoke·RendererStateSmoke 통과.
- 24개 PNG/HDR/설정 보존, HDR finite 및 같은 실행의 반복 검사 24/24 통과.
- 별도 OFF Release 실행의 원본 재현 24/24 통과. 정규화 HDR 최대오차 0.000745083,
  최대 case MAE 6.84465e-8, LDR 최대 1/255. 새로운 스크린샷은 저장하지 않았다.
- 시작 manifest와 비교 시 기존 파일 중 변경된 것은 CMakeLists/main/Renderer.cpp/Renderer.h뿐이다.
  모든 셰이더와 파라미터는 시작 상태와 SHA-256 일치. 기존 미커밋 변경은 보존했다.
- RTX 4080 SUPER / driver 32.0.15.9186, 단독 OFF Release 성능 12/12 통과.
  case별 Cloud p95 최대 8.132ms, Frame p95 최대 8.658ms. 시작 기준 실패 case 없음.
  첫 측정과 빌드의 일부 시간이 겹쳐 최종 기준은 빌드 종료 후 단독 실행으로 다시 측정했다.
- 기존 clean 폴더 재촬영 거부 확인. 최초 PNG 인코더 실패는 파일 저장 전 발생했으며
  빈 시도 폴더만 보존했다. BGRA 채널 순서로 WIC 인코딩 후 정상 저장했다.
- 재현 검사에서는 최초 촬영의 추가 반복 프레임까지 실행하도록 수정했다.
  처음 발견한 CPU windDirection 2 ULP 차이는 프레임마다 정규화되는 상태의 검사 시점 차이였다.
  같은 프레임 수에서 설정 binary도 일치한다. 렌더 수식은 변경하지 않았다.
- 사용자 00 승인: 2026-09-14. 비교 기준으로 승인했으며 명암 개선의 최종 승인은 아니다.

## 01 — 변경 전 판단

기존 조명 분리 출력이 F4에서 빠져 있고 숫자 9는 구간 중점만 검사한다.
기존 출력 노출과 불투명도 가중 태양 T(`sum(Tview*(1-Tstep)*Tsun)/sum(Tview*(1-Tstep))`)
진단을 추가한다. 간접광/phase 중립 실행 및 실제 GPU 버퍼의 방향·cache basis 검사를
로그로 남겨 원인을 분리한다. 기본 밀도·형상·조명·ABI는 바꾸지 않고 스크린샷을 저장하지 않는다.
예상 원인은 간접광의 대비 완화 또는 보이는 구름의 광학 두께 부족이며 아직 미확정이다.

## 01 — 실제 변경과 확인된 결과

- F4에 기존 Direct/Sky/Ground/Multiple/Silver 및 두 가시성 출력, 중점 Sun T/Phase를 노출했다.
- ID 60 Visible Sun T를 추가했다. 기존 24/숫자 9는 유지하고 중점 한 표본임을 표시한다.
- UI는 누적 성분의 `L/(1+L)`와 Tone 표시, Silver의 Direct 포함 관계, 빈 기여 0의 의미를 설명한다.
- 일반 Composite의 밀도·조명 기본값/High 상수/CB ABI/schema는 유지한다.
- 테스트에서 정상 Render 이후 물리 GPU 입력을 고정하고 간접광 Off/phase 중립/둘 모두를
  분리했다. 밀도·View T·원본 Sun T 불변, 중립 성분 0, albedo 독립, 빈 구름 0을 검사했다.

| Urban 고도 | 화면 불투명도 가중 Sun T | Direct 대비 | Direct+Sky+Ground+Multiple 대비 |
|---:|---:|---:|---:|
| 18° | 0.893175 | 0.503755 | 0.492176 |
| 45° | 0.930321 | 0.392715 | 0.391001 |
| 70° | 0.940951 | 0.391236 | 0.391716 |

대비는 불투명도>0.1 픽셀에서 근사 선형 휘도의 `(p90-p10)/(p90+p10)`다.
Tone/대기 배경을 포함한 Composite의 대비나 국소 골의 대비를 직접 측정한 값은 아니다.
18°에서 간접광은 이 mask의 총 구름 휘도 약 7.0%, 45° 약 10.2%, 70° 약 12.1%다.
간접광 제거로 대비가 크게 회복되는 결과는 없었다. 현재 모델의 **보이는 Urban 구름에서
태양 차폐가 약하므로 02의 광학 두께/밀도 분포 조사를 우선할 근거**를 얻었다.
이것만으로 cache 근사 정확도나 물리 밀도를 확정하지 않는다.

| Urban 조명 Type | Sun T 18° | 45° | 70° |
|---|---:|---:|---:|
| Stratus | 0.779586 | 0.870311 | 0.894681 |
| Cumulus | 0.750527 | 0.850497 | 0.877667 |
| Mixed | 0.493561 | 0.677491 | 0.736658 |

Mixed의 차폐는 이미 강하다. 다음 단계에서 동일 강화값을 무조건 채택하지 않고
계획의 0/0.35/0.70 후보 중 가장 약한 만족값과 타입별 하향 후보를 확인한다.

### 검증과 기각한 검사 방식

- OFF Debug/Release 빌드 성공. Debug DirectionalLightingDiagnostics·NoiseLabSmoke 2/2,
  OFF Release 전체 CTest 39/39 통과. 새 GPU 진단은 12개 case×4개 variant 통과.
- Light/Shadow/Atmosphere GPU 방향과 cache basis의 최대 오차 2.08617e-7≤1e-5.
  Shadow CS와 Cloud PS의 b8 identity 일치. 고도 변경이 실제 GPU까지 전달됨을 확인했다.
- Debug HDR finite, D3D11 error/corruption 및 리소스 hazard 검사 통과.
- 승인 Composite 원본 24/24 tolerance 통과. 최대 정규화 HDR 오차 0.000231213,
  최대 case MAE 4.12289e-8, LDR 최대 1/255. 원본 파일 SHA-256 유지, 새 이미지 저장 0.
- OFF Release 직렬 성능 12/12 통과. case p95 최대 Cloud 8.395ms, Frame 9.014ms.
  이전 baseline의 8.132/8.658ms와 측정 변동이 있으므로 성능 개선으로 해석하지 않는다.
- 처음에는 조명 비교마다 전체 프레임을 다시 생성했다. View T/밀도는 같지만 raw Sun T의
  정규화 HDR 최대차 0.00141632가 00의 재현 임계값 0.001을 넘었다.
  테스트 목적이 조명 독립성인데 매번 새 shadow 결과까지 비교하는 방식을 기각했다.
  정상 프레임의 물리 입력을 고정한 Cloud/Tone 검사로 분리했고 임계값은 완화하지 않았다.
  전체 Render 재현은 별도 baseline 회귀로 검사한다. raw T의 미세한 실행 간 변동 원인은
  이 단계에서 특정 GPU 연산이나 드라이버 문제로 확정하지 않는다.
- probe 경로의 shader 디렉터리 끝 separator 때문에 첫 파일 조회가 실패했다.
  `shaders/../tests/...` 정규화 경로로 수정했다. 렌더 알고리즘 수정은 없다.

실행 명령과 상세 체크리스트는 CONTRIBUTING/튜닝 가이드 및 로컬 학습 문서에 기록했다.
수치 로그는 `captures/stage15-directional-lighting/01-diagnostics/`에 보관한다.
**01 사용자 승인 전이며 02는 시작하지 않았다.**

## 이후 승인 단계의 고정 계약

| 단계 | 비교할 항목 | 채택 조건 |
|---|---|---|
| 01 | F4 Direct/Sky/Ground/Multiple/Silver/가시성, opacity 가중 태양 T; 숫자 9는 중점 진단 유지 | CPU/GPU 태양·cache 전달과 간접광/phase 분리로 원인 확인 |
| 02 | `lerp(q,smoothstep(0,0.4,q),s)`, s=0/0.35/0.70; View Detail 후와 Base 그림자에 공통 적용 | 0 보존·단조·Base 상한·empty skip 안전, 화면을 만족하는 최소 강도 |
| 03 | 필요할 때 Base octave 2·3 진폭 1.25/1.5배, 가중치 정규화 | 큰 연결을 보존; 충분하면 변경 없이 기록 |
| 04 | Near `s*(1-smoothstep(4000,8000,d))`, s=0/0.5/1 | Detail 조회 생략 조건과 Near/Far 연속성·성능을 만족하는 최고 강도 |
| 05 | shadowExponent 1/1.2배, fill·다중 산란 1/0.75/0.5배, phase +0/+0.1/+0.2, 외곽 optical-depth 1/1.25배 | 한 항목씩 비교; 검은 내부·넓은 과노출·전체 윤곽 발광 기각 |
| 06 | Concept·Type·Custom, 세 Concept×F5~F8, 전체 OFF Release CTest/성능 | 사용자 최종 승인 후 같은 baseline 조건의 최종 촬영 |

Phase 상한 2.5는 실제 clamp가 확인될 때만 4/8 후보를 비교한다.
02·04 추가 강도는 처음 0이며 승인된 후보만 기본값으로 반영한다.
F1 Density shaping은 Formation/Type/Custom 소유(b7 offset 40, 크기 48 유지),
F3 Near detail shadows는 그림자/Concept 소유(b8 offset 152, 크기 160 유지)로 계획한다.
그 단계에서 CPU/HLSL·sanitize·reflection·문서를 함께 변경하고 Custom schema 3
(구 schema 1/2는 shaping=0), snapshot schema 41로 올린다. 00의 schema는 그대로다.
실패 후보는 다음 단계에 누적하지 않으며 01/02의 촬영을 의무화하지 않는다.
# 2026-09-14: 01 승인 / 02 구현 시작

사용자가 높은 Sun T, 약한 Sky/Ground, phase=0 시 Silver 소멸과 Sun T 유지,
중점/화면 기여 진단의 차이 및 세 Type의 실루엣 일치를 확인하고 02 진행을 승인했다.
02는 기존 배치와 Detail 침식 이후 밀도에 공통 곡선을 적용한다.
`F(q,s)=lerp(q,smoothstep(0,0.4,q),s)`, 후보 0/0.35/0.70, 승인 전 기본값 0.
View 최종 밀도와 Base 그림자에 같은 함수, 빈 영역/단조성/상한/강도0 복원을 검증한다.
Weather·profile·extinction·High 상수는 고정하며 03 이후 변경은 포함하지 않는다.
02 전후 필수 촬영 없음. 사용자 화면 판정은 자동 검증과 별도로 받는다.

## 02 자동 검증 완료 — 사용자 화면 판정 대기

- Debug/Release 빌드 통과. Release 전체 CTest 40/40, Debug 관련 4/4 통과.
- 활성 HLSL과 b7 reflection(48B, densityShaping offset40/4B), CPU/GPU 곡선 오차≤1e-5,
  빈 영역·단조성·Base/Detail 상한·강도0 복원, HDR finite 및 Debug D3D11 오류/리소스 충돌 검사를 통과했다.
- Custom schema3의 .70 round-trip, schema1/2→0 이관, 누락/범위 밖 새 필드 거부와 snapshot41을 확인했다.
- 00 기준 24화면을 강도0으로 메모리 비교해 24/24 통과했다. HDR 정규화 최대오차 .00030996,
  최대 MAE 4.956e-8, LDR 최대차 1/255. 원본/clean 파일을 덮어쓰거나 새 비교 스크린샷을 만들지 않았다.
- Urban/Stratus/Cumulus/Mixed × 고도18/45/70 × 강도0/.35/.70 = 36후보 조건을 검사했다.
  세 Type 모두 기본 강도0이며 승인 전 내장값은 변경하지 않았다.

OFF Release, 1920×1080, VSync/UI/캡처 Off, 기존 12 case(세 Concept×F5~F8),
60frame 예열/120표본을 직렬 측정한 **case별 p95 중 최댓값**:

| 강도 | Cloud p95 최대 | Frame p95 최대 | 통과 |
|---|---:|---:|---:|
| 0 | 9.453 ms | 10.485 ms | 12/12 |
| .35 | 7.983 ms | 8.953 ms | 12/12 |
| .70 | 6.542 ms | 7.530 ms | 12/12 |

기준 00의 최대 Cloud 8.132/Frame 8.658ms와 이번 강도0 수치는 별도로 보존한다.
모든 case가 목표 Cloud≤10/Frame≤16.67ms 안이지만 측정 편차를 제거한 속도 향상 주장으로 사용하지 않는다.
밀도 변화에 따라 적분 길이/early exit도 달라질 수 있으며 상수는 변경하지 않았다.

근거는 로컬 `captures/stage15-directional-lighting/02-density/`의 release-all-tests.log,
debug-tests.log, baseline-verify.log, performance-035.log, performance-070.log에 있다.
초기 변경 이유와 실제 값은 학습 문서 9절 및 위 기록에 보존했다.
기존 미커밋 작업은 유지하며 02 수정 파일은 source-state/와 source-manifest.json에 별도 보존한다.
**사용자 판정: 아직 미승인.** F1 Density shaping으로 후보를 비교한 후 가장 약한 만족 강도를 결정한다.
03/04/05/06은 진행하지 않았다. 근거리 Detail 그림자는 여전히 후속 04의 작업이다.

## 02 사용자 승인 / 03 착수 — 2026-09-14

사용자 요청으로 02 체크리스트 전 항목을 체크했다. 체크는 사용자 확인/진행 승인이며
이전 체크리스트의 예상 결과 모두가 달성됐다는 뜻은 아니다.
실제 피드백: Urban/세 Type 모두 Density shaping 증가 시 흰 부분이 점차 뭉쳐 보임.
그러나 내부 명암과 실버 링은 여전히 비슷함. 사용자가 다음 단계 진행을 요청했다.
후속 답변으로 **Urban과 세 Type의 강도 0.70**을 선택했다. 이 값을 다음 기준으로 채택한다.
Meadow/Snow는 기존 강도0 유지. 02 기능은 승인됐지만 전체 명암 목표는 미달성이다.

### 03 변경 전 관찰 → 가설 → 수정 범위

밀도만으로 흰 덩어리가 뭉치므로 기존 Base 위의 돌출부/골을 보완할 필요가 있다.
옥타브는 서로 다른 크기의 굴곡을 겹치는 층이다. 가장 큰 첫 층은 유지하고
중간 크기인 2/3번째 층만 강조하면 햇빛을 가리는 작은 언덕과 골이 늘 것으로 예상한다.
원래 진폭 [.5,.25,.125,.0625]에서 [.5,.25*k,.125*k,.0625], k=1/1.25/1.5를 비교하고 합으로 나눈다.
마지막 층의 진폭까지 연쇄 증가시키지 않는다. seed·주파수·타일·Worley·Weather·profile·High 상수 고정.
F2의 임시 03 비교 선택으로 k를 바꾸고 Base/Detail 재생성 통로를 사용한다.
Detail은 재생성해도 내용/해시가 같아야 한다. 초기 k=1이며 후보는 사용자 승인 전 채택하지 않는다.
비교값은 b6 기존 예약 offset28에 extra=k-1(0/.25/.5)을 넣어 96B를 유지한다.
세션 실험값이라 Custom에는 저장하지 않고 진단 snapshot41에만 기록한다. 최종 정리 시 임시 UI를 정리한다.
CPU/HLSL 양쪽 생성식과 offset·sanitize·문서를 함께 갱신한다.
큰 연결이 깨지거나 잔무늬가 전체를 덮으면 기각한다. 실버 링은 05에서 별도로 조정한다.

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
Type/Concept 선택은 세션 후보를 유지한다. Custom에는 저장하지 않고 snapshot41의 noiseVolumes에
baseMidOctaveExtra를 기록한다. 초기 1.00배이며 아직 후보 채택은 하지 않았다. 06에서 임시 실험 UI를 정리한다.

`--base-octave-test`/CTest BaseOctaves는 72조건(세 후보×Urban/세 Type×세 고도×Detail Off/On)의
HDR finite/진단 범위를 검사한다. 생성 RGBA8 R은 CPU 기준과 1/255+1e-5 이내 비교한다.
GBA와 Detail의 비트 동일성, Weather 해시 유지, 1.00배로 복원 시 원래 Base 해시를 검사한다.
`--high-performance-test --base-octaves-125` 또는 `--base-octaves-150`은 기존 12 case를 직렬 측정한다.
밀도 인자가 없으면 승인된 Concept 기본값(Urban .70, Meadow/Snow 0)을 사용한다.
00 baseline 검증은 실행기에서 강도0을 명시적으로 복원하며 원본을 다시 촬영하지 않는다.

### 03 초기 관찰과 회귀 검사 보완

03 72조건 GPU 검사와 CPU 생성 검증은 통과했다. RGBA8 R의 CPU/GPU 최대차 .00219949로
한 UNORM 단계(1/255=.00392157) 이내다. Base GBA/Detail 비트 보존, Weather 보존 및 Base 복원 통과.
Urban Detail On, 18°: k=1/1.25/1.5의 Sun T=.79660/.80026/.80293,
Direct 대비=.57296/.56868/.56806. 수치상 평균 차폐/대비가 강화되지 않았다.
국소 굴곡 개선 여부는 별도 화면 판정 사항이며 후보를 자동 채택하지 않는다.

전체 Release 첫 실행은 40/41. 기존 01 테스트가 밀도 .70 Stratus에서
정규화 HDR 밀도 최대차 .00109141로 기존 상한 .001을 넘었다(MAE 5.931e-8).
01 실행기의 각 진단 호출이 물리 CB를 다시 sanitize/파생하던 부분을 제거하고,
정상 Render가 만든 물리 CB/texture를 고정한 채 b1 debugMode와 b3/b4 조명만 직접 갱신하도록 보완했다.
허용 오차는 완화하지 않는다. 이 변경으로 통과하는지는 재검증 결과에 기록한다.

### 01 대조 검사 재검증 결과

입력 CB 고정/진단 예열만으로 최초 기준과의 미세 차이는 사라지지 않았다.
조명을 그대로 둔 대조 실행에서도 동일한 Stratus 밀도 차이(.00109141)가 재현되므로,
이를 조명 변경의 인과 효과로 분류한 기존 판정은 적절하지 않았다. 근본 수치 원인은 아직 확정하지 않았다.
각 variant 직전에 기본 조명으로 Density/View T/Sun T를 읽고 이어서 같은 순서로 조명 변경 결과를 읽어
쌍으로 비교하도록 수정했다. albedo도 변경 직전 대조를 사용한다. 기준과 대조의 차이는 로그에 남긴다.
기존 허용 오차를 바꾸지 않고 12조건×4variant의 쌍 비교는 통과했다.
이 결과는 조명 변경에 대한 불변식 검증이며 모든 렌더 호출의 bit-exact 반복성을 보장한다는 뜻은 아니다.
일반 렌더 계산을 이 문제 때문에 수정하지 않았다. 관련 실패/통과 로그를 03-base에 보존한다.

### 03 Release 검증 및 성능 결과

Release 전체 CTest 41/41 통과, 00 원본의 강도0/옥타브1 조건 24/24 회귀 통과.
01 테스트는 조명 변경 직전 대조와의 쌍 비교를 사용한다. 최초 프레임과 대조 간 미세 차이는
앞선 제한 사항대로 남기며 bit-exact 통과로 표현하지 않는다.

OFF Release, 1920×1080, UI/VSync/캡처 Off, 기존 12 case, 60프레임 예열/120표본:

| 중간 옥타브 배율 | 최대 case Cloud p95 | 최대 case Frame p95 | 판정 |
|---|---:|---:|---|
| 1.00 | 8.977 ms | 9.944 ms | 12/12 통과 |
| 1.25 | 7.807 ms | 8.672 ms | 12/12 통과 |
| 1.50 | 8.938 ms | 9.763 ms | 12/12 통과 |

밀도는 Urban .70, Meadow/Snow 기존0이다. 후보 성능 차이를 반복 측정된 속도 개선으로 해석하지 않는다.
시작 1.00은 승인된 02 상태다. 03 후보는 아직 채택하지 않았다.
새 비교 화면 파일은 저장하지 않았고, 원본 captures/00-baseline은 유지했다.
02 체크 완료·사용자 피드백·.70 승인 patch는 02-density/approval-2026-09-14.md 및 approved-defaults.patch에 있다.
03-base/first-release-tests.log와 frozen-input-invariance-failure.log는 실패 이력,
paired-invariance.log와 release-all-tests.log는 보완 후 결과다. 성능/원본 회귀 로그도 같은 폴더에 보존했다.

## 03 검증 마무리 — 사용자 화면 판정 대기

Debug/Release 빌드 통과. Release 전체 41/41, Debug 관련 6/6 통과.
Debug는 NoiseLab UI, 승인 기본값/저장/CPU noise, 조명 쌍 비교와 BaseOctaves를 포함한다.
D3D11 오류·리소스 충돌이 보고되지 않았다. Release 전체 실행 후 비교 Combo 폭만 180 UI 단위로 줄여
이름이 잘리지 않게 했으며 두 빌드를 갱신하고 Debug UI 검사를 통과했다.

**채택 상태:** 02 밀도 강화 .70은 Urban/세 Type에 반영 완료. 03 배율은 1.00을 유지하고
1.25/1.50은 비교용으로만 제공한다. 아직 명암 개선을 달성했다고 판정하지 않는다.
03 체크리스트는 미체크로 남겨 사용자 화면 피드백을 기다린다. 04/05/06은 시작하지 않았다.
추가 비교 스크린샷 0장. 00 원본 및 이전 단계 source-state는 유지했다.
현재 소스/해시는 03-base/source-state 및 source-manifest.json, 실행 파일 해시는 validation-summary.json에 보존했다.

## 03 사용자 승인 / 04 착수 — 2026-09-14

사용자가 03 비교 순서를 모두 승인했다. 1.50배에서 덩어리의 들어감/나옴이 더 분명해진다고 확인했으며,
Density shaping .70 유지도 확인했다. 다음 기준은 Base 1.50배(중간 octave extra=.5), 밀도 .70이다.
체크 완료는 사용자 확인과 다음 단계 진행 승인이다. 전체 명암·림 목표의 최종 달성을 뜻하지 않는다.

**미해결: 세로 줄/띠.** 사용자는 1.00/1.50배 모두에서 관찰했다고 보고했다.
첨부 원본을 `03-base/user-reported-vertical-bands.png`로 보존했다. 새 촬영이 아니다.
화면에는 F5, Time248.80s, Custom Sun/Phase/Environment가 보이지만 정확한 태양 각도와 이미지의
옥타브 설정은 확인되지 않았다. 앞/뒤 밀도 중첩은 사용자 가설이며 원인으로 확정하지 않는다.
사용자 요청에 따라 세로 줄 원인 분석은 뒤로 미룬다. 04에서도 별도 미해결 항목으로 유지한다.

### 04 변경 전 관찰 → 가설 → 고정 조건

현재 View는 Detail로 표면을 깎지만 Near/Far 그림자는 Base만 적분한다.
가까운 구름의 파임과 빛의 통과 경로를 맞추기 위해 Near에서만 Base/Detail 최종 밀도를 혼합한다.
`w=strength*(1-smoothstep(4000,8000,distance(camera,sample)))`, 후보 strength=0/.5/1.
거리 단위 m, w는 무차원이다. 4km 안은 선택 강도 전량, 4~8km 연속 전환, 이후0.
Near·Detail 활성·w>0일 때만 Detail 경로를 사용하고 raw Base가 비면 Detail 조회를 생략한다.
Detail 침식/밀도 shaping 순서는 View와 같다. 같은 Base를 두 번 조회하지 않는다.
Far Cache와 cache 불가 시 cone은 기존 Base 경로다. step/skip/early exit/cone 상수와 cache 크기 유지.
F3 Near detail shadows는 b8 offset152 float, 총160B 유지. Type 전환 시 유지,
Concept 선택은 장면 승인값(아직0)을 적용한다. Custom Formation 저장 대상이 아니며 snapshot41에 실제값 기록.
처음에는0으로 두고 자동 검증·사용자 비교 후 만족하는 가장 높은 강도를 채택한다.
Detail 그림자는 차폐를 줄여 파임으로 햇빛이 통하게 할 수 있다. 단순히 전체를 어둡게 하는 항이 아니다.

### 04 실제 변경 및 검증 방법 보정

`CloudDeepShadow.hlsl` Near 적분에 카메라-표본 거리 기반 Detail 혼합을 추가했다.
CPU/HLSL b8 offset152 float, sanitize, reflection, F3, Concept descriptor, snapshot41을 연결했다.
Type 전환은 Shadow를 바꾸지 않는다. 공통 noise 기본값은 03 승인에 따라1.50배로 갱신했으며
00 검증 실행기는 Base1.00/밀도0을 명시적으로 재적용한다. baseline 파일은 덮어쓰지 않는다.

초기 캐시 bit-exact 검사에서 Far tau 최대0.000275701, Near0.0000976548의 차이를 확인했다.
정상 Render 입력 CB를 고정하고 **강도를0으로 유지한 반복**에서도 발생했다(`noop-control.log`).
따라서 기능 변경에 의한 차이라고 단정하지 않는다. 수치 흔들림의 원인과 세로줄과의 관련성은 미확정이다.
이 실패를 보존하고 검사 계약을 OFF 영상 회귀 규약에 맞추었다: cache tau를 실제 투과율
`T=exp(-tau)`로 변환하여 MAE≤1e-5/max≤1e-3 검사. Near 침식의 차폐 증가도 T 기준max1e-3까지만 허용한다.
기존 HDR/PNG 회귀 허용오차는 변경하지 않았다. “Far/복원 불변”은 이 허용오차 내 결과이며 bit-exact 주장이 아니다.
`RenderDeepShadowCaches`의 입력 준비와 `DispatchDeepShadowCaches` 적분을 분리하여 테스트가
동일 CB·실제 CS를 재사용한다. 런타임은 기존처럼 매 프레임 입력 준비 후 동일 dispatch를 호출한다.
48회 후보/무변경 반복(4 Formation×2카메라×6강도 순서), 선택된3개slice readback을 검사한다.
전체 slice의 bit-exact 증명이나 거리 이동 시 화면 품질 판정은 아니다. 후자는 사용자 체크 항목이다.

### 04 자동 검증 결과 / 사용자 판정 대기 — 2026-09-14

- Debug·Release 빌드 성공(VCLOUD_STRICT_VALIDATION=OFF). 초기 SDK 조회 접근 오류는 동일 단독 빌드 재실행으로 해소했다.
- 최종 Release CTest 42/42 통과. Debug 관련 5/5 통과(새 GPU 검사, Shadow/Noise CPU 수학, NoiseLab, RendererState).
- 활성 HLSL·reflection·HDR finite·D3D11 error/resource hazard 검사 통과. 캐시 투과율 비교는 앞서 명시한 허용오차 기준이다.
- 변경 전 baseline VerifyOnly 24/24 통과. 기존 원본·HDR·PNG 덮어쓰기 없음, 새 비교 촬영 없음.
- 후보 성능: OFF Release, RTX4080 SUPER,1920×1080, UI/VSync/캡처 Off,60 warm/120 samples,3 Concept×F5~F8 직렬.

| Near 강도 | 통과 case | case별 Cloud p95 최댓값 | case별 Frame p95 최댓값 |
|---|---:|---:|---:|
| .50 | 12/12 | 9.104ms | 10.025ms |
| 1.00 | 12/12 | 8.141ms | 8.621ms |

강도0은 Release 전체 CTest HighPerformance에서 통과했다. .5보다1의 일부 시간이 작은 결과만으로
성능 개선이라고 해석하지 않는다(실행별 변동 포함). 두 후보 모두 목표 Cloud10ms/Frame16.67ms 이내다.
원시 성능 로그·실패 시도·최종 검사 로그·현재 소스/해시는 `captures/stage15-directional-lighting/04-near-detail/`에 보존한다.

**채택 상태:** 03 Base1.50 / 밀도.70 유지. 04 Near detail shadows는 아직0이며 사용자 선택 전 기본값을 올리지 않는다.
04 사용자 비교 체크리스트는 미체크로 유지한다. 세로줄은 별도 미해결로 남고, 05/06은 진행하지 않았다.

### 04 사용자 비교 결과 — 2026-09-15

**사용자 확인:** Near detail shadows 0.0과1.0에서 구름 내부 명암은 비슷하며 영향이 크지 않았다.
Density shaping을1.0으로 올렸을 때 구름 그림자가 더 많이 생성되는 것으로 관찰했다.
이 관찰은 사용자 확인 결과로 수용한다. 관찰한 카메라/거리/Type/태양 각도는 이번 보고에 명시되지 않았다.

**해석과 다음 판단:** Near Detail 혼합은 Base 차폐에서 Detail 침식 후 차폐로 접근하여 가까운
파임의 빛 통과를 맞추는 기능이다. 전체 차폐를 강화하는 기능이 아니므로 명암 개선의 주 수단으로
효과가 확인됐다고 기록하지 않는다. 이번 관찰은 밀도/광학 두께와 후속 직접광·간접광의 균형을
계속 확인할 근거다. 어느 항이 주원인인지는 아직 확정하지 않는다.

**설정/승인 범위:** 이번 보고는 0/1 명암 비교에 대한 사용자 결과다. 거리 전환·지면 그림자·세 Type 등
보고하지 않은 체크 항목까지 일괄 통과 처리하지 않는다. Density shaping1.0은 추가 관찰값으로 기록하며,
기본값 채택 지시로 간주하지 않는다. 승인된 Base1.50/밀도.70과 Near 기본0을 유지한다.
04 최종 강도 선택 및05 진행 지시는 아직 별도 확인되지 않았다. 코드 변경·추가 촬영·재빌드는 하지 않았다.

## 04 진행 승인 / 05 변경 전 기록 — 2026-09-15

사용자가 다음 과정 진행을 승인했다. 04는 명암 개선 효과가 작다는 사용자 판정을 보존하며 종료한다.
보고되지 않은 거리/타입 항목을 통과로 꾸미지 않는다. 다음 기준은 Near0, Base1.50, Urban·Type 밀도.70이다.
밀도1.0 관찰은 기본값으로 채택하지 않는다. 세로줄은 기존 미해결로 유지한다.

### 관찰 → 가설 → 변경 항목과 고정 조건

04의 Detail 차폐 보완만으로 내부 명암이 충분히 달라지지 않았다. 05에서는 직접광 차폐의 표현과
그 위를 채우는 간접광 에너지를 분리해서 비교한다. 비유하면 구름 내부의 햇빛 그림자 위에
보조등을 얼마나 켜는지 조절하는 것이다. 밀도·형상·노출·화이트밸런스·태양 각도는 후보 간 고정한다.
각 후보는 Urban 원래 조명에서 **한 항목만** 바꾼다. 후보를 누적하거나 자동으로 채택하지 않는다.

- shadowExponent 1.35/1.62: 직접광 T^exponent. 원시 태양T/밀도는 그대로, 차폐된 직접광은 감소 예상.
- Sky/Ground 각각 .85/.6375/.425: 실제 물리 LUT fill을 각각 조절. 미사용 legacy strength를 바꾸지 않는다.
- Multiple attenuation .15/.1125/.075: 반복 산란 에너지 계수. extinction factor나 Interior blend와 구분한다.
- Phase intensity .20/.30/.40: 시선-태양 각도별 산란 배율. 역광 방향에서 효과를 확인한다.
- Edge optical depth scale 2/2.5: 외곽 가중치 T^scale, 증가하면 얇고 빛이 잘 통하는 부분에 더 한정한다.
- Phase 상한2.5는 활성 구름 픽셀에서 포화가 측정될 때만 테스트 전용4/8 shader variant를 비교한다.
  상한 후보는 정식 UI/기본값으로 자동 채택하지 않으며 ABI 크기를 바꾸지 않는다.

F3 기존 b3/b4 필드를 노출하고 Urban의 위 여섯 항목만 복원하는 버튼을 제공한다.
CPU→b3 Light64B/b4 Environment80B→CloudLighting/CloudEnvironment→HDR→기존Tone 흐름을 유지한다.
Type 전환은 조명을 유지하고 Concept은 기존 resolver를 적용한다. Custom Formation schema3/snapshot41 계약 유지.
원시 캐시·View 밀도/투과율 불변은 기존01 자동 회귀로 검사하며 05 후보의 finite/성분별 기여/대비를 추가 기록한다.
화면 품질 채택은 사용자에게 남긴다. 추가 촬영은 자동 필수 작업이 아니다.

### 05 초기 자동 비교와 검사 수정

첫396조건/상한14조건 검사는 통과했다. Urban/F5/18°/-108.5°에서 Sky 또는Ground fill 절반의
최종 HDR 평균 변화는 작았다. Multiple 절반은 해당 성분을 약47%로 낮췄으나 전체 대비가 크게 증가하지 않았다.
Phase 상한2.5에 걸린 가시 표본을 확인했다(4픽셀 간격 표본 중 intensity.2에서9267, .4에서12355).
따라서 조건부 상한4/8 비교를 실행했다. 상한을 풀 때 밝기/대비가 증가하지만 이것은 림 품질 승인이 아니다.

코드 점검에서 초기 상한 전용 shader가 O3, 일반 렌더가 기본 최적화로 컴파일된 차이를 발견했다.
공정한 비교를 위해 상한 variant도 기존 Renderer::CompileShaderFromFile을 통하도록 수정했다.
초기 로그/이미지는 시도 기록이며 채택 근거는 동일 컴파일 정책으로 재검증한 결과를 사용한다.
초기 비교 이미지 폴더 phase-cap-comparison과 최종 비교 폴더 phase-cap-comparison-matched-flags를 구분한다.
이미지는 모두05 분석용이며00 수정 전 원본을 대체하지 않는다. 테스트 환경변수
VCLOUD_LIGHTING_COMPARE_DIR를 지정했을 때만 Urban18°의 기준/.4cap2.5/.4cap4/.4cap8 네 장을 새 파일로 저장한다.
기존 파일이 있으면 덮어쓰지 않고 실패한다. 일반 CTest에는 저장을 요구하지 않는다.

### 05 동일 컴파일 정책의 결과와 한계

Urban/F5/18°/-108.5°에서 가시 픽셀의 Composite HDR 평균/대비(p90-p10)/(p90+p10):

| 후보 | 평균 | 대비 |
|---|---:|---:|
| 기준 Phase.20,cap2.5 | .225918 | .482819 |
| Shadow1.62 | .217258 | .473008 |
| Sky.425 | .225247 | .483361 |
| Ground.425 | .225698 | .482935 |
| Multiple.075 | .219555 | .481350 |
| Phase.40,cap2.5 | .235113 | .461800 |
| Phase.40,cap4 | .295142 | .578764 |
| Phase.40,cap8 | .364164 | .669571 |

이 구도에서는 간접광 감소가 큰 명암 개선을 만들었다고 볼 수 없다. 상한4/8은 밝기/대비를 증가시키지만
에이전트 이미지 관찰상 얇은 테두리뿐 아니라 큰 덩어리가 넓게 밝아진다. 목표 림 달성으로 판정하지 않는다.
이 표는 한 구도의 통계다. 다른 고도/Type/방위의 전체 결과는 matched-candidate-metrics.log를 따른다.
기존 상한은 가시 표본 포화7조건에서 확인되어 4/8 총14조건을 추가했다. 모든 후보 기본값은 미채택이다.

선택 비교 이미지는 phase-cap-comparison-matched-flags의 PNG4장과 원시 RGBA16F4개, settings.json이다.
그 중 Phase.40끼리 비교해야 상한만의 효과를 볼 수 있다. Phase.20 기준과cap4를 바로 비교하면 두 값이 달라진다.
초기 O3 variant 결과와 최종 동일 정책 결과는 기록된 소수점 정밀도에서 같았지만, 공식 비교는 최종 폴더를 사용한다.
촬영은 이 설명에 필요한 조건만 선택했다. 00 baseline 원본을 변경하거나 다시 촬영하지 않았다.

정상 경로 성능 12/12 통과: 최대 case Cloud p95=9.330ms, Frame p95=9.903ms.
조건 OFF Release1920×1080,UI/VSync/캡처Off,60예열/120표본,Base1.50/Near0,Concept별 승인 밀도.
테스트 전용cap4/8의 별도12case 성능은 아직 측정하지 않았으며 채택 시 조합과 함께 다시 검증해야 한다.

### 05 자동 검증 완료 / 사용자 후보 판정 대기 — 2026-09-15

- Debug/Release 빌드 성공. 중간 SDK 조회 오류는 동일 단독 빌드 재실행으로 해소했다.
- Release 전체43/43 통과. 이후 검사 전용 상한 컴파일 정책을 정규 렌더와 일치시키고 관련
  LightingTuning을 재실행하여396독립후보+14상한조건 통과(50.71초). 일반 렌더 계산 변경은 없다.
- 최종 Debug 관련5/5 통과(Phase/Ambient CPU,NoiseLab/RendererState,LightingTuning145.15초).
- HDR finite 및 Debug D3D11 error/resource hazard 검사 통과. 기존01 조명 입력/밀도·투과율 회귀도 전체Release에서 통과.
- 00 baseline VerifyOnly24/24 통과. 원본 덮어쓰기 없음.
- 일반 경로 성능12/12 통과. 상한4/8 채택 시 추가 성능 검증 필요.
- 현재 source-state/source-manifest,실행 파일 hash,04 대비 코드 delta와 검증 로그를05-lighting에 보존했다.

**사용자 판정:** 여섯 항목의 독립 비교와 Phase 상한 이미지를 검토할 준비가 됐다. 새 강도/조합은 미채택이다.
04는 Near0으로 유지하며,05 최종 조합·승인 및06 통합 작업은 남아 있다. 스크린샷 비교는 최종 matched-flags 폴더를 사용한다.

## 05 사용자 피드백 및 조사 — 2026-09-15

사용자는 Shadow exponent가1.62 이상, 최대4에서 내부 차폐를 더 선명하게 보이게 한다고 보고했다.
Sky/Ground fill의 영향은 작고, Multiple attenuation은 전체 밝기를 바꾸지만 명암 효과는 체감되지 않았다.
Phase는 광원 주변을 밝히는 포인트 라이트처럼 느껴졌고 Edge scale의 영향도 작았다.
이를 기본값 채택이나05 최종 승인으로 기록하지 않는다.
사용자 요청에 따라 공개 실무 자료와 현재 코드를 비교했다:
[구름 내부 명암 조사](../research/stage15-cloud-lighting-research.md).
공식 경로는 doc/research/stage15-cloud-lighting-research.md이며, 이번에는 셰이더/설정 변경·추가 촬영·빌드를 하지 않았다.
핵심: 높이 기반 ambient 근사는 실무 사례가 있지만 현재도 존재한다. 태양 방향 내부 명암은 실제 optical depth를
먼저 확인하며, Phase의 각도별 밝기와 자기 차폐를 구분한다. 새 방향성 mask는 필요할 경우 별도 미채택 제안이다.

### 각도 의존 줄무늬 추가 보고 — 2026-09-15

사용자는 특정 각도의 대각선/수평 평행 그림자 줄무늬를 보고했다. 원인 미확정으로 기록한다.
태양 차폐와 줄무늬의 계산 정확도를 먼저 확인한 뒤 명암 값을 확정하고, 높이 마스크는 ambient 보조로 두는 방향이다.
공개 cone/jitter/밀도AA 사례와 현재Y-slice·태양평면격자·View 중점 샘플 구조의 구분을
`doc/research/stage15-cloud-lighting-research.md`에 추가했다. 기존 보간은 구현되어 있으며 누락으로 단정하지 않는다.
코드/기본값/High규격은 변경하지 않았다. 진단 및 해결 완료나05 최종 승인으로 처리하지 않는다.

### 05 차폐 정확도 진단 착수 — 2026-09-15

관찰: 사용자는 태양 각도에 따라 수평/대각 줄무늬를 보고했다. 원인은 미확정이다.
가설: 낮은 태양에서 커지는 빛 적분 간격, 캐시 공간 보간 또는 View 표본 간격의 aliasing을 분리해야 한다.
수정 전 계획: 실제 캐시와 동일 Base 밀도를 25m/12.5m로 직접 적분한 고정 월드 단면을 비교한다.
줄자를 더 촘촘히 대어 원래 눈금 사이의 변화를 확인하는 검사다. 두 참조의 차이도 함께 기록해 참조 수렴을 확인한다.
고정 조건: Urban/세 Type, 시간71, Base1.5, Density shaping.7, Near detail0. 기존 High/Balanced512와 조명 기본값 유지.
추가 기능은 테스트 전용이며 F4 UI나 일반 렌더 설정을 바꾸지 않는다. 샘플 간격/해상도 후보는 원인 확인 뒤 비교한다.
CPU 정상 프레임 → 실제 CB와 texture → 검사 CS → float4 readback → 수치 기록.
위치/step은m, extinction은1/m, tau와T는무차원이다. 캐시 검사값에는 Phase/ambient/View 가중치가 없다.
실제 변경/자동 결과/사용자 판정: 진행 중. 수치 검사 성공을줄무늬 없음 화면 승인으로 취급하지 않는다.

### 05 차폐 진단 1차 결과 — 2026-09-15

**현재 판정: 원인 분리 진행 중. 줄무늬 없음 승인/조명값 확정/06 진행은 하지 않았다.**

사용자 추가 결과: 고도0~10도에서 가장 잘 보이며 다른 고도에서는 잘 느끼지 못한다.
Urban F5/시간71/방위각-108.5/고도5/Phase0/Shadow exponent4의 Direct에서 수평 줄을 재현했다.
Exponent4는 작은 차폐 차이를 드러내는 검사값이며 기본값으로 채택하지 않았다.

실제 변경:
- `--solar-occlusion-test`와 CTest `VolumetricCloud.SolarOcclusion` 추가. F4 UI 추가는 없다.
- `shaders/SolarOcclusionProbe.hlsl`: 실제 Deep Cache 선언과 Base 함수를 재사용하는 CS.
  u1 StructuredBuffer float4 두 개/표본,64×32 표본. F5 XZ 중심의 월드 XY 단면(폭16km/전체 높이)을 검사한다.
  첫 float4는 Near T/25m T/12.5m T/기존 Near 간격의 독립 중점 적분 T,
  두 번째는 혼합 T/Near 가중치/Base 밀도/기존 빛 간격m이다.
  실제 구름 오차는 Near 코어+밀도>.001에서만 집계한다. 전체 Far/보이는 모든 구름의 정확도 검사가 아니다.
- Urban/세 Type × 고도5/10/18/45/70 × 방위각-108.5/-18.5/71.5 =60조건.
  기본/빛 구간4분할/균일밀도.25의3변형으로180조건을 검사한다.
- 균일밀도는 실제 CS 누적/캐시 조회를 CPU 해석해 `exp(-min(tauMax,.25*extinction*남은높이/sunY))`와 비교한다.
  빈 Base 영역도 포함하되 Near 코어 조건은 유지한다. 허용 최대 T 오차1e-4, 실제 최대4.25688e-7.
- 실제 밀도의25m/12.5m 비교 최대 T 차이.000378132. 기본 캐시 최대 T 차이.049098.
  이 값은 표본 집합의 관찰값이며 새로운 전체 화면 품질 합격선을 의미하지 않는다.
- 재현 구도에서 별도 리소스로 XY1024(높이80/40유지), 높이159/79(XY512유지)를 각각 비교.
  159/79는 높이 **구간 수**를 정확히2배로 만든 값이다. 런타임 sanitize/ABI/High 상수는 유지한다.
- 시선25m(최대4096회, 기존 skip/early exit 유지)와 태양 차폐 강제T=1을 테스트 PS로 비교.
- 픽셀 사각형 x[550,750), y[640,710) 안에서만 태양광선50m/25m 직접 적분 참조를 실행.
  그 밖은 기존 캐시다. 전체화면 고정밀 참조가 아니며 Near/Far fade와 동일한 근사는 아니다.
  50/25 참조의 HDR 평균 차이는 영역 안4.4202363e-7, 영역 밖0으로 수렴/격리를 확인했다.
  실행기에도 이 비교를 넣었다(4픽셀 간격 RGB 평균, 각각1e-5 이하). 표본 영역 외 임의 장면 수렴 보장은 아니다.

관찰 → 해석:

| 독립 변경 | 재현 화면 관찰 | 판정 |
|---|---|---|
| 빛 적분4분할 | 수평 줄이 남음. 단면 오차도 항상 감소하지 않음 | 단독 해결책으로 미채택 |
| XY512→1024 | 수평 줄이 남음 | 해상도 증가만으로 해결했다고 볼 수 없음 |
| 높이 간격 절반 | 수평 줄이 남음 | 단독 해결책 미확정 |
| View25m | 수평 줄이 남음 | 시선 step만의 문제라고 확정할 수 없음 |
| 태양 차폐T=1 | 강한 수평 줄이 사라짐 | 차폐 경로가 현상에 관여한다는 증거. 최종 해결책 아님 |
| 좁은 영역의 직접 태양 적분 | 해당 영역의 강한 평행 줄이 완화됨.50/25m 수렴 | 캐시 근사와 차이를 확인. 캐시 좌표/보간/격자 표현/밀도 샘플링의 세부 원인은 미확정 |

주의: 처음 미리보기에서50/25m 참조의 영역 밖도 다르게 보인다고 판단했으나,
HDR 원본 비교 결과 영역 밖 차이는0이었다. 그 시각적 추정을 기각하고 참조를 복원했다.
코드 파일을 이동한 초기 include 탐색 실패와 C++ 변수 삽입 실패는 수정 후 빌드했다.
실패한 빌드의 실행 파일을 검증 결과로 사용하지 않았다.

기록: `captures/stage15-directional-lighting/05-occlusion/`.
현재 비교 원본은 `reference-comparison/`의 PNG/HDR이며 과거 단계00 원본은 건드리지 않았다.
시험마다 자동 촬영하지 않으며 `VCLOUD_SOLAR_COMPARE_DIR`를 지정한 실행만 CREATE_NEW로 저장한다.
중간 comparison/axis-comparison/isolation-comparison/view-comparison은 원인 분리 시도의 이력이다.

다음 조사: 재현 영역을 만드는 월드 표본에서 Near/Far 각각의 원시tau와 태양 평면 투영을 직접 적분과 대조한다.
현재 좁은 XY 단면 검사만으로 원경 Far/혼합 영역까지 정확하다고 결론 내리지 않는다.
고도0~3은 cone fallback이므로5도 캐시 결과와 별도로 검사해야 한다.
후보별 성능12case는 채택 후보가 생긴 뒤 측정한다. 이번 진단 실행 시간은 게임 성능이 아니다.

사용자 렌더 체크(미승인):
- [ ] F5/Urban, F1 Density shaping .70, F2 Base mid octaves1.50을 확인하고 바람/태양 재생을 끈다.
- [ ] F3 방위각-108.5/고도5/Phase intensity0/Shadow exponent4, F4 Direct에서 수평 평행 줄이 보고한 현상인지 확인한다.
      Direct는 직접광만 보여 하늘/지면이 검정이다. 검은 하늘 자체는 실패가 아니다. 구름 안의 반복되는 띠가 문제다.
- [ ] 저장된 `urban-alt5-variant0-mode32.png`와 `urban-alt5-roi-light25m.png`를 비교한다.
      검사영역(550~749,640~709)에서만 차폐 계산이 바뀐다. 사각형 경계 차이는 참조 방식 전환이며 최종 화면으로 채택하지 않는다.
- [ ] 진단 후 F3 `Reset Urban lighting comparison (05)`로 기존 조명으로 복구한다.
      실제 최종 채택값은 없으며 일반 High/512/.70/1.50/Near0을 유지한다.

자동 검증 최종(2026-09-15): Debug/Release 빌드 성공. Release 관련4/4(HighCloudQuality,Stage12ShadowMath,DirectionalLightingDiagnostics,SolarOcclusion) 통과. 최종 Debug SolarOcclusion1/1 통과(27.56초); 앞선 Debug 방향 전달/ShadowMath 포함3/3도 통과했다. HDR finite, 해석해/참조 수렴, D3D11 오류 검사 통과. `git diff --check` 통과(기존 줄바꿈 경고만). 이번 진단에서 전체CTest·12case성능·사용자 화면승인을 완료했다고 기록하지 않는다. 로그는05-occlusion/final-release-tests.log와final-debug-tests.log.

### 05-B 저고도 줄무늬 수정·명암 확정 계획 — 2026-09-15

사용자 정정: 직접 적분 스크린샷에서도 평행 줄이 보인다. 이전의 부분 영역 완화 관찰은 화면 해결의 증거로 채택하지 않는다. 전체 화면 대부분은 여전히 캐시였으므로 참고 영역을 명확히 나누지 않은 전달도 수정한다.

진행 순서와 완료 조건:
1. **참조 검증:** Urban/F5/시간71/고도5/Phase0/Shadow4 고정. 전체 화면의 직접 적분을 타일 단위로 렌더해 기존 부분 영역의 오해를 없앤다. 동일 뷰 표본에서50m/25m 태양 참조 수렴과 전체 HDR finite를 검사한다. CPU/GPU 태양 방향은 기존 검사를 재사용한다.
2. **조회 경로 분리:** Near만/Far만/기존 혼합을 같은 화면에서 비교한다. 물질과 해석 가능한 tau 필드를 이용해 UV 투영·높이 보간을 분리한다. 0~3도 cone과3~10도 cache를 구분한다. 참조에도 줄이 남으면 View/밀도까지 조사하며 캐시 탓으로 고정하지 않는다.
3. **최소 수정:** 원인이 확인된 계산/바인딩 오류부터 고친다. 예산 부족이면 빛 적분·XY·높이 저장 중 해당 축만 조절한다. 실패 후보는 기본값에 누적하지 않는다. High/512는 비교 기준이고, 변경 후보의 비용은 별도 측정한다. TAA/블러를 임의 도입하지 않는다.
4. **안정성 검증:** Urban/세Type, 고도0/1/2.9/3.1/5/10/18/45/70, 방위 회전·카메라 이동·Near/Far 전환, Direct/VisibleSunT/Composite 확인. 수치 통과와 사용자 화면 승인을 분리한다.
5. **명암 확정:** Density.70/Base1.50/Near0/노출/WB 고정, Shadow exponent1.35/1.62/2/3/4 순서 비교. 기존 간접광/Phase를 한 항목씩 복원한다. 줄을 숨기기 위해 명암을 약하게 고정하지 않는다. 가장 낮은 비용의 안정 후보에서 사용자가 값을 선택한다.
6. **05 승인 뒤06:** 세 Concept/F5~F8·Custom·타입 유지, Release 전체CTest,12case60예열/120표본 성능(Cloud p95≤10ms/Frame p95≤16.67ms), 최종 같은 조건 촬영과 문서 정리.

재사용: 기존 태양probe/실행기/원본. 새 기능: 타일 직접 적분·경로별 비교. 이번에 미확정: 최종 차폐 강도/해상도/형태. 상수버퍼 ABI와 저장 schema는 바꾸지 않는다. 수정 전후 원인·수치·실패 후보를 이 절에 이어 기록한다.

05-B 원인 후보(구현 전): 전체 직접 참조50/25m HDR 평균5.84554e-8/최대5.40626e-5로 수렴. Far-only에서 평행 줄이 넓게 나타남. 기존 태양 평면 up texel250m는 월드Y 고정 시250/sin(5)=2868m의 수평 이동에 해당한다. Near와Far 혼합 자체보다는 Far의 저고도 표본 면적을 우선 조사한다.
후보: lightUp 방향 폭만 `Wup=W*sin(alt)+층두께*cos(alt)`로 설정한다. W는 기존24/128km이며 구름층 중앙 중심에서 지평선 방향W와 높이H를 포함하는 투영 범위다. right 폭과해상도512·slice80/40·적분상수는유지. 생성/조회가 같은 식을 사용한다. 빛 표본당 광선 적분 간격은 이번 후보에서 바꾸지 않는다. 우선 test macro로 양쪽을 비교하고 유효하면 CPU center snap도 동일한 texel 크기로 맞춘다. 예상: 높이별 수평 간격이 좁아져 규칙적인 Far 띠가 줄고 직접 참조 오차 감소. 실패:구름/지면 그림자 범위가 잘리거나 Near/Far 경계가 보임.

05-B 추가 후보 비교(구현 전): 투영+250m 후보의5도 참조 오차는 평균1.84771e-5/최대.00553136으로 기존 대비 평균72.5% 감소했다. 그러나3.1도에서 더 가는 반복 띠가 관찰되어 화면 완료로 판정하지 않는다. 후보8은 수정 투영/적분을 유지한 채 높이 구간만2배(159/79장), 후보9는 XY만1024로 바꾼다. 별도 임시 자원이며 일반512²/80·40 계약은 유지한다. 3.1도 전체50m/25m 참조로 남은 오차를 다시 분리하고, 비용이 증가하는 후보는 사용자 승인 전에 기본값으로 채택하지 않는다.

### 05-B 실제 변경과 판정 — 2026-09-15

**현재 상태: 수정 후보를 실행할 수 있다. 줄무늬 없음과 최종 명암 값은 사용자 미승인, 06 미착수.**

#### 관찰 → 원인 구분

- 예전 "직접 적분"은 작은 사각형만 참조였다. 이번에는 전체 화면을50m/25m 태양 적분으로 비교했다.
- 태양 광선에 수직인 격자와 월드 높이 slice를 함께 쓰면, 같은 높이에서 up texel 간 수평 거리가
  `upTexel/sin(고도)`가 된다. 고도5도/기존 Far에서는250m가 약2868m에 해당한다.
  빛에 비스듬하게 댄 눈금자를 지면에 펼치면 눈금 간격이 크게 벌어지는 것과 같다.
- Near/Far 각각의 tau를 XY bilinear로 읽고 두 높이를 선형 보간한 뒤 T로 바꿔 혼합하는 코드는 이미 있었다.
  보간 누락이나 Near/Far 뒤바뀜을 발견한 것은 아니다. Far-only/참조/축별 후보 비교가
  저고도 격자 간격과 높이 저장의 근사 오차가 현상에 관여한다는 증거를 주었다.
- 전체 참조의 View 밀도 적분은 기존 High다. 따라서 이 결과가 모든 시점의 View aliasing까지 없음을 뜻하지 않는다.
  사용자03 세로줄 전체를 같은 원인으로 확정하지도 않는다.

#### 실제 변경 · 파일 책임 · 고정 조건

| 위치 | 변경과 이유 |
|---|---|
| Stage12ShadowParameters.hlsli / Stage12ShadowMath.h | Wup=W*sin(a)+H*cos(a). 수평 범위와 층 두께를 담는 up 투영 폭 |
| Renderer.cpp | right/up 각각의 texel 간격으로 카메라 중심 snap. 생성·조회와 같은 폭 사용 |
| CloudDeepShadow.hlsl | 각 저장 높이 구간을250m 이하 태양 광선 구간으로 나눠 중점 적분. 높은 태양에서는 필요한 만큼만 읽음 |
| Stage12Shadow.hlsli | 같은 Wup로 UV를 계산. 기존 XY/높이 보간·T 혼합·fade 규칙 유지 |
| DirectionalLightingBaseline.cpp | 전체화면 타일 참조, Near/Far 강제 조회, 이전 방식 재현, 투영/적분/높이/XY 독립 비교 |
| Renderer/main/NoiseLab | `--shadow-height-2x` 비교 실행. F3 Deep Cache에 실제159+79 및05-B comparison 표시. schema41 deepCache에 투영/적분 방식 기록 |

CPU 태양/층 범위 → b8 basis·center·폭·slice 수 → CS가R32 tau 배열 생성 → Cloud/Scene이 같은UV로 조회
→ 표본 직접광/환경광/다중 산란 → HDR → Tone의 순서다.
거리/폭/적분 간격은m, extinction은1/m, tau/T와 보간 비율은무차원이다.

일반 실행:512²×80/40,120MiB. 높이 후보:`--shadow-height-2x`에서512²×159/79,238MiB.
159/79는 장수가 아니라 **높이 구간 수**를 정확히 두 배로 만든 결과다.
수평 기준24/128km·High View 상수·8-tap cone·3도 경계·밀도.70·Base1.50·Near Detail0·조명 기본값은 유지한다.
b7/b8 크기·offset, Custom schema3, snapshot schema41은 유지한다. 비교 인자는 Custom/내장 기본값으로 저장하지 않는다.
0~3도는 기존 cone 경로다. 이번 캐시 수정의 효과 범위와 구분하여 확인해야 한다.

#### 수치 및 선택 이유

Urban/F5/시간71/방위-108.5/Phase0/Shadow4/1920×1080 고정, 전체RGB를 `L/(1+L)`로
정규화한 HDR 오차다. 검정 하늘도 평균에 포함되어 값이 작으며 미학적 합격선이 아니다.

| 고도 | 후보 |25m 참조 대비 평균오차| 최대오차 |
|---|---|---:|---:|
|5°|기존 정사각/1회 적분|6.72206e-5|.0303939|
|5°|투영만 수정|3.21018e-5|.0208345|
|5°|투영+250m 적분|1.84771e-5|.00553136|
|3.1°|기존 정사각/1회 적분|8.54097e-5|.0391151|
|3.1°|투영+250m 적분|2.06617e-5|.00947972|
|3.1°|위 수정+높이 구간2배|8.24017e-6|.00409044|
|3.1°|위 수정+XY1024|2.01603e-5|.00875544|

50m/25m 전체 참조 수렴:5° 평균5.84554e-8/최대5.40626e-5,
3.1° 평균4.01431e-8/최대5.40626e-5.
투영+적분 후보의 평균오차는5도에서72.5% 감소했다. 3.1도는 높이 세분화까지 하면90.4% 감소했다.
5도와3.1도를 서로 다른 조건으로 계산했으므로 후보 효과를 교차 비교하지 않는다.

- 기각/보류: 투영만 바꾸면 가는 띠가 남음. XY1024는 비용 대비 추가 개선이 작아 일반 경로에 넣지 않는다.
- 화면 비교 대상으로 준비: 투영+250m, 추가 높이159/79. 에이전트 관찰에서 높이 후보의 반복 띠가 더 줄었다.
  완전한 무줄무늬 판정과 비용 수용은 사용자에게 남긴다.
- Shadow exponent4는 오류를 확대해서 보는 검사값이다. 채택값이 아니다.
  안정 후보 승인 후1.35/1.62/2/3/4를 같은조건에서 비교하고 최종값을 정한다.

#### 원본과 재현

`captures/stage15-directional-lighting/05-banding/`에만 추가했다.00-baseline은 다시 찍거나 덮어쓰지 않았다.
핵심 비교: `low31-refinement/baseline.png`, `reference25.png`, `corrected-cache.png`,
`corrected-height2x.png`. 같은 stem의.rgba16f는1920×1080 RGBA half-float/header 없는 원본이다.
`full-reference`→`projected-candidate`→`projected-substeps`는 실패/중간 후보 이력이다.
빈 redirected .log는 GUI 실행의stdout 연결 실패이며 CTest의 실제 로그로 결과를 기록한다.
필요한 비교만 선택 저장했고, 자동 수치 검사 자체는 촬영을 요구하지 않는다.

#### 사용자 체크리스트 — 승인 대기

시작: 프로젝트 루트에서 `.\build\Release\VolumetricCloud.exe --shadow-height-2x` 실행.
기본 규격 비교는 인자를 빼고 실행한다. 동시에 두 앱을 띄우지 않고 차례로 비교한다.

- [ ] **후보 확인:** F3 Ambient floor 아래 Deep Cache가159+79 height slices (05-B comparison)인지 확인한다.
  일반 실행은80+40이다. 조작 가능한 새 F4 샘플 간격 slider는 없다. 숫자가 다르면 다른 후보를 보고 있는 것이다.
- [ ] **고정 설정:** F1 Urban/F5, Cloud movement speed0, Density shaping.70; F2 Base mid octaves1.50;
  F3 Near detail shadows0, 태양 각도 수동(기본 재생 꺼짐), 방위-108.5. 노출/WB는 바꾸지 않는다.
  Type은Urban 조명을 유지하는 Stratus/Cumulus/Mixed를 선택한다. Concept을 다시 누르면 조명이 초기화됨에 주의한다.
- [ ] **띠 확인:** F3 Phase intensity0, Shadow exponent4. 고도3.1→5→10, F4 Direct에서 내부의
  반복 평행/대각 띠를 확인한다. 정상은 구름 덩어리와 골을 따르는 연속적인 명암이다.
  일정 간격의 줄이 구름 모양을 가로지르면 실패다. Direct의 하늘/지면이 검정인 것은 정상이다.
- [ ] **경로 경계:** 고도0→1→2.9→3.1을 천천히 바꾼다.3도 아래는기존cone,위는cache다.
  낮은 입사광 때문에 화면이 어두워지는 것과, 임계각에서 명암이 갑자기 바뀌는 것을 구분한다.
  순간적인 띠/튀는 그림자/큰 밝기 점프는 실패로 각도를 함께 기록한다.
- [ ] **Near/Far·이동:** 고도5에서 F5~F8과 WASD/Shift 이동을 비교한다.
  가까운 골이 원경으로 넘어갈 때 그림자가 계단처럼 튀거나 격자와 함께 움직이면 실패다.
  F4 Visible Sun T에서 하늘검정/구름높은T흰색은진단표시다. 외곽 mask만으로 정확도 승인하지 않는다.
  숫자0 Composite로 복귀해 지면/건물 그림자가 잘리는지도 확인한다.
- [ ] **명암값 선택:** 위 항목이 통과한 뒤 F3 Reset Urban lighting comparison (05), Shadow exponent
  1.35→1.62→2→3→4를 고도18/45/70에서 한 항목씩 비교한다. Visible Sun T는같고 Direct 명암만
  깊어져야 한다. 내부가검게막힘/기존띠확대는기각. 선택값,세Type결과,역광림을기록한다.
  Phase.20/기존fill은복원한 상태로 Composite를 최종 확인한다.
- [ ] 사용자 판정/일자/남은 문제가 기록된 뒤에만05값을채택하고06으로진행한다.

자동 결과는 다음 실행 로그 절에 추가한다. 체크박스는 사용자 확인 없이 체크하지 않는다.


#### 05-B 자동 검증 최종 기록 (2026-09-15)

- Debug/Release 빌드 성공. 일반 렌더 수정 후 Release 전체CTest46/46 통과(168.31초).
- 그 뒤 F3 후보 표시/schema41 deepCache 설명과 두 규격648조건을 보완했고,
  최종 Debug 관련4/4(231.45초), Release 관련2/2(24.42초)를 다시 통과했다.
- 최종3.1도 원본 촬영 실행도1/1 통과(24.24초). 전체50m/25m 참조 수렴,648개 화면HDR finite,
  CPU/GPU 방향/CB 전달·lighting-only 불변·Debug D3D11 오류 검사 통과. 모든 이동 궤적의 품질 증명은 아니다.
- 두 높이 규격의12case 성능 모두 통과. 일반80/40의최대 Cloud/Frame p95=9.075/10.222ms,
  159/79 후보=9.310/10.653ms. 동일GPU/조건이지만 run 간변동이 있어 개별delta를고정비용으로해석하지 않는다.
- git diff --check 통과. 기존미커밋변경/00-baseline 보존,commit/push 없음.
- 최종검토이미지:05-banding/review/. 설정·소스/exe SHA256은review-source-manifest.json,
  이미지SHA256은review-image-sha256.json. baseline은05-B 이전 알고리즘을test macro로재현한것이며00 원본을대체하지 않는다.
- 로그:release-full-46.log,final-debug.log,final-release-related.log,review.log.
- 사용자 판정:미승인. 다음필수작업은높이후보/저고도/이동확인과명암값선택이다. 06을시작하거나05를완료로기록하지 않는다.

### 05-B 사용자 비교 결과 — 2026-09-15

관찰(사용자): 두 규격 모두 수평 줄무늬가 사라졌다. 2.5→3.5도에서 내부 명암이 탁해지고 꺾이는 느낌이 가장 강하다. 159/79는 밝은 부분이 조금 더 밝지만 그림자 차이는 작다. 두꺼운 Stratus/Cumulus 등에서는 지표 가까이 보이는 구름 하부 그림자가 평평하다. 이는 구름 자체의 하부 명암 보고이며 지면에 투영된 그림자와 구분한다.

규격 정정: 일반 규격은 Near80/Far40이다(50/80이 아님). 후보는 Near159/Far79로, 높이 저장 구간79/39를158/78로 두 배 늘렸다. 두 규격 모두 XY512와 수정된up투영/250m 이하 적분을 공유한다.159/79는 형상이나 조명 강도를 높이는 옵션이 아니다.

판정: 수평 줄무늬 항목만 사용자 해결 확인으로 기록한다. 경계 안정성/하부 명암/05 최종 승인은 미완료다. 시각적 추가 이득이 작다는 피드백에 따라 현재 기본80/40을 다음 조사 기준으로 유지하고,159/79의 기본 채택은 보류한다. 과거90.4%는 특정 Direct 참조의 수치 개선이지 체감 품질 향상률이 아니다.

코드 확인: Renderer::UpdateStage12ShadowParameters와Stage12Shadow.hlsli는sin(3도)를경계로cacheReady/valid를전환한다. CloudLighting.hlsli는 valid이면cache, 아니면cone으로 즉시분기하며 두 경로 사이 전환 혼합이 없다. 따라서 보고된2.5~3.5도의 불안정은 이 전환과 일치한다. 이것만이 원인이라고 확정하지는 않는다. T/tau가 직접광뿐 아니라 환경광 차폐/다중 산란에도 전달되므로 복합적인 밝기 변화가 가능하다.

다음 조사 계획(미구현):80/40에서 같은 고정 월드 표본의cone T/tau와cache T/tau를3도 주변에서 나란히 기록한다. 차이가 큰 원인과구름/지면사용경로를구분한뒤, 전환 구간에서의연속혼합/유효범위 후보를검증한다. 임계각만옮기거나명암지수를낮춰현상을숨기지않는다. 고정High cone 상수는 유지한다.

하부 평탄화는 별도 가설이다. Noise/Weather의 local bottom·typed vertical profile, 두꺼운 구름의 태양 차폐 포화, CloudEnvironment의 높이 가중치/Multiple 기여를 구분한다. F4 Transmittance(시선 투과율)에서도 경계가 평평하면 형상/밀도를, Direct에만 보이면 차폐를, Composite에서 강해지면간접광/합성을우선확인한다. 스크린샷만으로밀도중첩/형상문제로확정하지않으며 하부를 무작정깎거나밀도기본값을바꾸지않는다.

이번 턴은 코드 확인과 사용자 결과 기록만 수행했다. 렌더 코드/기본값 변경 및 새촬영/빌드는 하지 않았다. 06 미착수.

## 05-C 시작 — 문제·해결법 정리와 3도 전환 안정화 (2026-09-15)

사용자 지시: 기존 해결 과정 문서를 갱신하고,80/40 유지 → 3도 전환 안정화 → 하부 명암 원인 분리 → 명암값 확정 순서로 진행한다. 명암값 승인 전06은 시작하지 않는다.

| 문제 | 적용/비교한 해결법 | 현재 판정 |
|---|---|---|
| 밀도가 희박하고 내부 차폐가 약함 | Density shaping .70 | Urban/세Type 승인. 단독으로 원하는 명암을 완성하지는 못함 |
| 큰 덩어리 내부 굴곡 부족 | Base 중간 octave1.50 | 사용자 승인. Weather 배치 유지 |
| 가까운 Detail과 그림자 불일치 가능성 | Near detail0/.5/1 비교 | 체감 개선 작음.0 유지 |
| 저고도 수평 줄무늬 | up 투영 폭 수정 +250m 이하 빛 적분 | 두 높이 규격 모두 사용자 수평줄 소멸 확인 |
| 저장 높이 해상도 부족 가능성 | 80/40 대159/79 | 참조 오차는 감소했으나 체감 차이 작음.80/40 유지 |
| 2.5~3.5도에서 탁하고 꺾이는 명암 | 3도 cone/cache 즉시 분기 확인 | 이번 단계에서 안정화 후보 구현/검증 |
| 두꺼운 구름 하부가 평평함 | 형상/차폐/간접광 성분 분리 예정 | 아직 원인 미확정. 무작정 profile을 바꾸지 않음 |
| 최종 명암·실버라인 부족 | 조명 독립 비교 도구 구현 | 안정화 뒤 사용자 값 선택. 미확정 |

### 수정 전 관찰·가설·고정 조건

현재3도에서 서로 다른 근사인cone과cache를 바로 교체한다. Near/Far 공간 혼합과는 다른, 태양 고도에 따른 경로 전환 문제다. TV 입력을 바로 바꾸듯 밝기가 튈 수 있다. tau는Multiple/환경광에도 재사용되므로 Direct뿐 아니라 Composite의 탁함도 바뀔 수 있다.

후보:3~5도에서smoothstep 가중치로cone의T→cache의T를 연속 혼합하고, 혼합T에서tau=-ln(T)를 다시 계산한다.3도 아래cone와5도 위cache는 그대로 사용한다. 두 값을 모두 계산하는 추가 비용은 전환 구간에만 있다. 지면 경로는3도 아래 기존T=1이므로1→cache T로 같은 가중치를 적용해 그림자가 갑자기 생기는 것을 막는다. 높이80/40,XY512,밀도.70/Base1.50/Near0,조명 기본값,View High와cone8tap 상수/offset은 유지한다.

검증:이전 즉시분기 shader와새경로를 고정 시간/카메라/밀도에서 비교한다.3도와5도 경계 양옆의 HDR/Visible Sun T 차이,1/2.5/3/3.5/4/5/10도 유한값,형상·시선투과율 불변을 검사한다. 보간은 계산 차이를 없애는 물리 해법이 아니라 경로 전환을 연속으로 만드는 근사이므로, 넓게 늘어난 탁함/새로운띠는 사용자 기각 기준이다. 전환 구간 추가 비용을 별도 측정한다.

이후 하부 명암은 시선투과율/Direct/Multiple/Sky/Ground/Composite를 같은 조건에서 비교해 원인을 분리한다. 높이 마스크는 기존환경광의역할을확인한후판단한다. 이번시작시점에서새촬영은필수가아니며00-baseline은보존한다.

05-C 수식 검토 보완: T를 먼저 섞고log로tau를복원하면cone의T가float에서0인표본은원래큰tau정보를잃는다. 임의의T하한이Multiple의경계값을바꿀수있으므로, 구름경로는tau=lerp(coneTau,cacheTau,w), T=exp(-tau)로구현한다. 두성분에일관된차폐를전달하고양끝에서기존tau를보존한다. 지면은tau소비자가없어기존중립T=1에서cache T로fade한다. 이구분은Near/Far의기존공간T혼합을변경하지않는다.

05-C 초기 검사 이력: Urban/F5 방위-108.5에서3도±.001 경계의Visible Sun T HDR 평균차이는기존.00504565→새5.81245e-6,Composite는.000167533→약4.62e-6이었다. 단,전환밖5.1도 비교에서HDR 최대.00158967이기존.001합격선에걸려첫검사는실패했다. HDR 평균3.28619e-8,PNG최대1/255이며5도위는코드상추가연산없이기존cache T/tau를그대로반환한다. 서로다른PS 컴파일변형의OFF Release 비교는HDR 평균1e-5와PNG평균1/255·최대2/255를판정에쓰고HDR 최대는관찰값으로구분했다. 이를물리값완전일치/bit-exact로주장하지않는다. 시선투과율검사는기존전체허용오차를유지한다. MSBuild SDK경로접근오류1회는단독빌드재실행으로복구했다. 실패한실행파일로검증하지않았다.

### 05-C 실제 변경과 1차 원인 분리 — 2026-09-15

- 일반 실행은 Near80/Far40, XY512를 유지한다. 159/79는 테스트 옵션으로만 남는다.
- 구름은 3~5도에서 cone/cache의 광학 깊이 tau를 smoothstep으로 혼합하고 T=exp(-tau)를 계산한다. 3도 아래 cone, 5도 이상 cache는 기존 경로다. 지면 그림자는 기존 중립 T=1에서 cache T로 전환한다.
- F3에 `Sun shadow transition: 3-5 deg (05-C)` 읽기 전용 안내를 추가했다. schema41 진단에는 전환 범위/광학 깊이 혼합 방식을 기록한다. 상수버퍼 크기와 조명·형상 기본값은 바꾸지 않았다.
- Release SolarTransition: 72개 경계 조건 통과. Urban/세 Type × 방위 3종 × 3도/5도 양옆 ±.001도 × Visible Sun T/Direct/Composite를 검사했다. 전환 밖 화면 허용오차, 시선 투과율 불변, HDR finite와 D3D 오류 검사도 통과했다. 모든 연속 이동에서의 시각적 안정성을 대신하는 검사는 아니다.
- 전환 성능 8개 case(F5/F7 × 2.5/3.5/4.5/5.5도): 60프레임 예열·120표본, Release OFF 1920×1080, UI/VSync/캡처 Off. 최대 Cloud p95=4.75648ms, Frame p95=6.28531ms로 예산 통과. 3~5도에는 cone과 cache를 함께 쓰는 비용이 있다.

하부 명암의 확인된 사실: 같은 F5/시간71/고도18 조건에서 Stratus의 평평한 하단 윤곽은 시선 투과율(mode8)에도 보인다. 따라서 이 윤곽 전체를 조명 오류라고 볼 수 없다. Cumulus에는 더 불규칙한 윤곽이 남아 있고, Visible Sun T는 하단까지 균일한 검정이 아니라 공간에 따라 달라진다. Multiple에도 밀도 형상을 따르는 밝기 분포가 보인다. 화면 하단 ROI 평균은 화면 기여 통계이며 물리적으로 같은 높이의 표본 통계가 아니다.

코드 근거와 추정: Weather의 localBottom은 globalBottom+lift이다. lift는 (1-thicknessPotential)^1.5에 비례하고 Stratus 쪽 typeScale은 .15다. 두꺼운 기둥일수록 공통 바닥 가까이에 정렬되기 쉬워 평평한 하단의 원인 후보가 된다. CloudEnvironment는 이미 local height로 Sky/Ground를 가중한다. 이는 코드상 확인한 작동 방식이며, 사용자가 말한 내부 그림자 평탄화의 단일 원인을 확정한 것은 아니다. profile/Weather/밀도는 이번 변경에서 유지한다.

다음 순서: 전환 후보 사용자 확인 → 동일 하부 영역에서 투과율/태양 차폐/Direct/Multiple 비교 및 필요 시 한 변수만 제거한 실험 → 조명값 사용자 선택 → 06. 전환이 넓은 구간의 탁함으로 바뀌었다면 후보를 기각하고 원인을 더 추적한다.

### 05-C 사용자 검증 체크리스트 — 미승인

- [ ] 일반 Debug/Release 실행 파일을 옵션 없이 실행한다. F3에서 Deep Cache Near80/Far40과 전환 안내를 확인한다. 159/79이면 이전 테스트 옵션으로 실행한 것이다.
- [ ] F1 Urban, F5를 선택하고 바람/태양 재생을 끈다. F3 방위각 -108.5도, 고도 2.5→3→3.5→4→5→5.5도를 천천히 왕복한다. F4 Direct와 Composite에서 3도 직전/직후 명암이 갑자기 꺾이지 않아야 한다. 새로운 띠, 전환 구간 전체가 탁해지는 모습은 실패다.
- [ ] 같은 Urban 조명으로 Stratus→Cumulus→Mixed를 반복한다. F5와 F7에서 확인하고 F6/F8 및 카메라 이동으로 추가 확인한다. 밀도 .70/Base1.50/Near0은 유지한다. 특정 Type/거리에서만 나타나는 튐도 기록한다.
- [ ] 문제가 약하면 F3 Shadow exponent4/Phase intensity0으로 차폐 차이를 강조한다. F4 Visible Sun T의 하늘 검정은 구름 기여 없음이며 구름의 밝기는 태양 투과율이다. 경계에서 새 띠가 생기지 않아야 한다. 확인 후 Urban 조명 기본값 Shadow1.35/Phase.20으로 복원한다.
- [ ] 하부 비교는 고도18/45에서 숫자8(흰색=시선 투과, 검정=차폐), F4 Direct/Multiple/Composite를 번갈아 본다. 평평한 '외곽선'과 내부의 '어두운 면' 중 어느 것이 문제인지 구분한다. 숫자8에도 같은 외곽선이 있으면 조명만의 문제로 판정하지 않는다.
- [ ] 사용자 판정과 최종 명암값은 아직 미확정이다. 자동 통과로 체크하거나 06을 시작하지 않는다.

05-C Debug 검증: Debug 빌드 성공, Stage12ShadowMath/SolarTransition 2/2 통과(169.38초). Debug D3D11 오류 검사 포함. Release와 같은 경계/밀도 불변/하부 성분을 검사하고, 성능 판정은 Release에만 적용했다. 로그:05-transition/debug-related.log. 실행 가능한 일반 Debug/Release 모두80/40과3~5도 전환 후보를 포함한다. 사용자 화면 승인은 대기 중이다.

05-C 최종 자동 검증 기록(2026-09-15): Debug/Release 빌드 성공. Debug 관련2/2 통과, Release 전체 CTest47/47 통과(258.88초). 기존12case HighPerformance, SolarOcclusion, SolarBanding, 조명/밀도/설정/핫리로드와 새 SolarTransition 포함. 실패 후보를 기본값으로 누적하지 않았으며, 미채택159/79는 별도 테스트 옵션이다. 이번 전환 후보 자체의 사용자 화면 승인은 대기 중이다. git diff --check 통과. 기존 미커밋 변경과00-baseline 보존, commit/push 없음. 로그:05-transition/release-full-47.log. 비교 이미지/설정/소스·셰이더·exe SHA256은 같은 폴더의 README.md, review-manifest.json, source-sha256.json을 참조한다. 다음 사용자 판정 뒤 하부 원인 분리와 명암값 선택을 이어가며06은 아직 시작하지 않았다.

## 05 사용자 승인 및 06 시작 — 2026-09-15

사용자: “하부 명암 원인 분리는 나중에 해도 돼. 명암값을 이대로 확정하고 06 순서로 이어가자.”
판정: 현재 배포 소스의 명암/3~5도 전환/80·40을 채택하고06 진행 승인. 개별 미보고 화면 항목을 모두 직접 검사했다고 소급 기록하지 않는다. 하부 평탄화는 해결이 아니라 사용자 요청으로 후속 분석에 이관한다.

확정 기준: Urban Shadow exponent1.35, Sky/Ground fill 각.85, Multiple attenuation.15, Phase intensity.20, phase cap2.5, edge optical depth2. Density shaping.70( Urban/세 Type), Base mid octave1.50, Near detail0, XY512 Near80/Far40,3~5도 tau 전환. Meadow/Snow의 개별 조명/밀도 기본값은 유지한다. 세션에서 별도로 바꾼 미보고 UI 값은 추측하지 않는다.

06 목표: 승인 결과의 프리셋 전환 유지, 미채택 실험 조작 정리, 자동 회귀/성능, baseline과 같은 조건의 최종 비교 촬영, 공식 기록과 사용자 최종 확인. 하부 명암 수정은06 범위에서 제외한다.

수정 전 이유: 기존 baseline 실행기는 Density shaping0/Base1.00을 강제로 재적용하므로 그대로 최종 촬영에 쓰면 승인된 형상이 사라진다. 별도 final 모드를 추가해 카메라/시간/태양/노출 조건은 재사용하고 승인된 형상 기본값을 유지한다. 기존00 경로를 덮어쓰지 않고06-final 아래 새 clean 세트를 저장한다. 일반 UI의03 후보 선택은 승인1.50 표시로 정리하고05 후보 나열은 승인값 안내로 바꾼다. 실험 재현은 테스트 실행기에 보존한다.

## 후속 기록

[06 림 개선·경계 진단 기록](stage15-cloud-rim-lighting.md)에서 이어간다. 06 착수 이후 본문은 중복 보관하지 않는다.
