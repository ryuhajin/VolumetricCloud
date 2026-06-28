# 폴더 / 파일 구조

프로젝트의 모든 폴더와 파일의 역할을 한눈에 정리한 문서입니다.
**새 파일이나 폴더를 추가하면 이 문서에도 한 줄 추가하세요.** (→ [CONTRIBUTING.md](CONTRIBUTING.md))

```
VolumetricCloud/
├── CMakeLists.txt          # CMake 빌드 정의 (d3d11/dxgi/d3dcompiler 링크, 셰이더 경로 정의/복사, /utf-8)
├── README.md               # 프로젝트 개요 · 빌드 방법 · 로드맵 (GitHub 표지)
├── AGENTS.md               # AI 에이전트용 진입점 (아키텍처 요약 + 규칙 링크)
├── .gitignore              # 빌드 산출물 · VS 생성물 제외
├── .gitattributes          # 줄바꿈 정규화 · HLSL linguist 힌트
│
├── doc/                    # 학습/설계 문서 (한국어)
│   ├── ARCHITECTURE.md     # 모듈 구조 · 렌더 파이프라인 · 데이터 흐름
│   ├── FOLDER_STRUCTURE.md # (이 문서) 폴더/파일 역할 목록
│   ├── RAYMARCHING.md      # 레이마칭 수식 설명 (레이 생성 · 교차 · Beer-Lambert)
│   ├── ROADMAP.md          # 단계별 로드맵 (구 → 박스 → noise → light → cloud)
│   └── CONTRIBUTING.md     # 문서 수정 규칙 · 브랜치 규칙 · 커밋 규칙
│
├── src/                    # C++ 소스
│   ├── main.cpp            # 진입점(wWinMain): 창·카메라·렌더러 생성 및 메인 루프
│   ├── Window.h / .cpp     # Win32 윈도우 생성 · 마우스 입력 → 카메라/렌더러 전달
│   ├── Camera.h / .cpp     # 마우스 오빗 카메라 → view/proj/invViewProj 계산
│   └── Renderer.h / .cpp   # D3D11 device/swapchain/RTV, 셰이더 컴파일/핫-리로드, 드로우
│
├── shaders/                # HLSL 셰이더 (런타임 컴파일/핫-리로드, 빌드 시 exe 옆으로도 복사)
│   ├── Fullscreen.hlsl     # 풀스크린 삼각형 정점 셰이더 (정점 버퍼 없음)
│   ├── VolumetricClouds.hlsl # 박스 볼륨 레이마칭 픽셀 셰이더
│   └── Ray.hlsli             # RaySphere/RayBox 등 레이 교차 함수 모음
│
├── third_party/            # 외부 라이브러리 자리 (현재 비어 있음, 추후 ImGui 등)
│
└── build/                  # CMake 빌드 산출물 (git 제외)
```

## 핵심 흐름 요약

`main.cpp` → `Window`(입력) + `Camera`(행렬) → `Renderer`(상수버퍼 업로드) →
`Fullscreen.hlsl`(레이 준비) → `VolumetricClouds.hlsl`(+ `Ray.hlsli`) → 화면.

자세한 구조는 [ARCHITECTURE.md](ARCHITECTURE.md)를 참고하세요.
