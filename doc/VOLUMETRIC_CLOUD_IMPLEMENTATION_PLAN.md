# Volumetric Cloud 프로젝트 단계별 구현 계획서

> DirectX 11 + HLSL 학습 프로젝트 · 유지보수 정본 · 2026-08-09 개정

## 문서 목적

이 문서는 상수 밀도 안개부터 포트폴리오용 볼류메트릭 클라우드까지의 구현 순서, 각 단계의 범위와
검증 조건을 설명한다. 과거 단계 번호는 유지하지만 실제 실행 순서는 다음과 같다.

`0~8 완료 → 13 선행 → 9 재개 → 10 → 11 → 12 → 14 → 15`

단계 9 구현은 삭제하지 않는다. 소규모 AABB Dense 성능 기준 실패로 보류하고, 단계 13 대규모
평면 구름층의 사용자 승인 뒤 Optimization Off를 새 화질·성능 기준으로 삼는다.

## 프로젝트 목표와 범위

- 목표 환경: UE5 Open World 기본 맵과 비슷한 약 2km × 2km 지상형 인디 시뮬레이션 맵
- 최종 구름: 행성 곡률이 없는 대규모 평면 구름층
- 내부 단위: CPU와 HLSL 모두 meter, UI와 문서에서만 km 병기
- 공식 카메라: 구름 아래, 내부, 위
- 제외: 구형 셸, Ray-Sphere, 행성 카메라, 원점 재배치, 지상에서 우주로 전환

## 전체 단계 한눈에 보기

| 실행 | 단계 | 핵심 결과 | 상태 |
|---:|---:|---|---|
| 1 | 0 | DX11 풀스크린 레이와 Scene Depth 합성 | 완료 |
| 2 | 1 | 상수 밀도 AABB와 Beer-Lambert | 완료 |
| 3 | 2 | 단일 3D Base Noise | 완료 |
| 4 | 3 | 높이 프로파일 | 완료 |
| 5 | 4 | Base/Detail erosion | 완료 |
| 6 | 5 | Weather Map과 구름 종류 | 완료 |
| 7 | 6 | 태양 Light Ray와 단일 산란 | 완료 |
| 8 | 7 | Dual-lobe Phase Function | 완료 |
| 9 | 8 | 환경광과 다중 산란 근사 | 완료·승인 |
| 10 | 13 | 대규모 평면 구름층과 km 도메인 | 구현·승인 대기 |
| 11 | 9 | 기본 최적화 재측정 | 보류 |
| 12 | 10 | 저해상도 렌더·업샘플링 | 예정 |
| 13 | 11 | Jitter·Temporal Reprojection | 예정 |
| 14 | 12 | Cloud Shadow Map·Light Cache | 예정 |
| 15 | 14 | 대기·지면 조명 통합 | 예정 |
| 16 | 15 | 최종 품질·성능 조정 | 예정 |

# 1부 · 구축 완료 단계

## 단계 0 — 기반 구성

Win32 창, DirectX 11 장치, 풀스크린 삼각형과 픽셀별 월드 레이를 만든다. 불투명 진단 장면의
Depth SRV를 구름 패스에서 읽고, 구름 색·투과율을 장면과 합성한다. 카메라 역 View-Projection,
리사이즈와 런타임 HLSL 핫 리로드를 검증한다.

## 단계 1 — 상수 밀도 AABB

slab 방식으로 레이와 유한 박스를 교차하고 일정한 밀도를 Beer-Lambert 법칙으로 적분한다.

```text
T = exp(-density × extinction × distance)
color = background × T + cloudColor × (1 - T)
```

평행축, 카메라 내부, Scene Depth 제한과 퇴화 입력을 CPU 회귀 테스트로 고정한다. 이 AABB는 이후
단계를 배우기 위한 역사적 기준이며 단계 13에서 실제 포트폴리오 도메인으로 교체한다.

## 단계 2 — 단일 3D Noise

월드 좌표의 3D value noise로 일정한 안개를 덩어리로 나눈다. coverage와 wind를 분리하고 Noise Lab의
XY/XZ/YZ 단면에서 같은 밀도 함수를 확인한다. 시간 고정 시 결정론적 출력이어야 한다.

## 단계 3 — 높이 프로파일

구름 바닥과 천장에서 smoothstep fade를 적용한다. 높이 비율은 바닥 0, 상단 1이며 퇴화 두께는
밀도 0으로 처리한다. 큰 XZ 모양은 유지하면서 평평하게 잘린 위아래 경계를 부드럽게 만든다.

## 단계 4 — Base Shape와 Detail Erosion

저주파 Base가 큰 덩어리를, 고주파 Detail이 가장자리를 깎는다. Base가 비어 있으면 Detail 호출을
생략하지만 레이 간격을 크게 건너뛰는 단계 9 최적화와는 구분한다.

