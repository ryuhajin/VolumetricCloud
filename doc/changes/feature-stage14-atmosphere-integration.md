# feature/stage14-atmosphere-integration

## 목적

Rayleigh·Mie·오존 기반 대기 LUT를 추가하고 하늘, 태양, 구름 직접/환경광, 지면·건물,
대기 원근과 HDR 톤 매핑이 같은 태양·대기 결과를 공유하도록 통합한다.

## 진입 기준

- `stage12-approved`가 가리키는 merge commit `b8d3504`의 최신 `main`에서 분기했다.
- Stage 12 DeepCache/Balanced512, Full/50%/Temporal 화면 계약을 기준선으로 유지한다.

## 구현 계약

- 지표/대기 상단 `6360/6460km`, Rayleigh/Mie 높이 `8/1.2km`, 오존 `25±15km`를 Earth Clear 기준으로 사용한다.
- Transmittance 256×64, Multi Scattering 32×32, Sky View 192×108, Sky Irradiance 64×16,
  Aerial Radiance/T 32³를 `RGBA16_FLOAT` compute LUT로 만든다.
- Stage 14 전용 바인딩은 `Stage14CB(b13)`, LUT `t8~t13`, sampler `s3`이며 기존 b0~b12/t0~t7/s0~s2를 유지한다.
- 대기 실패는 마지막 정상 LUT를 보존하고 최초 실패는 Manual Reference로 폴백한다.
- Ground Material은 Concrete/Grass/Snow/Desert/Custom이며 건물은 0.18 linear concrete로 유지한다.
- Full/Spatial/Temporal은 같은 대기 원근 합성 함수를 사용하고 최종 HDR target 뒤에서 ACES를 적용한다.
- snapshot은 schema 35이며 schema 34 이하는 Manual Reference/Legacy Shoulder로 복원한다.
- F5~F8 position/target 구도는 유지하고 이후 조작만 FPS 자유 시점으로 바꾼다. 좌클릭 회전은
  position을 고정하고, WASD와 휠은 view forward/right로 이동한다. 휠 한 notch는 F4 이동속도의
  0.25초분이며 Shift는 4배다. 지면 충돌과 고도 제한은 두지 않는다.

## 시작 UI

- F3 최상단에 기본 접힘 `Atmosphere Debug`, 독립 기본 펼침 `Tone Mapping`, 그 아래 기본 펼침
  `Lighting & Atmosphere`를 둔다. Tone Mapping에는 history 상태를 읽기 전용으로 함께 표시한다.
  Angle/방향 도식과 Time of Day를 동시에 보여 주고,
  Ground Reflection·Cloud Ambient Visibility·Cloud Multiple Scattering·Manual Reference Colors를
  한 환경광 분류 안에서 역할별로 나눈다. 기존 독립 Directional/Environment 패널은 제거했다.
- Time of Day는 `30~120 simulated min/s`이며 재생 중에만 태양을 제어한다. Pause는 현재
  시간 방향을 Angle에 인계하고, 재생은 항상 19:30에서 05:30으로 순환한다.
- Physical Earth Clear, Concrete, Angles, azimuth -60°/elevation 18°, tint white/multiplier 1,
  Exposure 0EV/6500K/ACES On으로 시작한다.

## 제외 범위

- 달·별·깊은 밤, 실제 천문 태양 위치, 대규모 지형/PBR, 로컬 지면 GI, 분광 렌더링,
  자동 노출, LUT 품질 프리셋, Weather 습도와 Mie 자동 연결.

## 검증 상태

- CPU 기준과 `Stage14AtmosphereMath` 테스트를 추가했다. 구면 교차, 밀도 profile,
  Rayleigh/Mie phase, Beer–Lambert, LUT UV 왕복, 시간 경로, 지면 preset,
  ACES/white balance를 검사한다.
- 여섯 compute LUT, hash 기반 선택 갱신, 마지막 정상 세트 원자 보존을 구현했다.
- normal/material 장면, 대기 태양과 Sky/Ground 구름 조명, 공통 Aerial 합성,
  HDR composite와 ACES 최종 pass를 연결했다.
- F3 독립 Atmosphere Debug와 통합 대기·태양·시간·지면·환경광·Tone UI, 실제 SRV
  thumbnail/3D slice와 schema 35 snapshot을 연결했다. schema의 loop 필드는 보존하지만
  항상 true로 정규화한다.
