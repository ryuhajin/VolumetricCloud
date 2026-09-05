# feature/rebuild-foundation 변경 기록

## 1. 목표와 배경

- 복잡하게 결합된 기존 구름 구현을 보존 브랜치에 격리하고, 단계별 계획의 단계 0부터 다시 구축한다.
- 단계 0의 월드 레이, Scene Depth, 월드 위치와 별도 합성 패스는 사용자 승인을 받았다.
- 단계 1은 상수 밀도 AABB의 교차·폐색·Beer-Lambert 적분을 독립 검증한다.

## 2. 기존 구현과 관찰된 문제

- 기준 `main`은 풀스크린 픽셀 셰이더에서 AABB 교차, 상수 밀도와 Beer-Lambert 적분을 한 번에 수행한다.
- 실제 불투명 장면과 깊이 버퍼가 없어 볼륨이 장면 물체에 가려지는지 검증할 수 없다.

## 3. 원인 분석과 근거

- 장면 색상, Scene Depth와 구름 결과가 분리되지 않아 이후 단계의 교차·밀도 오류를 독립적으로 관찰할 기반이 없다.
- 깊이 역투영이 검증되지 않은 상태에서 레이 마칭을 확장하면 폐색 오류의 원인을 구분하기 어렵다.

## 4. 검토한 대안

- 하늘의 far depth만 사용하는 안은 실제 깊이 복원을 검증할 수 없어 제외했다.
- 단일 도형만 사용하는 안보다 서로 다른 깊이의 평면과 박스가 폐색·복원 오류를 드러내기 쉬워 진단 장면을 선택했다.

## 5. 선택한 해결 방법

- 불투명 평면과 박스를 HDR 색상 타깃과 샘플 가능한 32비트 깊이 타깃에 먼저 렌더링한다.
- 두 타깃을 풀스크린 단계 0 패스에서 읽어 레이 방향, 선형 거리와 월드 위치를 복원한다.
- 최종 모드는 임시 반투명 원판을 `scattering + background * transmittance`로 합성해 별도 경로를 눈으로 검증한다.

## 6. 실제 수정 내용

- `Renderer`를 불투명 진단 장면 패스와 단계 0 풀스크린 패스로 분리했다.
- `R16G16B16A16_FLOAT` 장면 색상과 `D32_FLOAT`/`R32_FLOAT` 공유 깊이 리소스를 추가했다.
- `CloudParameters` 48바이트 구조와 `CloudResult` HLSL 인터페이스를 추가했다.
- 숫자 0~4 디버그 출력과 F5~F7 고정 카메라 프리셋을 추가했다.
- CPU 역투영 회귀 테스트와 숨김 창 D3D11 smoke test를 추가했다.
- 단계 1에서 평행축을 나누지 않는 slab 교차와 Scene Depth 제한을 추가했다.
- 전체 교차 구간을 다시 나눈 상수 밀도 레이 마칭과 고정 산란색을 추가했다.
- 숫자 5~9, F8, Q/W/E/R/T 검증 입력과 현재 상태를 보여 주는 창 제목을 추가했다.
- `Stage1VolumeMath`와 모드 0~9 `Stage1Smoke` 회귀 검사를 추가했다.
- 단계 2에서 월드 위치 기반 단일 절차적 3D value noise와 부드러운 8-corner 보간을 추가했다.
- coverage threshold, density multiplier, wind direction/speed와 noise offset을 80바이트 `CloudParameters`에 추가했다.
- Z/X/C/V noise 디버그와 N/A/S/D/F/G/H/K 비교 프리셋을 추가했다.
- `Stage2NoiseMath`와 단계 2 프리셋·디버그를 순회하는 `Stage2Smoke`를 추가했다.
- `CloudParameters.hlsli`와 `Noise.hlsli`로 GPU 설정·noise 함수를 분리해 구름과 Noise Lab이 같은 구현을 사용한다.
- Dear ImGui Win32/DX11 UI에서 XY/XZ/YZ 단면, crosshair, raw/threshold/final과 공유 파라미터·시간을 조절한다.
- 512×512 단면 PNG 세 장과 파라미터·noise source hash JSON을 WIC로 내보낸다. PNG는 런타임 밀도 입력으로 읽지 않는다.
- 모든 HLSL/HLSLI를 재귀 감시하고 전체 셰이더가 성공한 경우에만 generation 단위로 교체하는 원자적 핫리로드를 추가했다.
- 단계 3에서 AABB 월드 Y를 0~1 높이로 바꾸고 독립적인 하단·상단 smoothstep을 곱한다.
- `CloudParameters`를 96바이트로 확장하고 `bottomFadeEnd=0.20`, `topFadeStart=0.80`을 추가했다.
- B/M 대표 샘플 디버그와 Noise Lab Height Fraction/Profile 단면·64표본 곡선을 추가했다.
- Noise Lab JSON schema 2에 높이 경계를 기록하고 겹친 fade는 허용하되 UI 경고를 표시한다.
- 실행 기본 AABB를 X/Z `±8m`로 넓히고 `Y` 넓은 볼륨 프리셋을 추가했다. `Q`의
  X/Z `±2m` 볼륨은 단계 1 수치 검증 기준으로 보존한다.
- 단계 4에서 Base Shape 평가, 교체 가능한 Detail Noise 샘플과 subtractive erosion을 분리했다.
- `CloudParameters`를 112바이트로 확장하고 Detail scale/strength/speed/offset을 추가했다.
- Base가 0, Detail Off 또는 `sampleDetail=false`이면 Detail 함수를 호출하지 않는다.
- J/L/P/U, F9~F12와 Noise Lab의 Base/Detail/Erosion/Sample Mask 출력을 추가했다.
- Noise Lab JSON schema 3이 네 Detail 파라미터와 현재 Detail 프리셋을 기록한다.

## 7. 캐시·호환성·성능 영향

- 기존 noise cache와 프리셋을 읽지 않는다.
- 단계 0은 full-resolution 장면 색상과 32비트 깊이 타깃을 각각 하나 사용한다.
- Noise Lab은 `R8G8B8A8_UNORM` 512² render target/SRV/staging texture를 축별로 한 벌씩 사용하며 F1으로 UI를 숨길 수 있다.
- 단계 3은 새 texture를 만들지 않고 기존 CloudCB를 16바이트 늘리며 기존 Noise Lab 타깃을 재사용한다.
- 단계 4도 새 texture 없이 CloudCB 16바이트와 Base가 존재하는 표본당 Value Noise 1회만 추가한다.

## 8. 테스트 및 실행 결과

- Debug 빌드: 성공
- Release 빌드: 성공
- Release ctest: 2/2 성공 (`FoundationMath`, `FoundationSmoke`)
- Debug D3D11 smoke: 성공, error/corruption 메시지 없음
- HLSL 컴파일: Fullscreen VS, Foundation PS, Diagnostic Scene VS/PS 모두 성공
- 사용자 렌더 승인: 2026-08-02 수동 검증 체크리스트 전체 통과
- 단계 1 Debug/Release 빌드: 성공
- Release CTest: 4/4 성공 (`FoundationMath`, `FoundationSmoke`, `Stage1VolumeMath`, `Stage1Smoke`)
- 단계 1 HLSL: Fullscreen VS, Cloud PS, Diagnostic Scene VS/PS `fxc` 경고 없이 성공
- Debug D3D11: Foundation/Stage1 smoke 반환 코드 0, error/corruption 없음
- 단계 1 사용자 렌더 승인: 2026-08-02 수동 검증 전체 통과
- 단계 2 Debug/Release 빌드: 성공
- Noise Lab 통합 후 Debug/Release CTest: 8/8 성공, 이전 단계 회귀와 `NoiseLabSmoke`/`ShaderHotReloadSmoke` 포함
- 단계 2 HLSL: Fullscreen VS, Cloud PS, Diagnostic VS/PS `fxc /Od` 경고 없이 성공
- Debug D3D11: Foundation/Stage1/Stage2 smoke 반환 코드 0, error/corruption 없음
- NoiseLabSmoke: raw/threshold/final 세 출력의 XY/XZ/YZ GPU readback과 512² PNG 3장·JSON 생성 통과
- ShaderHotReloadSmoke: 임시 `Noise.hlsli` 변경 시 Lab/Cloud 동시 변화, 문법 오류 시 이전 generation 유지, 복구 후 원상 복귀 통과
- 단계 2 사용자 렌더 승인: 2026-08-03 수동 검증 전체 통과
- 단계 3 Debug/Release 빌드 및 양 구성 CTest: 각각 10/10 성공, `Stage3HeightMath`와 `Stage3Smoke` 포함
- 단계 3 HLSL: Fullscreen/Cloud/Noise Lab/Diagnostic VS·PS 5개 엔트리 포인트 `fxc /Od` 경고 없이 성공
- 단계 3 D3D11: 기존 회귀와 B/M·fade 조합 smoke에서 error/corruption 없음
- 넓은 볼륨 추가 후 Debug/Release 빌드와 양 구성 CTest 10/10 통과. 확장된
  Stage1Smoke가 `Q/Y/W/E/R/T` 여섯 프리셋의 모드 0~9 draw를 모두 통과했다.