## 단계 5 — Weather Map과 구름 종류

주기적인 2D Weather Map으로 넓은 지역의 coverage와 cloud type을 정한다. Weather UV는 월드 XZ에
고정하고 음수 좌표도 양의 주기로 wrap한다. 구름 종류가 높이 프로파일을 바꾸며 PNG/JSON export로
입력과 해시를 기록한다.

## 단계 6 — 태양광과 단일 산란

각 View 표본에서 태양 방향으로 Base-only Light Ray를 적분한다. 광학 깊이로 태양 투과율을 구하고
직접 단일 산란을 누적한다. LightCB와 Noon/Low Sun 프리셋, 표본 수 진단을 추가한다.

## 단계 7 — Dual-lobe Phase Function

전방·후방 Henyey-Greenstein lobe를 혼합한다. 방향 부호를 카메라→표본과 표본→태양 기준으로 고정하고,
Phase Off가 단계 6 결과와 같도록 배율 1을 중립값으로 쓴다.

## 단계 8 — 환경광과 다중 산란 근사

분석적 하늘·지면 색과 밀도 AO로 저비용 환경광을 만든다. 기존 Light Ray 광학 깊이를 여러 octave로
재사용해 추가 레이 없이 다중 산란을 근사한다. 단계 8 승인 태그와 실행본은 과거 화질 기준으로 보존한다.

# 2부 · 단계 13 선행 구현

## 단계 13 — 대규모 평면 구름층

### 목표

유한 AABB를 카메라 기준 대규모 평면 구름층으로 교체하고 km 규모 Weather/Noise와 유한 추적 거리를
도입한다. “카메라 기준”은 구름 텍스처나 XZ 박스가 따라 움직인다는 뜻이 아니다. 밀도장은 월드 XZ에
고정하고 각 카메라에서 일정 반경까지만 추적한다.

### 기준 프리셋

| 항목 | 기본값 |
|---|---:|
| 구름 바닥 / 두께 / 상단 | 1,500m / 3,000m / 4,500m |
| Weather 반복 크기 | 32,000m (32km) |
| View 최대 거리 / fade 시작 | 50,000m / 40,000m |
| Light 최대 거리 | 20,000m |
| Noise Lab 미리보기 폭 | 32,000m |
| View step / 최대 횟수 | 100m / 256 |
| Light step / 최대 횟수 | 250m / 32 |
| Base / Detail 주파수 | 0.0015 / 0.012 cycle/m |
| extinction | 0.01/m |
| Base / Detail / Weather 바람 | 12 / 18 / 8m/s |

### 평면층 교차

```text
bottom = cloudBottomAltitude
top = bottom + cloudLayerThickness
t0 = (bottom - rayOrigin.y) / rayDirection.y
t1 = (top    - rayOrigin.y) / rayDirection.y
tStart = max(0, min(t0, t1))
tEnd = min(max(t0, t1), maxTraceDistance, opaqueSceneDepth)
```

아래나 위에서 층을 향하는 레이는 두 교차점의 정렬된 구간을 쓴다. 내부 카메라는 `tStart=0`이다.
층 밖 수평 레이는 miss, 층 안 수평 레이는 최대 거리까지 진행한다. 퇴화 두께, 비정상 방향, NaN은
중립 결과로 끝낸다.

하늘 픽셀의 끝 거리는 투영 far plane과 분리한다. 불투명 물체가 있는 픽셀에만 복원 Scene Depth를
적용한다. View Ray는 평면 이탈·Scene Depth·50km 중 가장 가까운 지점, Light Ray는 평면 이탈·20km
중 가까운 지점에서 끝난다.

### 원거리 fade와 월드 고정 밀도

```text
viewFade = 1 - smoothstep(40000, 50000, distance)
viewDensity *= viewFade
weatherUV = positiveWrap(worldXZ / 32000)
```

40~50km fade는 최대 거리의 원형 절단면을 숨긴다. Weather와 Base/Detail Noise는 카메라 좌표를
더하지 않고 월드 위치로 샘플하므로 장거리 이동에도 같은 위치의 밀도가 유지된다.

### 공유 인터페이스

128바이트 CloudCB의 AABB 여섯 float를 다음 여섯 값으로 교체한다.

- `cloudBottomAltitude`, `cloudLayerThickness`
- `maxViewTraceDistance`, `maxLightTraceDistance`
- `viewTraceFadeStartDistance`, `noiseLabPreviewWorldSize`

C++ 구조체, HLSL cbuffer와 아키텍처 표는 항상 함께 변경한다. Noise Lab export schema는 13이며 도메인,
추적 거리, km 규모 파라미터와 카메라 중심 XZ를 기록한다.

### 입력과 디버그

