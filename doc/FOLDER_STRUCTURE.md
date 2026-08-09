# 폴더 / 파일 구조

```text
VolumetricCloud/
├─ CMakeLists.txt
├─ README.md / AGENTS.md
├─ doc/
│  ├─ ARCHITECTURE.md / RAYMARCHING.md / PERFORMANCE.md
│  ├─ ROADMAP.md / CONTRIBUTING.md / FOLDER_STRUCTURE.md
│  ├─ VOLUMETRIC_CLOUD_IMPLEMENTATION_PLAN.md # PDF 유지보수 원본
│  ├─ Volumetric Cloud 프로젝트 단계별 구현 계획서.pdf
│  └─ changes/feature-rebuild-foundation.md
├─ src/
│  ├─ main.cpp / Window.* / Camera.* / Renderer.*
│  ├─ CloudParameters.h              # 128B CloudCB와 평면층 기본값
│  ├─ LightParameters.h / EnvironmentParameters.h
│  ├─ OptimizationParameters.h / FrameProfiler.*
│  ├─ NoiseLab.* / WeatherMap.*
│  ├─ Stage1VolumeMath.h ... Stage9OptimizationMath.h
│  └─ Stage13CloudLayerMath.h        # 평면층·거리 fade·Weather 기준
├─ shaders/
│  ├─ VolumetricClouds.hlsl          # 평면층 View Ray 적분과 합성
│  ├─ CloudLighting.hlsli            # 평면층 Light Ray 적분
│  ├─ CloudParameters.hlsli / Noise.hlsli / Weather.hlsli
│  ├─ NoiseLab.hlsl / DiagnosticScene.hlsl / Fullscreen.hlsl
│  └─ Ray.hlsli                      # 단계 1 역사적 AABB 회귀용
├─ tests/
│  ├─ FoundationTests.cpp / Stage1...Stage9...Tests.cpp
│  └─ Stage13CloudLayerMathTests.cpp
├─ tools/
│  ├─ Run-Stage9Benchmark.ps1 / Compare-Stage9Performance.ps1
│  └─ Build-ImplementationPlanPdf.py
├─ notes/                             # 로컬 단계 학습·사용자 검증 기록, Git 제외
├─ captures/noise-lab/                # schema 13 PNG/JSON, Git 제외
├─ captures/performance/              # 벤치마크 번들, Git 제외
├─ third_party/imgui/
└─ build/                             # CMake 산출물, Git 제외
```

`CloudParameters.h`와 `CloudParameters.hlsli`, 그리고 `doc/ARCHITECTURE.md`의 CloudCB 표는 항상
같이 수정한다. `VOLUMETRIC_CLOUD_IMPLEMENTATION_PLAN.md`가 단계별 계획서의 정본이며 생성 스크립트로
동일 폴더의 로컬 PDF를 갱신한다. PDF 자체는 `.gitignore` 대상이고 Markdown과 생성기를 버전 관리한다.