- 사용자 검증 피드백에 따라 Tone Mapping을 F3 최상위 독립 섹션으로 옮기고 Exposure/White
  Balance가 history를 지우지 않는지 확인할 수 있는 읽기 전용 Temporal 상태를 함께 표시했다.
  Stage 13-5 Detail LOD 버튼은 현행 승인 기본 `Approved 32–48km`와 더 공격적인 개발 후보
  `Aggressive 24–40km`로 명칭을 명확히 했다.
- 위 UI·가이드 수정 뒤 Debug/Release 빌드와 Release 전체 CTest를 다시 실행해 49/49 통과했다.
  `NoiseLabSmoke`, `Stage13OpticsLightingMath/Smoke`, `Stage11TemporalMath/Smoke`,
  `Stage14AtmosphereMath/Smoke`를 포함해 레이블·읽기 전용 표시가 기존 렌더 계약을 바꾸지 않음을
  확인했다.
- `Stage14AtmosphereSmoke`에서 최초 generation, 정적 생략, Tone 불변, Ground/Sun/Camera
  선택 무효화와 D3D11 debug layer를 확인해 통과했다. 여섯 LUT는 모두 finite/non-negative이며,
  Transmittance 전체 RGB readback은 CPU 40-step 기준 대비 `MAE=0.000189`, `P99=0.000485`다.
- Debug/Release 빌드와 전체 49개 CTest를 통과했다. 과거 단계 smoke는 schema 34 호환과
  같은 Manual Reference/Legacy Shoulder로 실행하며, offscreen 진단도 일반 프레임과 같은
  b13 준비 순서를 사용한다. shader hot reload와 최신 Debug D3D11 layer도 통과했다.
- 2026-08-27 F3 통합 개편 뒤 Debug/Release와 전체 49개 CTest를 다시 통과했다. 첫 렌더부터
  Manual Reference인 구형 자동 회귀는 기존 Light 방향을 최초 Angle로 승격해 승인 화면을
  보존하고, 일반 Physical/Manual 전환은 Stage 14 Angle 하나를 계속 공유한다.
- F5/F6의 먼 target을 중심으로 orbit할 때 위쪽 드래그가 카메라를 지하로 이동시켜 지면 뒷면을
  검은 하늘처럼 보이게 하던 문제를 재현했다. Camera를 position+yaw/pitch FPS 상태로 바꾸고
  SetLookAt/SetOrbit과 schema 35 position/target 저장 계약은 호환 레이어로 유지했다.
- 2026-08-27 FPS 전환 뒤 Debug/Release 빌드와 Release 전체 49개 CTest를 다시 통과했다.
  실제 F5/F6에서 pitch 89.43°까지 올려도 position Y는 각각 15m/2m로 유지되고 하늘·구름이
  표시됐다. F6 천정 시선에서 기본 1000m/s 휠 한 칸은 forward로 약 250m 이동했고 역방향
  한 칸으로 원위치했으며 FOV 60°와 reference distance는 변하지 않았다. F4 UI 위 휠은
  ImGui가 소비해 카메라 위치를 바꾸지 않았다.
- 2026-08-26 Release 초기 화면에서 Physical 하늘 그라데이션과 공기색이 섞인 구름을
  확인했으며 전체 검정, NaN 경고색과 뚜렷한 Aerial slice 띠는 관찰되지 않았다.
- `--stage14-performance-test`를 추가해 같은 실행의 Stage 12 호환/Stage 14 물리 경로를
  1920×1080 일곱 장면에서 각각 120 warmup+600 timestamp로 측정한다. `GPU Cloud Total`이
  Shadow+Raymarch+Resolve를 모두 포함하도록 Stage 14 profiler와 CPU 회귀도 수정했다.
- 구름 표본마다 반복하던 물리 대기 LUT 조회를 View ray당 대표 고도
  `CloudLightingContext` 한 번으로 줄였다. 최종 물리 경로 GPU Cloud p95 평균은
  `7.37616ms`, 같은 실행 Stage 12 호환 `7.07040ms` 대비 `+4.325%`다. 장면별 Cloud/Frame
  p95와 Sun/Camera Atmosphere 갱신 p95, D3D11 debug layer gate를 모두 통과했다.
- 남음: 사용자 화면 검증 및 승인.