- 단계 3 사용자 렌더 승인: 2026-08-03 높이 프로파일과 Q↔Y 넓은 볼륨 검증 전체 통과
- 단계 4 Debug/Release 빌드와 양 구성 CTest 각각 12/12 성공. 기존 Foundation,
  Stage1~3, NoiseLabSmoke와 ShaderHotReloadSmoke 회귀를 포함한다.
- `Stage4DetailMath`에서 독립 Base/Detail 좌표·속도, subtractive erosion,
  Detail Off·빈 Base·`sampleDetail=false` 샘플 생략과 finite 출력을 확인했다.
- `Stage4Smoke`에서 J/L/P/U × F9~F12 × Q/Y 조합을 렌더했고 D3D11
  error/corruption 없이 반환 코드 0을 확인했다.
- NoiseLabSmoke에서 9개 출력 readback과 PNG 3장, JSON schema 3의 Detail
  프리셋·네 파라미터 기록을 확인했다.
- 단계 4 HLSL은 Fullscreen/Cloud/Noise Lab/Diagnostic VS·PS 5개 엔트리 포인트를
  `fxc /Od /WX`로 경고 없이 컴파일했다.
- 사용자 검증에서 Noise Lab의 ImGui 키보드 캡처가 F9~F12를 막는
  문제를 확인했다. F5~F12 전역 검증 단축키를 UI보다 먼저 처리하도록
  수정했고, Windows가 F10을 메뉴 키로 전달하는 `WM_SYSKEYDOWN` 경로도 포함했다.
- 단계 4 사용자 렌더 승인: 2026-08-03 F9~F12 입력 재검증과 접이식 Noise Lab
  UI를 포함한 수동 체크리스트 전체 통과
- 단계 5에서 CPU 생성 256² RGBA8 Weather Map을 `t2`, linear-wrap sampler를
  `s1`에 연결하고 Uniform/Periodic Perlin/Channel Debug 프리셋을 추가했다.
- Weather R은 effective coverage, G는 층운·단계 3 혼합형·적운 높이 보간,
  B는 0.5~1.5 density modifier로 분리했다.
- `CloudParameters`를 128바이트로 확장하고 16m world size, 0.10m/s 독립 속도,
  float2 UV offset을 추가했다.
- I/O와 Shift+I/O Weather 디버그, F2~F4 프리셋, Noise Lab 15개 출력과
  실제 RGBA preview를 추가했다.
- Noise Lab JSON을 schema 5로 올리고 `weather-map.png`, 채널 정의·맵 해시,
  Weather transform과 CPU generator 설정을 기록한다.
- 단계 5 Debug clean/Release 빌드와 양 구성 CTest 각각 14/14 성공. 기존
  Foundation~Stage4, NoiseLab과 ShaderHotReload 회귀를 포함한다.
- `Stage5WeatherMath`에서 세 맵의 결정성·채널, 반복 UV·바람, coverage·density,
  Type 0.5 회귀·구간 연속성·finite와 Weather 빈 Base Detail skip을 확인했다.
- `Stage5Smoke`에서 I/O·Shift+I/O × F2~F4 × Q/Y × 이동 전후 렌더를 확인했고
  D3D11 error/corruption 없이 반환 코드 0이었다.
- NoiseLab 15개 출력 readback, PNG 4장과 JSON schema 5를 확인했고 실제
  `weather-map.png`의 RGBA 채널 순서도 검증했다.
- HLSL Fullscreen/Cloud/Noise Lab/Diagnostic VS·PS 5개 엔트리 포인트를
  `fxc /Od /WX`로 경고 없이 컴파일했다.
- 사용자 검증에서 반복 셀과 단조로운 배치가 확인되어 F3를 CPU 생성 2-scale
  Periodic Perlin으로 교체했다. R/G/B별 독립 seed와 macro/detail period를 사용하며
  modulo lattice와 quintic fade로 UV 경계의 값과 기울기를 잇는다.
- Weather texture/SRV는 `D3D11_USAGE_DEFAULT`로 한 번 만들고 Noise Lab 변경과
  F2~F4 전환은 `UpdateSubresource`만 수행한다. Live Update는 최대 10Hz이며
  UI에 Apply/Reset/Next Seeds를 제공한다.
- Weather R에 층운·혼합형·적운별 높이 cutoff를 적용해 혼합형과 적운의 중간
  footprint는 넓고 바닥·상단은 좁아지게 했다. F2/F4, 128바이트 CloudCB와
  기존 Weather 디버그 출력은 유지한다.

## 9. 남은 문제와 후속 개선

- 단계 0은 `c94825b`로 커밋되어 원격 `feature/rebuild-foundation`에 보존됐다.
- 단계 1은 자동 검증과 사용자 수동 렌더 검증을 모두 통과했다.
- 단계 2는 자동 검증과 사용자 수동 렌더 검증을 모두 통과했다.
- 단계 3은 자동 검증과 사용자 수동 렌더 검증을 모두 통과했다.
- 단계 4는 자동 검증과 사용자 수동 렌더 검증을 모두 통과했다.
- 단계 5는 2026-08-04 사용자 수동 체크리스트 전체와 최종 승인을 통과했다.
- 외부 Weather PNG/편집, precipitation, fBm/Worley와 early exit는 이후 단계로 남긴다.
- 단계 6에서는 승인된 Weather 밀도장을 입력으로 태양 방향 Light Ray와 단일 산란을 구현한다.
- 단계 6에서 CloudCB와 분리된 48바이트 LightCB(b3), Noon/Low East/Low West
  방향 프리셋과 Noise Lab 조명 조절·schema 6 export를 추가했다.
- View 표본에서 태양 방향 AABB 이탈까지 Base Density만 적분해 광학 깊이와
  Beer-Lambert 태양 투과율을 계산한다. Detail, Phase Function, 환경광은 제외한다.
- Shift+J/L/P/U에 태양 투과율·광학 깊이·Light Sample 비용·직접 산란 진단을 추가했다.
- 단계 6 Debug/Release 빌드, 양 구성 CTest 18/18, HLSL 5/5가 통과했다.
  Stage6Smoke는 조명 모드·태양·Weather·Q/Y pairwise와 8/16/32 Light Step,
  schema 6 export 및 D3D11 error/corruption 부재를 확인한다.
- 단계 6 보조 기능으로 `FrameProfiler`의 8-slot 비동기 D3D11 timestamp query ring과
  우측 상단 성능 오버레이를 추가했다. CPU Frame은 Present/VSync를 포함하고 GPU Frame은
  Present를 제외하며, GPU Cloud는 구름 패스만 측정한다.
- Noise Lab에 Performance/VSync 설정을 추가했다. 오버레이는 F1로 Noise Lab을 숨겨도
  유지되고, VSync Off는 `Present(0, 0)`을 사용한다.
- `FrameProfilerMath`와 `PerformanceOverlaySmoke`를 추가해 전체 CTest를 18개로 확장했다.
  동일 조건 비교 절차는 `doc/PERFORMANCE.md`에 고정했다.
- 2026-08-07 사용자 수동 검증에서 단계 6 조명, 디버그 출력, Light Step 비용 변화,
  F1 독립 성능 오버레이와 VSync On/Off를 모두 확인하고 단계 6을 승인 완료했다.
- 다음 작업은 승인된 등방성 단일 산란을 보존한 채 단계 7 Dual-lobe
  Henyey-Greenstein Phase Function을 추가하는 것이다.

