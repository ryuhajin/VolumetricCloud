# feature/stage10-low-resolution-upsampling

## 목표

단계 9 Balanced의 레이 하나당 비용은 유지하면서 구름을 계산하는 화면 픽셀 수를 줄이고,
현재 프레임의 Scene/Cloud Depth와 Transmittance로 Full-resolution 공간 복원한다.

## 구현 상태 — 2026-08-19

- 구름 패스를 RGBA16F `scattering.rgb + T`와 RG32F `opacity-weighted cloud depth +
  source scene limit` MRT 출력으로 분리했다.
- Full-resolution `CloudUpsample.hlsl`에 Nearest, Bilinear, Joint4, Joint9를 추가했다.
- F1에 50/67/75/Full 해상도와 비용순 필터·동일 비용 임계값 버튼을 추가했다.
- 32바이트 `Stage10UpsamplingParameters/UpsamplingCB(b10)`와 snapshot schema 32를 추가했다.
- GPU timestamp를 Cloud Raymarch, Upsample/Composite, GPU Cloud Total로 분리했다.
- Low-resolution Grid, Scene Rejection, Cloud Depth Weight, Transmittance Weight debug ID
  64~67을 추가했으며 숫자 0~9 매핑은 유지했다.
- 시작 해상도는 사용자 승인 전까지 Full이다. jitter/history/reprojection은 단계 11로 남겼다.
- 2026-08-19 사용자 비교에서 50%가 67/75%보다 격자감이 적고 세 필터의 가시적 차이가 작았다.
  활성 F1/자동 후보를 50/Full과 Nearest/Bilinear/Joint4로 줄이고 `50% + Nearest`를 잠정 최종
  후보로 정했다. 제외한 enum과 Joint9 셰이더 경로는 schema 32 호환용으로 보존한다.

## 자동 검증

- `Stage10UpsamplingMath`: 타깃 크기, 최소 1픽셀, 픽셀 중심 UV, opacity 가중 깊이,
  Scene/Cloud/T 가중치와 finite fallback.
- `Stage10UpsamplingSmoke`: Full 직접 합성 대비 split RGB MAE `0.000093`, 최초 네 해상도 ×
  네 필터와 네 debug 출력 finite, 후보 hash 13개, Half `48×27`, D3D11 오류 없음. 사용자 축소 뒤
  활성 2개 해상도 × 3개 필터에서 hash 4개와 같은 Full MAE·Half 크기를 다시 통과했다.
- Debug/Release 빌드, 새 셰이더 포함 hot reload와 전체 CTest 43개를 통과했다. 첫 전체 실행에서
  schema 32인데 `implementationStage=9`를 기대한 구형 테스트 두 곳을 발견해 10으로 동기화했고,
  두 테스트 재실행까지 통과했다.

## 승인 전 남은 게이트

- 1920×1080 Full 대비 SSIM/RMSE/T 화질 측정.
- 일곱 장면의 120-frame warmup + 원시 timestamp 600개 p95 측정.
- F5~F8 및 Dense/Stratus/Cumulus에서 건물 번짐, 얇은 구름 소실, 격자와 이동 중 떨림을
  사용자가 검사한다.
- 자동·사용자 검증을 모두 통과한 가장 싼 조합만 기본값으로 승격한다.
