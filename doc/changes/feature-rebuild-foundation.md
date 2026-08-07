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
