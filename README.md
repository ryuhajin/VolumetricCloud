# VolumetricCloud

DirectX11 + HLSL로 **레이마칭(ray marching)** 을 학습하고, 최종적으로 **볼류메트릭 클라우드**를
렌더링하는 것을 목표로 하는 학습용 프로젝트입니다.

> **현재 단계 — 2단계:** noise도 light도 없이, 공중에 떠 있는 **상수 밀도 박스 볼륨(AABB)** 을
> 레이마칭으로 렌더링합니다. 1단계의 반투명 안개 구에서 출발해, 볼륨의 경계와 교차 계산을
> 일반화하는 중입니다.

## 특징

- 정점 버퍼 없이 **풀스크린 삼각형 1개**를 그리고, **픽셀 셰이더에서 레이마칭**
- **해석적 ray-box(AABB) 교차** + **Beer-Lambert** 밀도 적분으로 반투명 박스 볼륨
- **마우스 오빗 카메라** (드래그=회전, 휠=줌)
- **HLSL 핫-리로드** — 실행 중 `shaders/`의 셰이더를 저장하면 다음 프레임에 자동 재컴파일
- 외부 의존성 없음 (DirectX 11 / Windows SDK만 사용)

## 요구 사항

- Windows 10/11
- Visual Studio 2022 (또는 MSVC 빌드 도구) + Windows 10 SDK
- CMake 3.20+

## 빌드 & 실행

```powershell
git clone <this-repo-url>
cd VolumetricCloud

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

.\build\Debug\VolumetricCloud.exe
```

> Visual Studio로 열고 싶다면, CMake가 `build/VolumetricCloud.sln`을 함께 생성합니다.

### 조작

| 입력 | 동작 |
|------|------|
| 마우스 왼쪽 드래그 | 볼륨 주위를 궤도 회전 |
| 마우스 휠 | 줌 인 / 아웃 |

개발 중에는 소스 트리의 `shaders/`를 우선 읽습니다. 실행 중 HLSL을 저장하면 자동으로 다시
컴파일되며, 컴파일 실패 시에는 기존 정상 셰이더가 유지됩니다.

실행하면 하늘 그라데이션 배경 위에, 가장자리가 부드럽게 비치는 반투명 회색 박스 볼륨이 보입니다.
(볼륨 내부를 길게 통과할수록 진하고, 짧게 통과할수록 투명 — 통과 거리에 따른 Beer-Lambert 누적 차이)

## 프로젝트 구조

```
src/        C++ 소스 (main / Window / Camera / Renderer)
shaders/    HLSL (Fullscreen.hlsl, VolumetricClouds.hlsl, Ray.hlsli)
doc/        설계·학습 문서 (한국어)
AGENTS.md   AI 에이전트용 안내
```

자세한 내용은 [doc/FOLDER_STRUCTURE.md](doc/FOLDER_STRUCTURE.md)와
[doc/ARCHITECTURE.md](doc/ARCHITECTURE.md)를 참고하세요.

## 문서

- [아키텍처](doc/ARCHITECTURE.md) — 모듈 구조 · 렌더 파이프라인 · 상수버퍼
- [레이마칭 설명](doc/RAYMARCHING.md) — 레이 생성 · ray-box 교차 · Beer-Lambert
- [로드맵](doc/ROADMAP.md) — 구 → 박스 → noise → light → cloud
- [작업 규칙](doc/CONTRIBUTING.md) — 문서/브랜치/커밋 규칙

## 로드맵 (요약)

1. ✅ **반투명 안개 구** (완료)
2. 🚧 **형상 일반화** (박스 / SDF, 현재)
3. ⬜ Noise로 구름 형태
4. ⬜ Light (산란 / 그림자)
5. ⬜ 구름 완성 / 최적화

전체 계획은 [doc/ROADMAP.md](doc/ROADMAP.md) 참고.

## 라이선스

학습용 프로젝트입니다. 자유롭게 참고하세요.