## 10. 단계 7 Dual-lobe Phase Function 구현 및 승인 완료

- `LightParameters`와 HLSL `LightCB(b3)`를 64바이트로 확장하고 Phase Enable,
  전방/후방 `g`, lobe 혼합 비율과 적용 강도를 추가했다. 기본 프리셋은 Off라서
  승인된 단계 6 등방성 직접 산란을 그대로 보존한다.
- `PhaseFunction.hlsli`와 `Stage7PhaseMath.h`에 `1 / 4π`를 생략한 isotropic-relative
  Henyey-Greenstein 기준을 구현했다. 카메라→표본과 표본→태양이 같은 방향이면
  `cosTheta=+1`이며 전방 산란이 강해지는 부호 규칙을 CPU/HLSL에 동일하게 적용한다.
- 후방/전방 lobe를 독립 계산한 뒤 Blend하고, Phase Intensity로 등방성 1에서 결과로
  전환한다. 잘못된 방향과 비정상 입력은 1로 복귀한다. raw 진단은 0~16이며 13-5의
  LDR 합성 적용값은 별도 0~2.5 제한을 사용한다.
- Phase는 픽셀당 한 번만 평가하고 직접 산란량에만 곱한다. View/Light 투과율,
  광학 깊이와 Light Step 수는 변경하지 않는다.
- Noise Lab에 Off/Balanced/Silver Lining/Backscatter Check 프리셋, 파라미터 편집과
  128개 각도 표본 곡선을 추가했다. 태양 프리셋과 Phase 프리셋 상태는 독립적이다.
- Shift+B/M/C/V에 cosTheta, Forward/Backward HG, 최종 Phase Factor 진단을 추가하고
  export를 schema 7로 확장했다. 기존 15개 공간 단면과 PNG 4장은 유지한다.
- `Stage7PhaseMath`와 `Stage7Smoke`를 추가해 전체 테스트 목표를 양 구성 20/20으로
  확장했다. 2026-08-07 Debug/Release 빌드, 양 구성 CTest 20/20, HLSL 5/5
  `/Od /WX`와 D3D11 error/corruption 부재를 확인했다. 사용자 수동 렌더 승인을
  기다리며 단계 7 변경은 커밋하지 않는다.
- 2026-08-07 사용자 수동 검증에서 Phase Off 회귀, 전방·후방 lobe, 태양 방향,
  디버그 출력, Light Ray 불변성과 schema 7을 확인하고 단계 7을 승인 완료했다.
- 다음 작업은 외부 Cube Map 없이 분석적 하늘·지면 환경광과 기존 광학 깊이를
  재사용하는 저비용 다중 산란 근사를 추가하는 단계 8이다.

## 11. 단계 8 환경광과 다중 산란 구현 및 승인 완료

- 64바이트 `EnvironmentParameters`/`EnvironmentCB(b4)`를 추가하고 LightCB와 책임을 분리했다.
- 외부 Cube Map·간접광 텍스처 없이 높이 기반 Sky/Ground와 밀도 기반 AO를 계산한다.
- 기존 Light Ray 광학 깊이를 최대 네 octave로 재사용해 추가 레이 없는 다중 산란을 근사한다.
- Environment Off/Balanced/Strong Fill/Ground Check와 Custom UI, 높이 곡선을 추가했다.
- Ctrl+J 모드 32에서 레이 전체의 누적 직접광을 확인한다. 가시성이 낮았던
  Sky/Ground/Multiple/AO/전체 간접광 Ctrl 디버그 모드는 제거했다.
- Noise Lab export를 schema 8로 올리고 Environment 입력과 근사 모델을 기록한다.
- `Stage8AmbientMath`와 `Stage8Smoke`를 추가해 전체 테스트 목표를 22개로 확장했다.
- 2026-08-09 Debug/Release 빌드와 양 구성 CTest 22/22, HLSL 5/5,
  schema 8 export 및 D3D11 error/corruption 부재를 재확인했다.
- 2026-08-09 사용자 수동 검증에서 Off 회귀, Balanced 환경광, 높이별 Sky/Ground,
  AO, 1~4 multiple octave, Phase·Light Sample 불변성과 schema 8을 모두 확인하고
  단계 8을 승인 완료했다. 다음 작업은 단계 8 기준 실행본을 보존하고 레이마칭의
  불필요한 표본을 줄이는 단계 9 기본 최적화와 계측이다.

## 12. Stage 8 승인 기준 복원과 후속 재계획

- 2026-08-10 단계 9 최적화와 단계 13 평면 구름층 실험에서 발생한 문제를 분리하기 위해
  `stage8-approved`(`3549ddb`)를 새 기준 브랜치의 코드로 복원했다.
- 단계 9 시도는 `feature/rebuild-foundation`의 `7df6e88`, 단계 13 실험은
  `feature/large-planar-cloud-layer`의 `71cb634`와 `stage13-paused-20260810`에 보존했다.
- 별도 `build-stage8-replan` 폴더에서 Debug/Release 빌드, 양 구성 CTest 22/22,
  HLSL 5/5, Stage8Smoke와 D3D11 error/corruption 부재를 재확인했다.
- 단계 8의 2026-08-09 사용자 승인은 유지한다. 단계 9 이후의 순서·범위·성능 기준을
  새로 승인하기 전까지 렌더링 코드 변경을 중단한다.

## 13. 포트폴리오 계획 승인과 단계 13-0 단위 계약

- 실행 순서를 `13 → 9 → 10 → 11 → 12 → 14 → 15`로 확정하고 평면층 승인 뒤
  Earth-scale 구형 shell을 같은 밀도장으로 비교한다.
- `Stage13ScaleMath` CPU 기준을 추가해 길이 S배, cycle/m과 extinction 1/S배에서
  noise 좌표, Weather UV, 광학 깊이와 파장당 표본 수가 보존되는지 검사한다.
- 단계 13-0에서는 `src/`의 렌더러와 `shaders/`를 변경하지 않는다. 자동 검증과 사용자
  문서 승인이 끝난 뒤에만 단계 13-1 평면층 교차를 시작한다.

## 14. 단계 13-1 AABB/평면층 교차 분리

- 2026-08-10 사용자가 단계 13-0을 승인해 단위 계약을 잠갔다.
- 32바이트 `CloudDomainParameters`/`CloudDomainCB(b5)`를 추가하고 AABB Reference와
  Planar Layer가 View/Light의 같은 교차 인터페이스를 사용하게 했다.
- 평면층 시작값은 바닥 1,500m, 두께 3,000m, View 50km, 40~50km fade,
  Light 20km다. 수평 레이, 내부 시작, Scene Depth와 비정상 입력을 유한하게 처리한다.
- Noise Lab에서 도메인과 거리 값을 선택하고 Entry/Exit/Segment/Actual Step을 출력한다.
  F5~F8은 선택한 도메인의 지상 위보기·수평선·내부·위쪽 카메라다. 기존 meter급 박스로는
  Planar 폐색을 판정하기 어려워 주황 박스를 3m 층고 기준 `20×60×20m` 20층 건물로
  확대하고, `Shift+F5`에 건물 뒤 평면층을 함께 보는 DOMAIN-DEPTH 카메라를 추가했다.
- Noise Lab JSON을 schema 13으로 올려 도메인 종류와 meter 단위 범위를 기록한다.
- `Stage13CloudDomainMath`와 `Stage13DomainSmoke`를 추가해 전체 CTest를 25개로 확장했다.
  13-1에서는 교차만 분리하며 기존 density/noise/step/extinction의 km 재조정은 13-2 이후다.
- 최종 코드 기준 Debug/Release 빌드, 양 구성 CTest 25/25, HLSL 5개 엔트리의
  `/Od /WX`·`/O3 /WX`, D3D11 error/corruption 부재를 확인했다. 13-1 사용자 렌더
  승인 전에는 13-2를 시작하지 않는다.

## 15. 단계 13-2 기존 noise 단계적 상사 확대

- 2026-08-10 사용자가 13-1 교차·DOMAIN-DEPTH 화면을 승인해 13-2를 시작했다.
- Noise Lab에 순서 독립적인 1×/10×/100×/1000× 버튼과 파장·step·파장당 표본·대표
  광학 깊이 진단을 추가했다. 프리셋은 Uniform Weather와 Stage 8 형태 기준을 다시 적용한다.
