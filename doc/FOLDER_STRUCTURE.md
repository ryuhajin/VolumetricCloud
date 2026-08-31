# 폴더 / 파일 구조

```text
VolumetricCloud/
├─ CMakeLists.txt                  # 앱과 단계별 수치·D3D smoke 테스트 빌드
├─ README.md / AGENTS.md           # 사용자 안내 / AI 작업 규칙
├─ doc/
│  ├─ ARCHITECTURE.md              # 현재 파이프라인과 상수버퍼
│  ├─ RAYMARCHING.md               # 교차·적분·noise 밀도 수식
│  ├─ PERFORMANCE.md               # 동일 조건 성능 측정 절차와 지표 범위
│  ├─ STAGE13_4B_DEBUGGING_GUIDE.md # F1~F4·단축키·카메라·형상 판독 초보자 가이드
│  ├─ STAGE14_ATMOSPHERE_VALIDATION_GUIDE.md # 대기 LUT·지면·HDR 사용자 화면 승인 가이드
│  ├─ STAGE15_PRESET_VALIDATION_GUIDE.md # 10분 승인·상세 판정·증상별 진단·사용자 캡처 가이드
│  ├─ CIRRUS_CLOUD_AUTHORING.md    # 공용 Texture3D를 방향성 샘플링하는 권운 초심자 제작 가이드
│  ├─ ROADMAP.md                   # 사용자 승인 기반 0~15단계
│  ├─ VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md # 단계 13~15 포트폴리오 구현·승인 계획
│  ├─ FOLDER_STRUCTURE.md
│  ├─ CONTRIBUTING.md
│  └─ changes/
│     ├─ feature-rebuild-foundation.md
│     ├─ feature-stage15-final-quality.md
│     └─ STAGE15_PERFORMANCE_REGRESSION.md # Stage 15 회귀 원인·실험·전후 수치 회고
├─ src/
│  ├─ main.cpp
│  ├─ Window.* / Camera.*          # PMv2 일반 1920×1080 중앙 배치·작은 화면 16:9 fit·Native 1080p, 입력과 카메라
│  ├─ VolumetricCloud.manifest / .rc # 프로세스 PerMonitorV2 DPI awareness 리소스
│  ├─ Renderer.*                   # Stage 15 transaction/진단 입력 guard/bytecode cache, 대기 LUT, Deep Shadow, HDR 합성
│  ├─ NoiseLab.*                   # 정리된 F1~F4, Scale Budget, Physical Fill/Rim 진단, schema 38 snapshot
│  ├─ DeveloperUiSettings.*        # DPI×Zoom 배율과 developer-ui schema 1 원자 저장
│  ├─ CloudFormationSettings.*     # F1/F4 공통 formation snapshot, strict 검증·Weather/domain 원자 적용 준비
│  ├─ CloudFormationPresetStore.*  # F4 4개·F1 3개·Custom 1개 독립 schema 1 저장소와 내장 resolver
│  ├─ CloudAppearance.*            # 구형 외형·Custom schema 30/29 migration 호환 경로
│  ├─ WeatherMap.*                 # 256² CPU periodic Perlin/Channel Debug RGBA 생성과 해시
│  ├─ CloudParameters.h            # CPU/HLSL 공유 구름 설정
│  ├─ CloudLodParameters.h         # 16바이트 Detail 거리 LOD 설정(b8)
│  ├─ OptimizationParameters.h     # 64바이트 View/Light 설정과 offset 44 호환 padding(b9)
│  ├─ Stage10UpsamplingParameters.h # 32바이트 해상도·공간 필터 설정(b10)
│  ├─ Stage10UpsamplingMath.h      # 크기·UV·대표 깊이·joint weight CPU 기준
│  ├─ Stage11TemporalParameters.h  # 144바이트 재투영/history 설정과 상태(b11)
│  ├─ Stage11TemporalMath.h        # jitter·D32 plane source gate·3×3 Cloud Depth·재투영·EMA CPU 기준
│  ├─ Stage12ShadowParameters.h    # 160바이트 Deep Cache/cascade/표면 설정(b12)
│  ├─ Stage12ShadowMath.h          # light basis·UV·snap·slice·Beer-Lambert CPU 기준
│  ├─ AtmosphereParameters.h       # 물리 대기·태양 시간·LUT 디버그 CPU 설정
│  ├─ GroundLightingParameters.h   # Concrete/Grass/Snow/Desert/Custom와 지면 반사광
│  ├─ ToneMappingParameters.h      # ACES/Linear/Legacy, Exposure와 white balance
│  ├─ Stage14Parameters.h          # 224바이트 Stage14CB(b13)와 LUT 크기 계약
│  ├─ Stage14AtmosphereMath.h      # 구면 대기·밀도·위상·LUT UV·HDR CPU 기준
│  ├─ CloudShapeParameters.h       # 112바이트 물리 두께·타입 Profile·Cirrus 설정(b7)
│  ├─ CloudShapeDomainContract.h   # 활성 로컬 두께+base lift+headroom 원자 fit
│  ├─ Stage15Parameters.h          # CPU 전용 품질·콘셉트·진단 descriptor와 순수 resolver
│  ├─ CloudRimParameters.h         # 독립 48바이트 NTE rim 설정, Composite pass-local b10
│  ├─ Stage15CirrusMath.h          # Cirrus 방향 UV·물리 두께·profile CPU 기준
│  ├─ CloudDomainParameters.h      # 교차 도메인·추적 한계·Physical LUT 대표 고도(b5)
│  ├─ LightParameters.h            # CPU/HLSL 공유 태양광 설정과 프리셋
│  ├─ EnvironmentParameters.h      # Manual 환경광·다중 산란·Physical fill 설정
│  ├─ Stage7PhaseMath.h            # HG·방향 부호·Dual-lobe CPU 기준
│  ├─ Stage8AmbientMath.h          # 높이·AO·octave CPU 기준
│  ├─ Stage13ScaleMath.h           # 13-2 런타임/CPU 공유 meter 상사 변환 기준
│  ├─ Stage13OpenWorldMath.h       # 13-3 실제 km 시작값·파장·step budget 기준
│  ├─ Stage13NoiseVolumeMath.h     # 13-4 Texture3D 규격·periodic CPU 기준
│  ├─ Stage13CameraPresets.h       # 단일 씬 F5~F8 위치/타깃 기준
│  ├─ Stage13SceneMath.h           # 지면·건물·50km·입력·이동·숫자 매핑 기준
│  ├─ Stage13OpticsLightingMath.h  # 13-5 km 광학·Light 후보·Detail LOD CPU 기준
│  ├─ Stage9OptimizationMath.h     # 가변 View 구간·coarse rewind·cone weight CPU 기준
│  ├─ Stage13WeatherShapeMath.h    # 13-4B Weather 두께 분포·타입 프로파일 CPU 기준
│  ├─ NoiseVolumeCache.h           # 테스트 전용 3D noise cache·hash 검증
│  ├─ Stage13SimilarityDiagnostics.h # float GPU 프레임의 배율별 오차·연결 블록 수치 비교
│  ├─ Stage13CloudDomainMath.h     # Y 평면층 교차·거리 fade CPU 기준
│  ├─ FrameProfiler.*              # CPU 시간·8-slot GPU timestamp, Resolve/Cloud Composite 분리
│  ├─ Stage1VolumeMath.h           # AABB·상수 밀도 CPU 테스트 기준
│  ├─ Stage2NoiseMath.h            # value noise·coverage CPU 테스트 기준
│  ├─ Stage3HeightMath.h           # 높이 fraction·profile CPU 테스트 기준
│  ├─ Stage4DetailMath.h           # Detail 좌표·침식·샘플 생략 CPU 기준
│  ├─ Stage5WeatherMath.h          # Weather UV·coverage·type profile CPU 기준
│  └─ Stage6LightMath.h            # 광학 깊이·단일 산란 CPU 기준
├─ shaders/
│  ├─ DiagnosticScene.hlsl         # 불투명 평면·박스
│  ├─ Fullscreen.hlsl              # SV_VertexID 풀스크린 삼각형
│  ├─ VolumetricClouds.hlsl        # 직접·환경·다중 산란 적분과 단계 10 MRT 출력
│  ├─ CloudUpsample.hlsl           # Full-resolution Spatial cloud color/T+aux pair 복원
│  ├─ CloudTemporalResolve.hlsl    # Full D32 source 검증·재투영·history pair 생성
│  ├─ CloudComposite.hlsl          # resolve pair+장면/대기 합성, 일반/no-rim PS와 history 후 NTE rim
│  ├─ CloudRimParameters.hlsli     # CPU와 공유하는 48바이트 CloudRimCB(pass-local b10)
│  ├─ CloudSpatialResolve.hlsli    # Spatial/Temporal 공용 class·plane hard rejection과 Sky soft weight
│  ├─ Stage15CaptureAccumulate.hlsl # Native 1080p Tone Map 전 4-sample HDR running average 입력
│  ├─ CloudDeepShadow.hlsl         # Near/Far Base-only 누적 광학 깊이 compute
│  ├─ NoiseLab.hlsl                # XY/XZ/YZ 고정 단면 픽셀 셰이더
│  ├─ NoiseVolume.hlsl             # Base/Detail periodic Texture3D compute 생성
│  ├─ NoiseVolumeParameters.hlsli  # CPU와 공유하는 96바이트 NoiseVolumeCB(b6)
│  ├─ CloudParameters.hlsli        # CPU와 공유하는 128바이트 CloudCB
│  ├─ CloudLodParameters.hlsli     # CPU와 공유하는 16바이트 CloudLodCB(b8)
│  ├─ OptimizationParameters.hlsli # CPU와 공유하는 64바이트 OptimizationCB(b9), offset 44 padding
│  ├─ Stage10UpsamplingParameters.hlsli # CPU와 공유하는 32바이트 UpsamplingCB(b10)
│  ├─ Stage11TemporalParameters.hlsli # CPU와 공유하는 144바이트 TemporalCB(b11)
│  ├─ Stage12ShadowParameters.hlsli # CPU와 공유하는 160바이트 ShadowCB(b12)
│  ├─ Stage12Shadow.hlsli          # t6/t7 cache 조회·cascade·표면 계수
│  ├─ Stage14Atmosphere.hlsli      # b13/t8~t13/s3와 공통 태양·하늘·Aerial 조회/합성
│  ├─ Stage14AtmosphereLut.hlsl    # 여섯 RGBA16F compute LUT 생성
│  ├─ Stage14ToneMap.hlsl          # ACES/white balance/sRGB/dither와 LUT fullscreen debug
│  ├─ CloudShapeParameters.hlsli   # CPU와 공유하는 112바이트 CloudShapeCB(b7)
│  ├─ CloudDomainParameters.hlsli  # CPU와 공유하는 32바이트 DomainCB(b5), offset 24 LUT 고도
│  ├─ CloudAdvection.hlsli         # Physical Weather/Base/Detail 공통 수평 Bulk 이동
│  ├─ Noise.hlsli                  # 구름과 Lab 공용 Base/Detail density 라이브러리
│  ├─ Weather.hlsli                # t2 Weather 샘플·구름 종류 높이 프로파일
│  ├─ LightParameters.hlsli        # CPU와 공유하는 80바이트 LightCB(b3)
│  ├─ CloudLighting.hlsli          # 태양 광학 깊이·직접 단일 산란
│  ├─ PhaseFunction.hlsli          # 전방·후방 HG와 Phase Factor
│  ├─ EnvironmentParameters.hlsli  # CPU와 공유하는 80바이트 EnvironmentCB(b4)
│  ├─ CloudEnvironment.hlsli       # 하늘·지면·AO·다중 산란 근사
│  └─ Ray.hlsli                    # 안전한 AABB 교차
├─ tests/
│  ├─ FoundationTests.cpp          # 카메라 역투영 CPU 회귀 테스트
│  ├─ Stage1VolumeMathTests.cpp    # AABB·Beer-Lambert 회귀 테스트
│  ├─ Stage2NoiseMathTests.cpp     # value noise·coverage·wind 회귀 테스트
│  ├─ Stage3HeightMathTests.cpp    # 높이 fraction·fade·밀도 회귀 테스트
│  ├─ Stage4DetailMathTests.cpp    # Detail erosion·sample skip 회귀 테스트
│  ├─ Stage5WeatherMathTests.cpp   # Weather Map·UV·cloud type 회귀 테스트
│  ├─ Stage6LightMathTests.cpp     # 태양 투과율·단일 산란 회귀 테스트
│  ├─ FrameProfilerMathTests.cpp   # EMA·FPS·입력 검증 회귀 테스트
│  ├─ Stage7PhaseMathTests.cpp     # 방향·HG·프리셋·안정성 회귀 테스트
│  ├─ Stage8AmbientMathTests.cpp   # 환경광·AO·octave 회귀 테스트
│  ├─ Stage9OptimizationMathTests.cpp # preset ABI·가변 step·cone 구간 회귀
│  ├─ Stage10UpsamplingMathTests.cpp # 크기·UV·대표 깊이·joint weight 회귀
│  ├─ Stage11TemporalMathTests.cpp # 4-phase·D32 평면·3×3 depth·재투영·clip·EMA 회귀
│  ├─ Stage12ShadowMathTests.cpp   # basis·ray UV·snap·cascade·slice·cache ABI 회귀
│  ├─ Stage14AtmosphereMathTests.cpp # 대기·LUT UV·시간·지면·ACES/white balance 회귀
│  ├─ Stage13ScaleMathTests.cpp    # 1×~1000× 공간 단위 상사 불변식 테스트
│  ├─ Stage13OpenWorldMathTests.cpp # 13-3 실제값·View/Light budget·fade 테스트
│  ├─ Stage13NoiseVolumeMathTests.cpp # 13-4 규격·주기·cache CPU 테스트
│  ├─ Stage13WeatherShapeMathTests.cpp # 13-4B 분포·두께·프로파일·주파수 테스트
│  ├─ Stage13CameraControlMathTests.cpp # F5~F8 교차·FPS 회전·휠 이동 테스트
│  ├─ Stage13SceneMathTests.cpp    # 단일 씬 크기·입력·이동·디버그 매핑 회귀
│  ├─ Stage13OpticsLightingMathTests.cpp # km τ·Light 후보·Detail LOD 회귀
│  ├─ CloudAppearanceTests.cpp     # 13-4E 점유율·preset·density·Custom JSON 회귀
│  ├─ CloudFormationPresetStoreTests.cpp # 8슬롯 독립성·원자 저장·fallback·migration·Cirrus texture 불변
│  ├─ Stage15PresetMathTests.cpp   # resolver·sanitize·소유권·Weather·b7/Cirrus 수학 계약
│  ├─ DeveloperUiSettingsTests.cpp # DPI×Zoom, 5% 양자화와 schema 1 원자 저장 회귀
│  ├─ Stage13SimilarityCameraMathTests.cpp # double 기준과 float 역 VP 레이 정밀도 비교
│  └─ Stage13CloudDomainMathTests.cpp # 평면층 교차·깊이 제한·거리 fade 회귀 테스트
├─ notes/                           # 로컬 단계 학습·사용자 검증 문서와 개인 메모, Git 제외
├─ third_party/imgui/              # Win32/DX11 개발 UI submodule
├─ captures/noise-lab/             # 로컬 PNG/JSON 출력, Git 제외
│  ├─ developer-ui.json            # 렌더 snapshot과 분리된 UI Zoom schema 1
│  └─ cloud-presets/               # formation 전체 snapshot schema 1, 슬롯별 독립 원자 파일
│     ├─ concepts/                 # urban/meadow/desert/snow 네 F4 override
│     ├─ types/                    # stratus/cumulus/cirrus 세 F1 override
│     └─ custom.json               # 내장 fallback이 없는 독립 Custom
└─ build/                          # CMake 산출물과 config별 shader-cache/, Git 제외
```

`doc/Volumetric Cloud 프로젝트 단계별 구현 계획서.pdf`는 AI 에이전트 로컬 참고 자료이며 `.gitignore`로 제외한다.
