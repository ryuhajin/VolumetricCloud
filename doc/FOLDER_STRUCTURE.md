# 폴더 / 파일 구조

```text
VolumetricCloud/
├─ CMakeLists.txt                  # 앱과 단계별 수치·D3D smoke 테스트 빌드
├─ README.md / AGENTS.md           # 사용자 안내 / AI 작업 규칙
├─ doc/
│  ├─ ARCHITECTURE.md              # 현재 파이프라인과 상수버퍼
│  ├─ RAYMARCHING.md               # 교차·적분·noise 밀도 수식
│  ├─ PERFORMANCE.md               # 동일 조건 성능 측정 절차와 지표 범위
│  ├─ ROADMAP.md                   # 사용자 승인 기반 0~15단계
│  ├─ FOLDER_STRUCTURE.md
│  ├─ CONTRIBUTING.md
│  └─ changes/
│     └─ feature-rebuild-foundation.md
├─ src/
│  ├─ main.cpp
│  ├─ Window.* / Camera.*          # 입력, 오빗과 고정 검증 카메라
│  ├─ Renderer.*                   # 진단 장면, 깊이, 풀스크린 합성
│  ├─ NoiseLab.*                   # ImGui 단면 UI, GPU readback과 PNG/JSON 내보내기
│  ├─ WeatherMap.*                 # 256² CPU periodic Perlin RGBA 생성과 해시
│  ├─ CloudParameters.h            # CPU/HLSL 공유 구름 설정
│  ├─ LightParameters.h            # CPU/HLSL 공유 태양광 설정과 프리셋
│  ├─ FrameProfiler.*              # CPU 시간·8-slot 비동기 GPU timestamp 계측
│  ├─ Stage1VolumeMath.h           # AABB·상수 밀도 CPU 테스트 기준
│  ├─ Stage2NoiseMath.h            # value noise·coverage CPU 테스트 기준
│  ├─ Stage3HeightMath.h           # 높이 fraction·profile CPU 테스트 기준
│  ├─ Stage4DetailMath.h           # Detail 좌표·침식·샘플 생략 CPU 기준
│  ├─ Stage5WeatherMath.h          # Weather UV·coverage·type profile CPU 기준
│  └─ Stage6LightMath.h            # 광학 깊이·단일 산란 CPU 기준
├─ shaders/
│  ├─ DiagnosticScene.hlsl         # 불투명 평면·박스
│  ├─ Fullscreen.hlsl              # SV_VertexID 풀스크린 삼각형
│  ├─ VolumetricClouds.hlsl        # 단계 6 View/Light Ray 적분·합성 패스
│  ├─ NoiseLab.hlsl                # XY/XZ/YZ 고정 단면 픽셀 셰이더
│  ├─ CloudParameters.hlsli        # CPU와 공유하는 128바이트 CloudCB
│  ├─ Noise.hlsli                  # 구름과 Lab 공용 Base/Detail density 라이브러리
│  ├─ Weather.hlsli                # t2 Weather 샘플·구름 종류 높이 프로파일
│  ├─ LightParameters.hlsli        # CPU와 공유하는 48바이트 LightCB(b3)
│  ├─ CloudLighting.hlsli          # 태양 광학 깊이·직접 단일 산란
│  └─ Ray.hlsli                    # 안전한 AABB 교차
├─ tests/
│  ├─ FoundationTests.cpp          # 카메라 역투영 CPU 회귀 테스트
│  ├─ Stage1VolumeMathTests.cpp    # AABB·Beer-Lambert 회귀 테스트
│  ├─ Stage2NoiseMathTests.cpp     # value noise·coverage·wind 회귀 테스트
│  ├─ Stage3HeightMathTests.cpp    # 높이 fraction·fade·밀도 회귀 테스트
│  ├─ Stage4DetailMathTests.cpp    # Detail erosion·sample skip 회귀 테스트
│  ├─ Stage5WeatherMathTests.cpp   # Weather Map·UV·cloud type 회귀 테스트
│  ├─ Stage6LightMathTests.cpp     # 태양 투과율·단일 산란 회귀 테스트
│  └─ FrameProfilerMathTests.cpp   # EMA·FPS·입력 검증 회귀 테스트
├─ notes/                           # 로컬 단계 학습·사용자 검증 문서와 개인 메모, Git 제외
├─ third_party/imgui/              # Win32/DX11 개발 UI submodule
├─ captures/noise-lab/             # 로컬 PNG/JSON 출력, Git 제외
└─ build/                          # CMake 산출물, Git 제외
```

`doc/Volumetric Cloud 프로젝트 단계별 구현 계획서.pdf`는 AI 에이전트 로컬 참고 자료이며 `.gitignore`로 제외한다.