- `Renderer::ApplyStage13SimilarityScale`은 AABB/평면층/추적 거리/View·Light step·bias·
  Weather 크기·세 바람 속도를 S배하고 Base/Detail 주파수와 extinction을 1/S배한다.
- `Ctrl+F5`는 원점의 진단 건물을 피한 `x=40×S` 위치를 같은 정규화 구도로 바라본다.
  1000×의 `-1~2km` 층과 16km Weather는 비교값이며 13-3 실제값이 아니다.
- `Stage13ScaleMath`는 도메인 거리·bias·바람 좌표 불변식까지 확장했고,
  `Stage13DomainSmoke`가 네 b1/b3/b5 런타임 프리셋과 HLSL 실행을 검사한다.
- Noise Lab export는 schema 14, `implementationStage=13-2`, `similarityScale`을 기록한다.
- Debug/Release 빌드와 양 구성 전체 CTest 25/25가 통과했다. Debug 13.31초,
  Release 17.90초였고 두 구성의 Stage13DomainSmoke와 D3D11 debug-layer가 통과했다.
## 16. 단계 13-2 배율 블록화 코드 진단

- PNG 없이 320×180 float GPU 출력을 readback해 Ray/Noise/Density/Lighting/Composite의
  MAE, RMSE, P99, 최대 오차, mismatch 연결 영역을 CTest 텍스트로 기록한다.
- `Stage13SimilarityCameraMath`와 `Stage13SimilarityGpu`를 추가해 전체 테스트는 27개다.
- 첫 측정에서 현재 `x=40×S` 경로는 10×부터 GPU Ray 게이트가 실패하고 100×/1000×에서
  오차가 확대됐다. 배율 clip만으로는 Noise/Density가 회복되지 않았고 원점 중심 조건에서
  회복되어 `VIEW_PROJECTION_TRANSLATION_PRECISION`으로 분류했다.
- 진단 테스트의 실패는 의도한 검출 결과이며 수정과 사용자 재검증 전에는 13-2를 승인하지 않는다.
## 17. 단계 13-2 translation-free 카메라 레이

- `CameraCB/cbCamera`를 224바이트로 확장해 `invProjection`과 translation이 제거된
  `invViewRotation`을 추가했다. 기존 `invViewProj`는 Scene Depth 복원 전용으로 유지한다.
- Cloud PS는 NDC를 View Space에서 역투영하고 `w=0` 방향에 카메라 회전만 적용한다.
- 순수 상사 GPU 비교에서 고정 meter 진단 장면을 제외해 Scene Depth 비상사 영역이
  Noise/Density 지표에 섞이지 않게 했다. 폐색은 기존 Stage13DomainSmoke가 검사한다.
- 수정 후 1000× CPU/GPU Ray 최대 오차는 각각 `0.00001369°`, `0.00001625°`이며,
  Final Density MAE `0.00000025`, 마스크·연결 mismatch 0으로 자동 게이트를 통과했다.
- Lighting과 Composite는 계속 보고 전용이며 큰 배율 차이는 다음 원인 분석 대상으로 남긴다.
  직접광 적분의 `1/extinction` 항은 S배 증가하지만 `scatteringCoefficient`가 유지되는 단위
  불일치를 우선 원인 후보로 기록했다.

## 18. 단계 13-2 단일 산란 알베도와 조명 상사

- 단위가 불명확하던 `scatteringCoefficient`를 무차원
  `singleScatteringAlbedo(ω=σs/σt)`로 교체하고 기본값 1, 범위 `[0,1]`로 고정했다.
- `extinctionCoefficient=σt(1/m)`, `σs=ωσt(1/m)` 관계를 명시하고 직접광·Sky·Ground·
  Multiple 공통 진폭을 `viewT × ω × (1-stepT)`로 바꿨다. `σt=0`이면 산란도 0이다.
- 다중 산란 octave 모델은 유지하며 알베도는 카메라로 들어오는 최종 산란 사건에 한 번 적용한다.
- 상사 프리셋은 알베도 1을 재설정하되 배율하지 않는다. Noise Lab slider는 `[0,1]`,
  JSON은 schema 15와 `singleScatteringAlbedo`를 기록하며 이전 필드는 내보내지 않는다.
- CPU는 알베도 선형성·sanitize·zero extinction·1×~1000× 상사와 `σs`의 `1/S` 변화를
  검사한다. Debug GPU Current 1000× Accumulated Direct/Composite MAE는 각각
  `0.00036347`/`0.00070073`으로 보고 목표 `0.01`을 통과했다.
- Debug/Release 전체 CTest 27/27이 각각 15.17초/19.86초에 통과했으며 PNG는 생성하지 않았다.

## 19. 단계 13-3 실제 오픈 월드 스케일

- 2026-08-11 사용자가 13-2의 1×~1000× 화면 체크를 완료하고 최종 승인했다.
- 일반 실행 기본값을 Planar `1500~4500m`, View `50km`, 40~50km fade, 64km
  Periodic Perlin Weather와 지상 수평선 카메라로 전환했다. 자동 테스트는 이전 AABB
  초기값을 유지한다.
- `Stage13OpenWorldParameters`가 View `100m/512`, Light `250m/80`, Base/Detail
  `0.00035/0.0025 cycle/m`, density `1`, extinction `0.0005/m`, albedo `1`과
  Base/Detail/Weather 바람 `12/18/8m/s`를 한곳에서 정의한다.
- Noise Lab은 `Open World` 버튼, Base/Detail 파장당 표본, View/Light budget, 수직 τ와
  Weather texel 크기를 표시한다. Actual Step은 budget 안을 파랑→초록, 초과를 노랑→빨강으로 표시한다.
- JSON은 schema 16, `implementationStage=13-3`, `stage13Preset`을 기록하며 Open
  World/Custom의 `similarityScale`은 `null`이다.
- `Stage13OpenWorldMath`와 `Stage13OpenWorldSmoke`를 추가해 전체 테스트를 29개로 확장했다.
  Smoke는 임의 Weather seed·주기·가중치를 먼저 적용해도 Open World가 기본 Periodic
  Perlin hash와 동일한 b1/b3/b5로 원자 복구되는지 검사한다.
- 최종 코드 기준 Debug/Release 빌드와 CTest 29/29가 각각 15.99초/21.33초에 통과했다.
  HLSL 5개 entry도 `/Od /WX`와 `/O3 /WX`에서 모두 통과했고 Open World smoke에
  D3D11 error/corruption은 없었다.
  3D texture, 거리 LOD, Early Exit와 조명 재설계는 현재 범위에 포함하지 않는다.

## 20. 단계 13-4 Base/Detail Texture3D

- 2026-08-11 사용자가 13-3 Open World의 카메라·Actual Step 화면을 확인하고 승인했다.
- 일반 Open World는 seed 1337의 Base `128³ RGBA8`와 Detail `32³ RGBA8` Texture3D를
  사용한다. Similarity 및 이전 단계 자동 회귀는 `ProceduralLegacy`를 유지한다.
- `NoiseVolume.hlsl` compute shader가 periodic gradient Perlin-Worley와 Worley 대역을
  생성한다. 현재 world scale은 Base XZ 8km/Y 6km, Detail 2km이고 GPU 메모리는 각각 8MiB와 0.125MiB다.
- `NoiseVolumeCB(b6)` 96바이트, Base `t3`, Detail `t4`를 C++/HLSL/문서에 함께 추가했다.
  핫리로드는 VS/PS/CS와 새 볼륨 생성이 모두 성공해야 원자적으로 교체한다.
- Noise Lab은 source 전환·결정적 재생성, 채널/결합/타일 경계 진단, 규격·파장당 표본·
  hash·생성 시간을 표시한다. JSON은 schema 18과 volume 규격·seed·hash, Base Y world size와 로컬 높이 설정을 기록한다.
- `Stage13NoiseVolumeMath`와 `Stage13NoiseVolumeSmoke`를 추가해 전체 테스트는 31개다.
  PNG 없이 CPU/GPU 기준 복셀, 분산·채널 hash, 재생성 결정성, seam, cache 왕복,
  다섯 카메라의 유한·구분 출력을 텍스트로 검사한다.
