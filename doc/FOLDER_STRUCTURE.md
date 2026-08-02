# 폴더 구조

```text
VolumetricCloud/
├─ CMakeLists.txt
├─ README.md / AGENTS.md
├─ assets/noise-cache/bundle/       # 배포 기본 .cso, manifest, 128³/128³ volume, 512² weather/placement
├─ doc/
│  ├─ ARCHITECTURE.md
│  ├─ RAYMARCHING.md
│  ├─ CLOUD_PLACEMENT_AND_VERTICAL_SHAPING.md
│  ├─ FOLDER_STRUCTURE.md
│  ├─ ROADMAP.md
│  ├─ changes/                     # 브랜치별 설계 판단·구현·검증 개발 기록
│  │  ├─ TEMPLATE.md
│  │  └─ fix-placement-boundary-artifacts.md
│  └─ CONTRIBUTING.md
├─ notes/interview/                 # 개인 면접 메모(로컬 전용, Git 제외)
├─ src/
│  ├─ main.cpp
│  ├─ Window.* / Camera.*
│  ├─ Renderer.*
│  ├─ CloudParameters.h
│  ├─ DebugUI.*
│  └─ NoiseCacheManager.*
├─ shaders/
│  ├─ Fullscreen.hlsl
│  ├─ VolumetricClouds.hlsl
│  ├─ TemporalResolve.hlsl
│  ├─ Composite.hlsl
│  ├─ NoisePreview.hlsl
│  ├─ NoiseVolumeCS.hlsl
│  ├─ CloudNoise.hlsli
│  ├─ CloudAtmosphere.hlsli
│  └─ Ray.hlsli
└─ third_party/imgui/               # Dear ImGui Git submodule + Win32/DX11 backend
```

사용자 캐시는 저장소 밖 `%LOCALAPPDATA%\VolumetricCloud\cache\bundle`에 저장된다. 사용자 프리셋은 `%LOCALAPPDATA%\VolumetricCloud\cloud-presets.ini`에 저장된다. `build/`는 생성물이며 Git에서 제외한다.

`doc/changes/`에는 코드를 수정하는 브랜치마다 Markdown 문서 하나를 둔다. 브랜치 이름의 `/`는 `-`로 바꾸며, 이 문서는 결과만 나열하지 않고 문제를 발견한 근거와 선택하지 않은 대안까지 기록한다.

`notes/interview/`는 공개 개발 기록을 바탕으로 개인 면접 답변을 정리하는 로컬 전용 폴더다. `.gitignore`로 제외하며 저장소나 PR에 포함하지 않는다.