- Q/W/E: 기준층·얇은층·두꺼운층 프리셋
- F5/F6/F7/F8: 지상 위보기·지상 수평선·구름 내부·구름 위 카메라
- 기존 디버그 번호 유지, AABB Entry/Exit 이름은 Cloud Layer Entry/Exit로 변경
- Noise Lab은 카메라 XZ 중심의 32km 단면 표시

### 자동 검증

`Stage13CloudLayerMath`는 아래·내부·위 카메라, 수평 평행 레이, Scene Depth, 최대 거리, fade,
높이 비율, 퇴화·NaN을 검증한다. Weather UV는 양·음수 좌표와 32km 경계의 연속성, 카메라 이동 후
동일 월드 위치 불변성을 검증한다. Debug/Release, 전체 CTest, Legacy/Optimized Od/O3 런타임 컴파일,
D3D11 오류 부재와 schema 13 export를 확인한다.

### 사용자 승인 장면

| 장면 | 합격 화면 |
|---|---|
| 지상에서 위 | 1.5~4.5km 층과 단계 8 조명 유지 |
| 지상 수평선 | AABB 옆면과 50km 절단면이 보이지 않음 |
| 구름 내부 | `tStart=0`, NaN·검정·갑작스러운 팝 없음 |
| 구름 위에서 아래 | 층 전체와 Scene Depth 폐색 정상 |
| 장거리 이동 | Weather/Noise 미끄러짐과 주기 경계 튐 없음 |

사용자 승인 전에는 단계 13을 완료 처리하거나 단계 9를 재개하지 않는다.

# 3부 · 평면층 기반 후속 단계

## 단계 9 — 레이마칭 기본 최적화 재개

보존한 Support Precheck, Base 재사용, Early Exit와 Search/Full coarse 탐색을 승인된 평면층에 맞춰
재조정한다. 단계 13 Optimization Off를 기준으로 다음 장면을 측정한다.

| 장면 | 목적 |
|---|---|
| GroundZenithDense | 지상 위보기 조밀 구름 |
| GroundHorizonDense | 긴 구간의 주 성능 게이트 |
| SparseHorizon | 빈 공간 탐색과 회귀 |
| DepthOccluded | Scene Depth 제한 |
| InsideLayer | 내부 카메라 `tStart=0` |

완료 조건은 GroundHorizonDense GPU Cloud p95 15% 이상 개선, 나머지 장면 3% 초과 회귀 없음,
Optimization Off 대비 SSIM 0.99 이상·정규화 RMSE 0.01 이하와 사용자 화질 승인이다.

## 단계 10 — 저해상도 렌더와 업샘플링

구름을 저해상도 렌더 타깃에서 계산하고 깊이·투과율을 고려해 업샘플링한다. 얇은 가장자리, 불투명
물체 경계와 수평선 누출을 검증하며 단계 9 결과와 비용을 분리해 측정한다.

## 단계 11 — Jitter와 Temporal Reprojection

프레임별 표본 위치를 jitter하고 이전 프레임 구름을 재투영한다. 카메라 행렬, 깊이와 history validity를
사용해 ghosting을 거부한다. 정지 화면 수렴과 빠른 카메라 이동의 잔상을 함께 검사한다.

## 단계 12 — Cloud Shadow Map과 Light Cache

View 표본마다 반복하는 태양 적분 일부를 재사용한다. 넓은 지면 그림자와 구름 자기 그림자의 안정성,
갱신 주기, 카메라 이동 독립성을 검증한다. 단계 13에서는 이 캐시를 미리 구현하지 않는다.

## 단계 14 — 대기와 지면 조명 통합

승인된 평면 구름층을 하늘·태양·지면 색과 연결한다. 대기 산란 모델 또는 Cube Map 입력은 이 단계에서
선택하며 행성 곡률이나 우주 전환을 요구하지 않는다.

## 단계 15 — 최종 품질과 성능 조정

품질 프리셋, 기본 카메라, 조작법, 캡처 장면과 포트폴리오 설명을 확정한다. Debug/Release 회귀,
GPU p95, 화질 비교, 장시간 이동 안정성과 사용자 최종 승인을 기록한다.

# 부록 · 단계 경계와 승인 원칙

- 각 단계는 자동 수치·빌드 검증과 사용자 렌더 검증을 분리한다.
- 에이전트는 실제 실행한 자동 검증만 완료 표시하고 화면 미적 품질을 대신 승인하지 않는다.
- 코드 변경 시 C++ 구조체, HLSL cbuffer, 아키텍처 표와 관련 단계 문서를 같은 변경에서 맞춘다.
- 단계 13은 16.67ms 성능 게이트가 아니라 새 기준선 수립이 목적이다.
- 모든 단계 9~15 작업은 사용자 승인된 단계 13 평면층을 기준 도메인으로 사용한다.