- 최종 코드 기준 Debug/Release CTest 31/31이 각각 53.27초/67.83초에 통과했다.
  HLSL 7개 entry도 `/Od /WX`와 `/O3 /WX`에서 모두 통과했고 D3D11
  error/corruption은 없었다. Debug GPU 생성/readback 보고값은 49.605ms였고
  seam 실제 최대 차이는 `0.00027466`이었다.
- 거리 LOD·mip·제품용 영구 cache·광학 재조정은 13-4 범위에서 제외하며 사용자 화면
  승인 전에는 13-5로 넘어가지 않는다.
- 2026-08-12 최초 Texture3D 동작 확인 뒤 사용자가 모든 구름의 두께가 같은 쿠키틀 모양을
  피드백했다. Weather A를 `localHeightPotential`로 전환하고 A/G로 XZ별 로컬 상단을
  계산하도록 보완했다. 밑면은 1,500m로 공유하고 Base UV는 XZ와 Y를 분리했다.
- CloudCB 예약 float 3개를 minimum thickness/variation/cumulus boost로 교체해 128바이트를
  유지했다. Weather Height/Local Top/Local Height 진단과 schema 18을 추가했고,
  Stage5 CPU와 GPU smoke에서 A 결정성·seam·상단 단조성·비균일 Local Top을 검증한다.
- 보완 후 Debug/Release CTest 31/31을 각각 56.34초/71.36초에 통과했고 HLSL 7 entry도
  `/Od /WX`, `/O3 /WX`에서 통과했다. 자동 PNG는 생성하지 않았으며 사용자 화면 재승인을 기다린다.
- 2026-08-12 사용자 F6 재검증에서 수평 질량에 비해 높이 변화가 여전히 작다는 피드백을
  받았다. 64km Weather 배치는 반복 방지를 위해 유지하고 Base XZ만 16km→8km로 줄여
  지배 파장을 XZ 1.6km/Y 1.2km(`4:3`)로 맞췄다. 최소 로컬 두께도 0.30→0.40,
  즉 900m→1.2km로 올렸다.
- 종횡비 보완 후 Debug/Release CTest 31/31을 각각 52.94초/68.16초에 통과했다.
  Similarity GPU와 Noise Volume GPU smoke도 통과했으며 PNG는 생성하지 않았다.

## 21. 단계 13-4B Weather 기반 물리 두께와 세로 형상

- 쿠키틀처럼 상단이 비슷해 보이는 문제를 분포 테스트로 재현했다. Open World 전역 도메인을
  `1,500~7,500m`로 넓히고 Weather A를 `Local Thickness Potential`로 재정의했다.
- 밀도 경로를 `Weather → 타입별 1~6km 물리 두께 → Typed Vertical Profile → Base 3D
  Shape → Detail 3D Erosion` 순서로 분리했다. Stratus/Mixed/Cumulus는 서로 다른 bottom/top
  fade와 상부 질량을 사용하고 타입 구간에서 연속 보간한다.
- `CloudShapeCB(b7)` 64바이트를 추가했다. Open World는 물리 두께 mode, Similarity와 구형
  회귀는 Legacy mode를 사용한다. 기존 CloudCB의 로컬 상단 필드는 회귀 보존용으로 남겼다.
- Base Texture3D는 XYZ `6,000m`, 주파수 `{3,6,9,12}`로 통일했고 Detail은 XYZ
  `2,000m`, `{2,3,4,5}`로 조정했다. 100m View step에서 최소 samples/wavelength는
  Base 5, Detail 4다. cache version은 2로 올려 구형 주파수 cache를 무효화했다.
- 최대 6km 수직 경로에서도 대표 광학 깊이 `τ=1.5`를 유지하도록 Open World extinction을
  `0.00025/m`로 조정했다. 조명 적분 모델과 step budget은 바꾸지 않았다.
- JSON은 schema 19이며 Weather A 계약, 타입별 두께·프로파일, 새 Texture3D 주파수를 기록한다.
  `Stage13WeatherShapeMath/Gpu`를 추가해 전체 테스트는 33개다. PNG는 생성하지 않고 CPU
  분포와 실제 HLSL float 단면을 텍스트로 검사한다.
- 최종 Debug/Release 빌드와 CTest는 각각 33/33을 통과했다. 전체 실행 시간은
  71.10초/93.20초였고 HLSL 7개 entry도 `/Od /WX`, `/O3 /WX`에서 모두 통과했다.
  Release GPU 단면은 상단 span `1854.75737m`, 표준편차 `600.08451m`, 천장 도달률
  `0`, CPU/HLSL 두께 최대 오차 `0.00072m`, 프로파일 최대 오차 `0`을 보고했다.
- 2026-08-13 사용자 피드백에서 F1과 단축키의 의미를 이해하기 어렵고 F6이 주황 건물
  내부에서 시작하며 휠 줌이 너무 느리다는 문제가 확인됐다. `Stage13CameraPresets.h`로
  F5~F8 position/target을 공통화하고 일반 Open World 카메라에서는 진단 장면을 숨겼다.
  `Shift+F5`만 Scene Depth 건물을 켜며 일반/Shift 휠은 1.25×/2× 지수 줌을 사용한다.
- `STAGE13_4B_DEBUGGING_GUIDE.md`에 전체 F1 UI·단축키·색 판독·형상 실습·원인표를
  추가했고 `Stage13CameraControlMath`를 더해 전체 CTest는 34개가 됐다.
- 최종 Debug/Release CTest `34/34`가 각각 `72.72초/92.00초`에 통과했다. HLSL 7개
  entry도 `/Od /WX`, `/O3 /WX`에서 모두 통과했고 자동 PNG는 생성하지 않았다.

## 22. 단계 13-4B 세로 옆면 shape threshold 보완

- 사용자 렌더에서 가변 상단은 보이지만 구름 몸통의 세로 옆면이 직선으로 남는 문제를
  확인했다. Local Thickness는 XZ 기둥의 상단만 바꾸고, 기존 profile은 threshold 뒤
  밀도에 곱해져 0보다 큰 구간의 XZ support를 충분히 줄이지 못한 것이 원인이었다.
- Physical mode의 밀도 경로를 `Typed Shape Profile → global×Weather shape coverage →
  Base Noise remap → density multiplier → Detail erosion`으로 바꿨다. Profile은 경계에만
  사용하며 Weather UV와 Base/Detail UVW, 고정 1,500m 밑면은 유지한다.
- Stratus/Mixed/Cumulus footprint cutoff를 각각 `0.16/0.08/0.22@0.45`,
  `0.22/0.04/0.38@0.50`, `0.32/0.03/0.62@0.58`로 정하고 전체 높이에 걸쳐 연결해
  중간 plateau와 Weather R=1 수직 코어를 제거했다. Legacy mode 결과는 유지한다.
- `Typed Shape Profile`, `Effective Shape Coverage`, `Base Support Before Density` 진단과
  Noise Lab 출력 2개를 추가하고 JSON을 schema 20으로 올렸다. CloudShapeCB는 64바이트다.
- CPU는 profile 0 제거, threshold 단조성, R=1 taper, 타입별 support와 plateau 부재를
  검사한다. GPU는 CPU/HLSL 오차 0과 Mixed support `0.221/0.640/0.212`, Cumulus
  `0.017/0.592/0.206`을 확인해 중간 폭의 바닥 대비 10%, 상단 대비 15% gate를 통과했다.
- 최종 Debug/Release 전체 CTest `34/34`가 각각 `78.35초/106.33초`에 통과했다.
  HLSL 7개 entry도 `/Od /WX`, `/O3 /WX`에서 통과했고 D3D11 error/corruption은 없었다.
- 자동 검증은 수식과 support 분포만 합격 처리하며 최종 Composite의 직선 옆면 제거와
  미적 품질은 사용자 재검증 전까지 승인하지 않는다.

## 23. 단계 13-4B 시간 기반 구름 이동 복원

- 과거 공통 `WindOffsetWorld`와 달리 재구축 Stage 5는 Base/Detail/Weather 속도를
  분리했다. Open World Weather는 `64km/8m/s`로 정규화 이동도 느리고 Physical Shape의
  외곽을 지배해, `windSpeed`를 바꿔도 고정된 실루엣 안에서 Base 밀도만 흐르는 문제가 났다.
