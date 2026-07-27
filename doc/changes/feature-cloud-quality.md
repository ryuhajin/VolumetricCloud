# feature/cloud-quality 변경 기록

## 1. 목표와 배경

Stage 2 구름은 렌더되고 있었지만 화면에서는 흐린 한 덩어리처럼 보였고 조명 대비가 약했다. 목표는 3일 쇼케이스 범위에서 형태 주파수, 내부 명암과 샘플링 경계를 개선하고 재현 가능한 기술 판단과 검증 근거를 기록하는 것이다.

## 2. 기존 구현과 관찰된 문제

2026-07-27 사용자 캡처에서 구름은 AABB 안에 존재했지만 큰 실루엣이 흐리고 내부와 가장자리의 밝기 차이가 작았다. base는 128³ R8 단일값, detail은 64³ RGBA8이었다. 조명은 이미 6-step light march와 HG single scattering이 있었으므로 “빛 미구현”은 원인이 아니었다.

## 3. 원인 분석과 근거

- 128³ 자체보다 base가 단일 채널이라 저·중·고주파 형태를 분리 조합하지 못한 점이 더 컸다.
- detail 침식이 코어까지 적용돼 큰 형태가 약해졌다.
- 단일 높이 페이드는 적운 특유의 좁은 하단과 부푼 중단을 만들지 못했다.
- HG는 정규화되지 않았고 view 방향 부호도 산란 정의와 맞지 않아 파라미터 변화가 예측하기 어려웠다.
- 균일한 낮은 ambient와 단일 산란은 그림자와 얇은 가장자리의 정보가 부족했다.
- RGBA 절차식 노이즈를 view/light loop에 직접 넣자 fxc 컴파일이 60초 이상 걸렸다. 캐시 경로 분리가 필요했다.
- PowerShell에서 GUI subsystem 실행 파일을 `&`로 호출하면 완료를 기다리지 않아 캐시 생성과 테스트가 경합할 수 있었다.

## 4. 검토한 대안

- **base/detail 해상도 증가:** 메모리와 생성비용을 즉시 늘리지만 단일 채널의 정보 부족은 해결하지 못해 기각했다.
- **정확한 다중 산란:** 품질은 좋지만 3일 일정과 DX11 픽셀 비용에 맞지 않아 3-octave 근사를 선택했다.
- **프레임별 랜덤 jitter:** 밴딩은 줄지만 temporal reprojection 없이 shimmer가 생겨 고정 screen-space jitter를 선택했다.
- **메인 루프의 절차식/캐시 동적 분기:** fxc 컴파일과 픽셀 비용이 폭증해 메인은 캐시 전용, 절차식은 Inspector와 1회 차이 진단으로 제한했다.
- **반해상도 temporal:** 별도 history/depth/reprojection 검증이 필요해 이번 브랜치에서 제외했다.

## 5. 선택한 해결 방법

base RGBA에 Perlin-Worley와 세 Worley 밴드를 저장하고 거시 경계에서 조합한다. detail은 코어가 아닌 경계에만 적용한다. 적운 높이 프로파일, dual-lobe phase, powder/silver lining, 완화된 light visibility를 합성한다. view march는 48~128 step과 고정 jitter를 사용하고 빈 공간은 2배로 건너뛴다.

## 6. 실제 수정 내용

- `CloudParameters`/`CloudCB`를 96바이트에서 128바이트로 확장했다.
- base를 128³ RGBA8로 바꾸고 캐시를 v4로 올렸다.
- `coverage`, `baseErosion`, 적운 높이 profile과 경계 detail erosion을 추가했다.
- 정규화 dual-lobe HG, 3-octave visibility 근사, powder, silver lining, 높이 ambient를 추가했다.
- `viewSteps`, `jitterStrength`와 빈 공간 가변 marching을 추가했다.
- Base R/G/B/A, visibility, phase, ambient, direct 진단 모드와 Showcase 프리셋을 추가했다.
- 기본 카메라 타깃을 Showcase 밀도 중심에 맞췄다.
- branch별 `doc/changes/` 기록 규칙과 템플릿을 도입했다.
- 새 clone에서도 cache smoke가 재현되도록 배포 기본 bundle의 `.cso`만 git ignore 예외로 추적했다.

## 7. 캐시·호환성·성능 영향

base는 약 2 MiB에서 8 MiB로 증가하고 detail을 포함한 총 볼륨은 약 9 MiB다. 캐시 v4는 v3을 명시적으로 거부하며 기본 bundle을 재생성했다. 메인 ray/light loop는 Texture3D만 읽어 절차식 RGBA Worley의 큰 실행 비용을 피한다. view 기본값은 96, light 기본값은 8이다.

## 8. 테스트 및 실행 결과

최종 검증:

- 직접 fxc `ps_5_0` 컴파일 성공
- v4 cache 저장 및 정상 cache hit 확인
- Debug 빌드 성공
- Release 빌드 성공
- `VolumetricCloud.CacheSmoke` 0.32초 통과
- `VolumetricCloud.CodeTests` 36.39초 통과
- base 네 채널 분산, density 비퇴화, 캐시 round-trip, phase/다중 산란 유한값 검사 추가

기본 캐시 생성은 PowerShell에서 다음처럼 프로세스 완료를 기다린 뒤 테스트했다.

```powershell
$process = Start-Process .\build\Release\VolumetricCloud.exe `
    -ArgumentList "--build-default-cache" -Wait -PassThru
ctest --test-dir build -C Release --output-on-failure
```

시각 관찰:

- 날짜: 2026-07-27
- 프리셋: `Cumulus Showcase`
- 카메라: 기본 오빗 타깃
- 관찰: R8 단계보다 여러 크기의 형태와 내부 명암이 분명해졌으며 AABB는 기본 비표시다.
- 사용자 최종 승인: 대기

## 9. 남은 문제와 후속 개선

- 사용자 장비에서 Showcase 프리셋 최종 튜닝과 승인
- GPU timestamp query를 이용한 실제 1280×720 30 FPS 확인
- depth-buffer 합성, weather map, 반해상도 temporal reprojection
- 화면 밖 장거리 구름층과 지구 곡률
