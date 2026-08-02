# 폴더 / 파일 구조

```text
VolumetricCloud/
├─ CMakeLists.txt                  # 앱과 FoundationMath 테스트 빌드
├─ README.md / AGENTS.md           # 사용자 안내 / AI 작업 규칙
├─ doc/
│  ├─ ARCHITECTURE.md              # 단계 0 파이프라인과 상수버퍼
│  ├─ RAYMARCHING.md               # 월드 레이·깊이 역투영 수식
│  ├─ ROADMAP.md                   # 사용자 승인 기반 0~15단계
│  ├─ FOLDER_STRUCTURE.md
│  ├─ CONTRIBUTING.md
│  └─ changes/
│     └─ feature-rebuild-foundation.md
├─ src/
│  ├─ main.cpp
│  ├─ Window.* / Camera.*          # 입력, 오빗과 고정 검증 카메라
│  ├─ Renderer.*                   # 진단 장면, 깊이, 풀스크린 합성
│  └─ CloudParameters.h            # CPU/HLSL 공유 구름 설정
├─ shaders/
│  ├─ DiagnosticScene.hlsl         # 불투명 평면·박스
│  ├─ Fullscreen.hlsl              # SV_VertexID 풀스크린 삼각형
│  ├─ VolumetricClouds.hlsl        # 단계 0 진단·합성 패스
│  └─ Ray.hlsli                    # 단계 1 이후 교차 함수 자리
├─ tests/
│  └─ FoundationTests.cpp          # 카메라 역투영 CPU 회귀 테스트
├─ notes/                           # 로컬 단계 학습·사용자 검증 문서와 개인 메모, Git 제외
├─ third_party/                    # 현재 런타임 의존성 없음
└─ build/                          # CMake 산출물, Git 제외
```

`doc/Volumetric Cloud 프로젝트 단계별 구현 계획서.pdf`는 AI 에이전트 로컬 참고 자료이며 `.gitignore`로 제외한다.