- Physical Shape에 공통 수평 변위
  `normalizeOrZero(windDirection.xz) × windSpeed × effectiveTime`을 추가하고 Weather R/G/A,
  Base, Detail 모두 같은 `f(p-vt)` 위치를 읽게 했다. Y는 고정하며 Legacy는 기존 세 독립
  속도 수식을 보존한다. CloudCB 128바이트와 CloudShapeCB 64바이트는 변하지 않았다.
- Physical UI는 `Cloud Wind Speed (Bulk)`를 표시하고 Weather/Detail 속도를 Legacy 전용으로
  비활성화한다. Animation에는 Effective Time, 누적 Bulk 거리, Weather texel/s를 표시한다.
- JSON을 schema 21로 올리고 `physicalAdvectionMode=rigidSharedWindSpeed`, Bulk 속도·누적
  거리와 Legacy 속도 사용 여부를 기록한다.
- CPU는 정지·비정상 입력, `p+vΔt,t+Δt` 좌표 불변식과 Pause/Resume 시간 연속성을 검사한다.
  GPU는 이동 보정 프레임 MAE `0.00000001`, Legacy 속도 변경과 Bulk 정지 MAE `0`을 확인했다.
  기존 Mixed/Cumulus support와 두께·상단 분포 gate도 그대로 통과했다.
- 최종 Debug/Release 빌드와 전체 CTest `34/34`가 각각 `78.21초/105.71초`에 통과했다.
  HLSL 7개 entry도 `/Od /WX`, `/O3 /WX`에서 통과했고 D3D11 debug-layer 오류는 없었다.
  자동 PNG는 생성하지 않았으며 실제 이동 화면과 Pause/Resume은 사용자 승인을 기다린다.

## 24. 단계 13-4B 승인과 13-4C 개발 UI 역할 분리

- 2026-08-14 사용자가 Weather 기반 가변 두께·세로 형상과 강체 이동을 포함한 단계
  13-4B 화면을 최종 승인했다. 13-5 광학 구현 전에 개발 UI만 정리하는 13-4C를 둔다.
- 기존 단일 F1 창을 F1 Noise/Density/VSync/Time, F2 Weather Map/Generator,
  F3 Directional/Phase/Environment, F4 Camera의 네 독립 창으로 분리했다. 각 대분류는
  기존처럼 접고 펼칠 수 있고, F5~F12 검증 단축키는 유지한다.
- 기존 F2~F4 Weather 즉시 전환은 제거하고 F2 창의 Weather Preset에서 Uniform Legacy,
  Periodic Perlin, Channel Debug를 선택한다.
- Camera가 20~120° 수직 FOV와 디버그 이름·수동 조정 상태를 제공한다. F4는 position,
  target, distance, clip, FOV를 표시하고 F5~F8과 같은 네 버튼 및 메모리 북마크 저장·복원을
  제공한다.
- JSON은 schema 22로 올라가 현재 카메라와 저장 북마크의 position/target/FOV/clip/
  진단 장면 상태를 기록한다. 북마크는 캡처 재현 정보이며 앱 시작 시 자동 로드하지 않는다.
- Debug/Release 빌드와 전체 CTest `34/34`가 각각 `24.88초/31.27초`에 통과했다.
  `Stage13CameraControlMath`는 FOV clamp·round-trip·디버그 상태를 검사한다.
  `--stage8-smoke-test` export를 실제 JSON으로 파싱해 schema 22와 현재 카메라, 빈 저장
  슬롯을 확인했고 실제 앱에서 F1~F4 네 창의 독립 타이틀·분류·카메라 값을 캡처했다.
  사용자 최종 조작 승인은 남아 있다.

## 25. 단계 13-4C Local Cloud Inspector

- F1의 기술적인 `AABB Reference` 노출을 `Local Cloud Inspector`로 바꾸고
  `Open World Layer`와 전환하도록 했다. 내부 `CloudDomainType::AabbReference`와 HLSL
  `kCloudDomainAabb`는 기존 회귀 호환성을 위해 유지했다.
- Inspector는 `(-100,5,-120)m~(100,55,80)m` AABB, 200m Weather, `90m/30m`
  Base/Detail, View `0.5m×512`, Light `1m×256`, extinction `0.03/m`를 사용한다.
  기존 필드와 20×60×20m 주황 건물 뒤에 약 140m 단일 구름을 배치해 Scene Depth를 본다.
- `LocalInspector` Weather는 빈 테두리 안의 단일 연결 원이며 B=중립, A=최대 두께를
  고정한다. Stratus/Mixed/Cumulus는 같은 Coverage·Noise에서 G와 `15~25m`/보간/
  `30~50m` 물리 두께·세로 Profile만 바꾼다.
- F5~F8은 장면에 따라 Inspector 초기 Depth/측면/상부/내부 또는 기존 Open World 네
  시점을 사용한다. `Camera::TranslateRigLocal`과 메인 루프 delta로 Inspector에서만
  `WASD 20m/s`, `Shift 80m/s`를 처리하며 ImGui keyboard capture 중에는 멈춘다.
- Renderer가 두 장면의 공간 설정과 카메라를 따로 저장·복원한다. Noise 채널·주파수,
  Detail erosion, 정규화 Profile, 태양·Phase·Environment·Time은 공통 상태로 유지한다.
- JSON을 schema 23으로 올려 `cloudScene`, `inspectorCloudType`, 두 장면 상태와 카메라를
  기록한다. CPU/HLSL 상수버퍼 크기와 별도 Inspector shader 분기는 추가하지 않았다.
- `Stage5WeatherMath`, `Stage13CameraControlMath`, `Stage13InspectorMath`와
  `Stage13InspectorSmoke`가 단일 구름, Type 채널 불변, rig 이동, 수치 계약, 여러 카메라와
  Depth·Profile·Composite, Texture3D·Physical Shape·상태 왕복을 검사한다. 최종 화면과
  360°·WASD 조작은 사용자 승인 대기다.

## 26. Local/Open 입력·파라미터 통합

- Local 이동과 동시에 `W/A/S/D`가 Thin Volume/Sparse/Dense/Large Blobs를 실행하던
  중복 입력을 제거했다. 네 키는 두 도메인 공통 rig 이동 전용이며 기존 프리셋은 F1
  `Legacy Validation Presets` 버튼으로 이동했다.
- F4에 Move Speed `1~2000m/s`를 추가했다. 27절 변경 뒤 기본은 Local `20m/s`, Open
  `1000m/s`, Shift는 4배이며 delta는 `0.1s`로 제한한다.
- Renderer 상태를 공통 렌더 설정과 도메인별 AABB/Planar 기하·trace·카메라로 분리했다.
  장면 전환은 Weather texture나 Noise·Shape·조명·sampling을 변경하지 않는다.
- Cloud Type을 `Weather Map / Stratus / Mixed / Cumulus` 공통 모드로 확장했다. 고정
  Type은 모든 Weather preset에서 G만 교체하고 R/B/A는 유지한다.
- Planar modifier 카메라는 F4의 Building Depth/Scale Compare/Tile Wrap 버튼으로 옮겼고,
  export는 이후 schema 25의 `sharedRenderState`, `domainStates`, `cloudTypeMode`,
  `cameraMovement`를 기록한다. CPU/HLSL 상수버퍼와 교차 셰이더는 바꾸지 않았다.
- 최종 Debug/Release 전체 빌드와 CTest `36/36`이 각각 `90.25초/118.90초`에 통과했다.
  Inspector export JSON도 `ConvertFrom-Json`으로 파싱해 공통 렌더 상태,
  Cloud Type, 이동 속도와 양쪽 valid domain camera를 확인했다.

## 27. Open World 입력과 렌더 파이프라인 비교

- ImGui의 일반 keyboard capture 대신 텍스트 입력·활성 item만 장면 입력을 차단한다. 전역
  진단 키를 ImGui보다 먼저 분류하고 상단/숫자 패드 `0~9`를 같은 출력에 연결했으며 ImGui
  keyboard navigation이 WASD를 선점하지 않게 했다.
- WASD는 두 도메인에서 rig 이동만 수행한다. 이동 속도를 카메라와 같은 도메인 상태로 옮겨
  Local `20m/s`, Open World `1000m/s`, Shift `4×`를 각각 저장·복원한다.
