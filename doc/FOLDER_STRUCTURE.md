# 폴더 구조

```text
VolumetricCloud/
├─ CMakeLists.txt
├─ README.md / AGENTS.md
├─ assets/noise-cache/bundle/       # 배포 기본 .cso, manifest, 128³/64³ volume
├─ doc/
│  ├─ ARCHITECTURE.md
│  ├─ RAYMARCHING.md
│  ├─ FOLDER_STRUCTURE.md
│  ├─ ROADMAP.md
│  ├─ changes/                     # 브랜치별 설계 판단·구현·검증 개발 기록
│  │  └─ TEMPLATE.md
│  └─ CONTRIBUTING.md
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
│  ├─ NoisePreview.hlsl
│  ├─ NoiseVolumeCS.hlsl
│  ├─ CloudNoise.hlsli
│  └─ Ray.hlsli
└─ third_party/imgui/               # Dear ImGui Git submodule + Win32/DX11 backend
```

사용자 캐시는 저장소 밖 `%LOCALAPPDATA%\VolumetricCloud\cache\bundle`에 저장된다. 사용자 프리셋은 `%LOCALAPPDATA%\VolumetricCloud\cloud-presets.ini`에 저장된다. `build/`는 생성물이며 Git에서 제외한다.

`doc/changes/`에는 코드를 수정하는 브랜치마다 Markdown 문서 하나를 둔다. 브랜치 이름의 `/`는 `-`로 바꾸며, 이 문서는 결과만 나열하지 않고 문제를 발견한 근거와 선택하지 않은 대안까지 기록한다.
