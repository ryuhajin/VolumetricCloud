# Stage 11 Jitter·Temporal Reprojection

## 구현

- 50% Cloud Data에 저해상도 texel `±0.25`의 2×2 4-phase UV/Ray/Scene Depth jitter를 추가했다.
- Full-resolution `RGBA16_FLOAT scattering/T + RG16_FLOAT cloud depth/scene limit` history 두 벌을
  만들고 성공한 resolve 뒤에만 ping-pong index를 바꾼다.
- 대표 Cloud Depth와 이전 View-Projection, Physical Wind `-velocity×dt`로 history UV를 구한다.
- 화면 경계/clip.w/motion, Scene class/depth, Cloud depth, T, near-cloud 조건으로 history를 거부한다.
- 유효 history를 현재 3×3 scattering/T 범위로 clip한 뒤 EMA 혼합한다.
- F1 Temporal UI와 Jitter/Motion/History Validity/Weight/Current-History Difference/Current Source
  Validity 디버그 ID 68~73을 추가했다.
- resize, Stage 10 설정, Temporal toggle, F5~F8, 파라미터/preset, time jump, shader reload와 resource
  재생성에서 history를 초기화한다.
- 전체 snapshot을 schema 33으로 올리고 temporal 설정을 기록한다. 승인 뒤에도 비교 기준 기본값은 Off다.

## 2026-08-21~22 사용자 검증 수정

- Stable 4-Phase에서 건물 경계가 1픽셀 왕복했다. jittered low-res 표본을 Full 공간에서 복원할 때
  `sourcePosition = fullUv*size - 0.5 - jitter`로 생성 offset을 역보정했다. 하지만 세 공간 필터의
  재검증에서 떨림이 남아 이 수정만으로는 충분하지 않음을 확인했다.
- 실제 추가 원인은 건물 바로 옆 jittered source가 geometry/sky class 또는 geometry depth가 다른데도
  current 복원에 참여한 것이었다. source finite/range, class와 `clamp(target×1%,1m,10m)` depth를
  hard validation하고 Nearest/Bilinear/Joint 공통 3×3 유효 후보 fallback을 추가했다.
- 유효 current가 없으면 Full Scene surface/far anchor로 history를 재투영하고 Scene 검사를 통과한
  history를 100% 유지한다. history도 없으면 투명 구름으로 시작한다.
- Motion은 중립 회색 기준 signed RG와 화면 밖 파랑, Validity는 초록 허용과 reason별 고정색,
  Difference는 clamp 전 raw scattering/T를 R/G로 분리하고 F1에 범례를 표시한다.
- 테스트 전용 Scene Depth readback으로 F5 건물과 인접 하늘의 1픽셀 silhouette를 만들고,
  Current Source Validity ID 73의 초록/빨강/노랑/파랑/회색 계약을 검사한다. Weight 고정성은 실제
  증상 대상인 opaque 건물 측에서 재고, 인접 sky의 정상 Cloud Depth/T disocclusion은 Composite로 판정한다.

## 2026-08-22 F8 사선 평면 빗금 수정

- 사용자가 F8에서 카메라를 조금 돌리면 구름이 겹친 10km 지면에 수평 빗금이 생기고, 50%를
  유지한 Temporal Off와 세 필터 모두에서 남는 현상을 확인했다. Diff 파랑은 invalid current,
  Validity 노랑과 Weight 검정은 대표 Cloud Depth hard rejection이 scanline으로 반복됨을 보였다.
- 원인은 `clamp(target×1%,1m,10m)`가 넓은 사선 평면의 정상적인 perspective ray 거리 변화까지
  geometry 경계로 오판한 것이다. 이 meter 식은 가까운 source의 fast path로만 남기고, 10m를
  넘는 후보는 Full D32 Center/L/R/U/D의 작은 one-sided slope로 source device depth를 예측한다.
  오차 `8e-7 + 2e-7×ManhattanPixelDistance` 안이면 같은 평면으로 인정한다.
- history Cloud Depth는 중심 current 한 값과 비교하지 않고 `T<0.99`인 current Full 3×3 대표 깊이
  범위와 상대 margin 밖일 때만 거부한다. 같은 3×3 scattering/T 범위를 clipping에도 재사용한다.
- F8 기본·yaw ±3°·pitch ±2° 평면 mask와 Temporal Off hole 검사를 smoke에 추가했다. GPU 검증은
  Stratus, 1920×1080 Scene/960×540 Cloud Data와 Nearest/Bilinear/Joint4로 고정한다.
- Release smoke에서 F8 Composite range 평균/P99 `0.000012/0.000250`, source non-green·invalid run·
  Weight P99·Diff blue·Cloud Depth yellow run·Off hole run 모두 0이었다. 600표본 Resolve/Total p95는
  F5 `1.255424/3.897344ms`, F8 `1.314816/2.793472ms`로 두 성능 gate를 통과했다.

## 검증

- `Stage11TemporalMathTests`: jitter source의 Full guide pixel, D32 affine plane/slope/depth step,
  finite/class/slope 부족, neighborhood Cloud Depth margin·투명 생략, Bilinear 재정규화와 결정적
  최근접 tie, Joint texel 중심, invalid-current 유지/투명 정책까지 검사한다.
- `--stage11-temporal-smoke-test`: Stratus/F5, 1920×1080 Scene, 960×540 Cloud Data에서
  Nearest/Bilinear/Joint4의 독립 4-phase Composite·Current/History Validity·Weight·Diff를 검사한다.
- 2026-08-22 최종 Release smoke: F5 건물+인접 하늘 Composite RGB range 최악 평균
  `0.000301`, P99 `0.003803`, opaque 건물 Weight P99 `0.000000`; F8 사선 평면 지표와
  F5/F8 성능 수치는 위 수정 절의 값을 사용하며 resize/toggle reset과 finite/debug 계약도 통과했다.
- 2026-08-22 F8 수정 뒤 전체 Debug·Release 빌드와 Release CTest `45/45` 재통과.

## 2026-08-23 사용자 승인

- 사용자는 F8 Stratus/50%/Stable 4-Phase에서 Nearest, Bilinear, Joint4를 각각 회전해도 사선
  평면에 빗금이 생기지 않고 Current Source Validity가 초록인 것을 확인했다.
- F5 건물 경계의 1픽셀 왕복도 사라졌으며, Weight debug의 검정 픽셀은 구름 변화 경계에 국소적인
  점·짧은 선으로만 나타나고 평면을 가로지르는 연속 scanline이 아님을 확인했다.
- 사용자가 Stage 11 안정성 우선 기준을 최종 승인했다. Temporal 런타임 기본값은 계속 Off이며
  4×4 interleaved/ray-start jitter는 승인 기준에 포함하지 않는다.
- Stage 10 커밋 계보와 Stage 11 구현·수정·자동 검증을 함께 `stage11` 태그로 고정하고 다음
  개발 목표를 Stage 12 Cloud Shadow Map/Light Cache로 전환한다.
- 승인 기록 직전 Release 빌드와 `Stage11TemporalMath`/`Stage11TemporalSmoke`를 다시 실행해
  `2/2` 통과했다. 이전 전체 Release CTest `45/45` 기록도 함께 보존한다.

## Stage 15로 이관한 정량화

- 계획했던 1080p Full reference 대비 4/8/16프레임 SSIM/RMSE/T 오차는 승인 시점에 실행한
  것으로 소급 기록하지 않는다. Stage 15의 일곱 장면 최종 화질·성능 측정에서 수행한다.