- 최신 Open World를 기본으로 유지하면서 Legacy 1000x, Texture3D, Periodic Weather,
  Physical Shape, Full Open World의 누적 비교 프리셋을 추가했다. 모든 단계는 현재 AABB/Planar
  기하·카메라·이동 속도를 보존한다.
- export는 schema 25로 올라가 `openWorldPipelinePreset`과 양 도메인 이동 속도를 기록한다.
  CPU 입력 검사와 Open World smoke는 5단계×6개 출력을 렌더해 finite 값, 구분되는 hash,
  도메인 불변성과 Full Open World 복원을 검사한다.
- Debug/Release 전체 빌드와 CTest `36/36`이 각각 `101.23초/132.17초`에 통과했다. 실제
  schema 25 JSON도 파싱해 Local 사용자값 `25m/s`와 Open World `1000m/s` 복원을 확인했다.

## 28. Open World Weather·Base Noise 기본 품질 재조정

- Coverage 기본 생성값을 seed `1013`, Macro/Detail `4/11`, Detail Weight `0.42`, Bias
  `-0.02`, Contrast `1.15`, Threshold/Softness `0.56/0.14`로 바꿨다. 기본 256² 맵은
  이전 `68.8% / 1개 / 68.8%`에서 Coverage 약 `43.8%`, 토러스 연결 성분 `10개`,
  최대 성분 전체 면적 약 `31.2%`로 바뀌어 한 개의 큰 구름군 대신 여러 중소 구름군을 만든다.
- CPU 전용 `thicknessCoverageInfluence`와 F2 `Thickness-Coverage Link`를 추가하고 기본을
  `0.20`으로 정했다. Weather A는 독립 두께장 80%와 Coverage 중심 20%를 보간하며 빈
  Coverage의 A=0, Uniform/Debug/Inspector 동작과 Cloud/HLSL 상수버퍼 배치는 유지한다.
- Base Texture3D를 XYZ `12km`, 주파수 `{4,9,17,23}`으로 바꾸고 Perlin octave seed를
  `seed + octave×173`으로 분리했다. 100m step 최소 표본은 약 `5.22/파장`이며 G/B/A
  Worley, Detail `32³/2km`, Local Inspector `90m`, texture fetch 수는 그대로다.
- 생성 알고리즘을 구형 파일과 분리하도록 cache version을 `3`, export schema를 `26`으로
  올렸다. 회전 이중 샘플, domain warp, 구형 shell은 이번 범위에 넣지 않았다.
- Weather 면적·연결 성분·R/A 상관·두께 분포, 12km wrap/6km 비반복, octave seed와 cache
  v2 거부를 CPU 회귀로 고정했다. 기본 R/A 상관은 `0.56434`, 두께 P95-P05는
  `2087.4m`, 상단 표준편차는 `593.7m`, 5.9km 천장 비율은 `0`이었다. 13-4B의
  Weather·Base·Thickness 화면 판정은 재검증 대기다.
- Debug/Release 빌드와 전체 CTest `36/36`이 각각 `95.25초/129.00초`에 통과했고 HLSL
  7 entry도 `/Od /WX`, `/O3 /WX`에서 통과했다. Release Base 생성은 `112.076ms`, wrap
  최대 차이는 `0.00015259`, D3D11 debug-layer 오류는 없었다. 런타임 shader의 Texture3D
  조회 수는 바꾸지 않아 fetch 증분은 0이며, 동일 조건의 변경 전 GPU Cloud 기준 캡처는 없어
  p95 전후 비교는 이번 기록에 만들지 않았다.

## 29. 단계 13-4C 사용자 승인과 13-5 시작

- F1~F4 역할 분리, Local Cloud Inspector, 양쪽 도메인 WASD·카메라 복원, Open World
  5단계 파이프라인 비교와 Weather/Base/Thickness 기본 품질을 2026-08-16 사용자가
  최종 승인했다.
- 단계 13-5는 승인된 형상 입력을 고정하고 View/Light 광학 수렴, Base-only 자기 그림자,
  Phase·환경광 성분 분리와 Detail 거리 LOD만 다룬다. 단계 9 이후 최적화와 13-6 shell은
  계속 제외한다.

## 30. 단계 13-5 km 광학·조명과 Detail 거리 LOD

- `Stage13OpticsLightingMath`에 meter/1m Beer-Lambert, 1×~1000× τ 불변, Light
  62.5/125/250m 후보와 Detail LOD 연속성·weighted mean 기준을 추가했다.
- Open World Light 기본을 `250m/80`에서 `125m/160`으로 바꿨다. 96×54 Ground Horizon의
  `62.5m/320` reference 대비 Light T/누적 직접광/Composite MAE는
  `0.00000584/0.00000070/0.00000115`였다. 이전 250m 값은 비교 보고만 유지한다.
- CPU는 100m까지 허용하면서 HLSL이 1m로 자르던 Light bias를 HLSL도 `0~100m`로 맞췄다.
  따라서 1000× 상사 프리셋의 10m bias가 보존된다.
- 새 16바이트 `CloudLodCB(b8)`는 기본 32~48km에서 Detail을 실제 `32³ RGBA8` weighted
  mean `0.44994098`로 수렴시킨다. 끝 거리 밖에서는 `t4` fetch를 생략하되 평균 침식량은
  유지한다. Similarity 프리셋은 LOD를 꺼 기존 회귀를 보존한다.
- F3에 세 Light 후보, Quality/Balanced LOD와 View τ·Direct/Sky/Ground/Multiple·LOD factor
  출력 버튼을 추가했고 export를 schema 27로 올렸다. 단계 9 이후 최적화와 실제 대기 입력은
  추가하지 않았다.
- Debug/Release 빌드와 전체 CTest `38/38`이 각각 `121.29초/157.15초`에 통과했다.
  HLSL 7개 entry도 `/Od /WX`, `/O3 /WX`에서 총 `14/14` 통과했고 D3D11 debug-layer
  오류·NaN·검정 프레임은 없었다. 최종 Composite의 조명 방향과 LOD 전환은 사용자 화면
  승인 대기다.

## 31. 단계 13-5 사용자 조명 피드백 보완

- 사용자 검증에서 Open World `62.5/125/250m`는 같은 실루엣과 그림자 순서를 보였지만,
  200m Local Inspector에 같은 km 후보를 적용하면 62.5m와 125/250m의 그림자 방향이
  달라졌다. 이는 90m Base volume의 최소 파장 약 3.91m를 모두 undersample한 잘못된 비교다.
  F3는 Local `0.5/1/2m`와 Open World `62.5/125/250m`를 장면별로 분리하고 Local에서
  Detail 거리 LOD를 비활성 표시한다.
- F3 Directional Light에 XZ 도식을 추가했다. 노란 Sun 위치, 주황 incoming light, 빨간
  shadow-away 방향, 청록 camera forward와 현재 카메라 기준 Sun/Shadow 좌우 판정을 함께
  표시한다. `directionToSun`과 실제 광선 진행 `-directionToSun`을 혼동하지 않게 했다.
- 방향 변경 때 View τ는 불변이고 누적 Direct만 변한다는 사용자 결과를 GPU gate로 추가했다.
  Low East/West의 View τ MAE는 `0`, Direct MAE는 `0.00234312`였다.
- Silver Lining은 `g/intensity=0.75/0.10`, 최종 적용 Phase 상한 `2.5`로 낮췄다. Balanced
  Sky/Ground/AO/Multiple energy/phase를 `0.12/0.05/1.50/0.20/0.25`로 재조정하고,
  Composite peak 0.8 위에 hue-preserving LDR shoulder를 적용했다. 96×54 Silver Lining
  최대 RGB는 Low East `0.93706274`, Low West `0.79763252`로 흰색 clip gate를 통과했다.
- 최종 Debug/Release 전체 CTest `38/38`은 각각 `122.55초/154.68초`, HLSL 7개 entry의
  `/Od /WX`와 `/O3 /WX`는 총 `14/14` 통과했다. 수정된 Local 수렴·Balanced 대비·Silver
  Lining 화면은 사용자 재검증 대기다.

## 32. 단계 13-4D 단일 포트폴리오 디버깅 씬

- 13-4C의 사용자 승인 이력은 보존하되 Local Inspector 런타임, 전용 Weather, 장면
  enum·저장/복원과 smoke를 제거했다. 일반 실행은 Planar 1.5~7.5km, View 50km,
  Weather 64km의 단일 씬만 사용한다.
- 불투명 장면은 Y=0의 10km×10km 실제 quad와 원점의 20×60×20m 건물 하나로 줄였다.
  linear RGB는 각각 `(0.10,0.14,0.12)`, `(0.02,0.025,0.035)`이며 모든 일반 카메라에서
  항상 색상과 깊이를 쓴다.
- F5~F8을 Hero/건물 Depth, 지면 수평선, 구름 내부, 구름 위 하향으로 고정했다. 전역 입력은
  숫자 0~9, F1~F8, WASD/Shift, 마우스 오빗·휠만 남겼다. HLSL ID 1~7과 문자/F9~F12
  진단 키를 삭제하고 숫자는 기존 ID 0/10/20/16/17/12/52/32/8/24에 명시적으로 매핑했다.
- F1/F3의 메인 출력은 두 Debug View 콤보로 통합했다. F2는 Periodic Perlin·Channel Debug와
  공통 `CloudTypeMode`, F4는 카메라 네 개·수치·저장/복원·내보내기만 노출한다.
- Stage1/2/4 사용자 preset enum·Renderer 상태/API는 제거하고 과거 smoke는 직접 값 fixture를
  사용한다. AABB·Similarity·Procedural/Uniform legacy는 자동 회귀와 5단계 Pipeline Compare
  내부에서만 유지한다.
- export를 schema 28, `implementationStage=13-4D`로 올리고 `sceneContract`를 추가했다.
  장면/도메인/preset/similarity/diagnostic 상태 필드는 제거했다. `Stage13SceneMath`와
  `Stage13UnifiedSceneMath/Smoke`가 새 수학·입력·카메라·GPU·JSON 계약을 검사한다.
- 기존 13-5 자동 측정값은 이력으로 보존하지만 새 F6에서 같은 허용 오차로 다시 측정하고,
  13-4D 및 13-5 사용자 승인이 끝나기 전에는 13-6이나 단계 9 이후로 넘어가지 않는다.

## 33. 단계 13-4E Dense Broken-Sky와 구름 타입 프리셋

- 13-4D 사용자 검증에서 Full Open World가 특정 Weather 지역에만 존재하고, 세로 profile이
  noise threshold를 다시 줄여 상단이 납작하며, `0.00025/m` 광학값에서 명암이 약한 문제가
  확인됐다. 13-4D 단일 scene 이력은 보존하되 최종 외형은 13-4E가 대체한다.
- Physical density를 Weather support, 70~100% Weather factor, 80~100% footprint factor,
  Base noise, vertical profile의 독립 단계로 나눴다. Profile은 최종 Base 밀도에 한 번만
  곱하고 Detail은 완성된 Base 경계만 침식한다.
- 결정적 Dense Mixed/Stratus/Cumulus를 추가했다. 기본 Weather R non-zero/core는 각각
  `79.62/49.11`, `87.31/56.60`, `73.82/42.07%`이며 Full Open World는 항상 Dense Mixed를
  복원한다. View `512×100m`, Light `160×125m`, 태양·Phase·환경광은 바꾸지 않았다.
- F1 `Cloud Type Settings`에 Stratus/Cumulus/Custom/Save를 추가하고 F2 타입 버튼은 읽기
  전용 G mode로 교체했다. 외형 slider 편집은 `Custom Unsaved`, 명시적 저장만 Custom이 된다.
- export를 schema 29/`13-4E`로 올렸다. 안정 Custom은
  `captures/noise-lab/custom/noise-settings.json`에 원자 저장하며 엄격한 자체 parser가
  누락·구버전·손상·비유한·범위 밖 값을 렌더 상태 변경 없이 거부한다.
- Pipeline Compare UI의 main cloud pass는 effective time 0을 사용해 단계별 좌표를 고정한다.
  일반 animation state는 건드리지 않으며 Compare 밖에서는 즉시 정상 time으로 복귀한다.
- 당시 외형 프리셋 회귀 테스트와 확장 `Stage13UnifiedSceneSmoke`가 점유율, density 0 조건, preset
  불변 상태, Custom JSON, F5/F6 주요 출력의 finite/non-black/distinct hash를 검사한다.
  Debug/Release 전체 CTest는 각각 `39/39`, HLSL 7개 entry의 `/Od /WX`·`/O3 /WX`는
  총 `14/14` 통과했다. Dense Mixed F6의 13-5 품질 Light T/Direct/Composite MAE는
  `0.00001641/0.00000270/0.00000508`로 기존 허용 오차를 유지했다.
  최종 broken-sky 모양과 명암은 사용자 승인 대기이며 그 전에는 13-5 승인 또는 이후 단계로
  넘어가지 않는다.

## 34. 단계 13-4E Coverage와 13-5 Light 비용 보완

- 2026-08-17 사용자 비교에서 Stratus/Cumulus Coverage `0.72/0.68`이 구름을 지나치게
  뭉치게 만든 문제가 확인돼 Weather·두께·광학값은 유지하고 `0.40/0.45`로 낮췄다.
- 세 Light 후보의 화면 차이가 크지 않고 `250m/80`이 가장 빨라 Open World 기본을
  `125m/160`에서 되돌렸다. `125m/160`은 이전 품질 비교, `62.5m/320`은 reference로 남긴다.
- Physical Light 전용 scalar Base 경로가 Weather support·로컬 높이·세로 profile 공백에서
  Base Texture3D fetch를 생략한다. 누적 `tau≥9.21034`, 즉 `T≤0.0001`이면 해당 Light
  Ray만 끝내며 디버그 sample 수는 실제 반복 횟수를 표시한다. View 최적화는 추가하지 않았다.
- Dense/Stratus/Cumulus의 새 기본 Composite MAE는 reference 대비 각각
  `0.00002579/0.00000912/0.00001599`, P99는
  `0.00029521/0.00010890/0.00017973`로 `0.01/0.03` gate를 통과했다.
- Silver Lining을 켜도 외곽 입체감이 부족하다는 사용자 피드백은 재현됐으며 Phase/외곽광은
  이번 변경에서 건드리지 않고 다음 작업으로 분리했다.

## 35. 단계 13-5 태양 노출 기반 외곽광과 환경광 보완

- 추가 Light Ray나 texture fetch 없이 기존 `Tsun`을 재사용한다. 직접광은
  `Tsun^shadowExponent`, Phase 범위는 `Tsun^edgeOpticalDepthScale`로 분리했으며 중립값은
  기존 Stage 8/13-5 결과를 보존한다.
- Silver Lining을 `g/blend/intensity=0.75/0.90/0.20`, 외곽 비율/폭/그림자 지수
  `0.85/2.0/1.35`로 바꿨다. F3에 Portfolio Hero와 Silver Contribution, Shaped Sun,
  Ambient Visibility 분리 출력을 추가했다.
- 환경광은 밀도 AO에 태양 가시성을 선택적으로 결합하고 다중 산란을 차폐 내부로 옮긴다.
  기존 Balanced는 중립, Portfolio Ambient는 coupling/exponent/interior blend
  `0.55/0.50/0.75`다.
- LightCB와 EnvironmentCB를 각각 80바이트로 확장하고 C++/HLSL/아키텍처 표를 동기화했다.
  전체 snapshot은 schema 30/`13-5`, 외형 Custom 파일은 schema 29를 유지한다.
- GPU smoke에서 View τ 방향 MAE `0`, Direct/Silver 방향 MAE
  `0.03554698/0.03117338`, 노출 외곽/내부 Silver 평균 `0.12536342/0.06037134`,
  near-white 비율 `0`으로 자동 gate를 통과했다. 미적 품질은 사용자 재승인 전까지 미완료다.
- `1920×925` Cumulus F6 Noon/Balanced에서 원시 GPU Cloud 600표본 평균/p95는
  변경 전 `13.688282/15.639552ms`, 변경 후 `13.867684/15.785984ms`다. 새 p95 증가는
  약 `0.94%`로 +5%와 16.67ms gate를 모두 통과했다. 과거 UI EMA와 원시 p95는 섞지 않는다.
