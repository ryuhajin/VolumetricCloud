# 기여 / 작업 규칙

이 프로젝트는 학습용이며, **사람과 AI 에이전트가 함께** 작업합니다.
일관성을 위해 아래 규칙을 따릅니다.

## 1. 문서 수정 규칙

문서는 코드와 항상 동기화되어야 합니다.

- **코드 동작을 바꾸면** 관련 `doc/*.md`를 **같은 변경(커밋/PR)에서** 갱신합니다.
- **새 파일/폴더를 추가하면** [FOLDER_STRUCTURE.md](FOLDER_STRUCTURE.md)에 한 줄 추가합니다.
- **새 모듈·렌더 단계·상수버퍼 필드를 추가/변경하면** [ARCHITECTURE.md](ARCHITECTURE.md)를 갱신합니다.
  - 특히 상수버퍼(`CameraCB` ↔ `cbCamera`)는 C++·HLSL·문서 **세 곳을 동시에** 맞춥니다.
- **로드맵 단계를 완료하면** [ROADMAP.md](ROADMAP.md)의 체크박스를 채웁니다.
- 알고리즘/수식을 바꾸면 [RAYMARCHING.md](RAYMARCHING.md)를 갱신합니다.
- 문서 언어는 **한국어**, 코드 주석도 한국어를 기본으로 합니다.
- 코드를 바꾸는 모든 브랜치는 `changes/` 아래에 브랜치 작업 기록을 하나 둡니다.
  - 파일명은 브랜치명의 `/`를 `-`로 바꿉니다. 예: `feature/cloud-quality` → `changes/feature-cloud-quality.md`
  - 브랜치를 시작할 때 [TEMPLATE.md](changes/TEMPLATE.md)를 복사하고, 구현 중에는 살아 있는 문서처럼 계속 갱신합니다.
  - 각 코드 커밋에는 그 코드의 변경 이유와 실제 수정 내용을 작업 기록에 함께 반영합니다.
  - 화면 품질은 자동 승인하지 않습니다. 날짜, 프리셋, 카메라, 관찰 결과를 기록하고 사용자가 최종 승인합니다.
  - 필수 항목은 목표와 배경, 기존 문제, 근거, 대안, 해결 방법, 실제 변경, 영향, 검증, 후속 과제입니다.

> AI 에이전트는 작업 시작 전 [AGENTS.md](../AGENTS.md)를 먼저 읽고, 끝낼 때 위 동기화를 확인하세요.

## 2. 브랜치 규칙

| 브랜치 | 용도 |
|--------|------|
| `main` | 항상 빌드 가능한 안정 상태. 직접 푸시 지양, PR로만 병합 |
| `feature/<설명>` | 새 기능 (예: `feature/worley-noise`, `feature/box-volume`) |
| `fix/<설명>` | 버그 수정 (예: `fix/resize-crash`) |
| `doc/<설명>` | 문서만 수정 (예: `doc/raymarching-math`) |

- 로드맵의 각 단계는 별도 `feature/` 브랜치에서 진행합니다.
- 작업이 끝나면 PR을 만들고, 빌드가 통과하는지 확인 후 `main`에 병합합니다.
- 브랜치 작업 기록이 없거나 현재 코드와 맞지 않으면 작업 완료로 간주하지 않습니다.

## 3. 커밋 규칙 (Conventional Commits)

`<type>: <요약>` 형식을 사용합니다.

| type | 사용 시점 |
|------|-----------|
| `feat` | 새 기능 |
| `fix` | 버그 수정 |
| `docs` | 문서만 변경 |
| `refactor` | 동작 변화 없는 구조 개선 |
| `build` | 빌드/CMake/의존성 변경 |
| `perf` | 성능 개선 |

예시:
```
feat: add worley noise to cloud density
fix: prevent crash on window minimize
docs: explain beer-lambert integration
```

- **셰이더(HLSL)를 바꾼 커밋**은 본문에 시각적 결과를 한 줄 요약합니다.
  (예: `구 가장자리가 더 부드러워짐`)
- 한 커밋은 한 가지 일만 담습니다.

## 4. 빌드 확인

PR 전 아래가 통과하는지 확인합니다.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

빌드 에러 0, 셰이더 런타임 컴파일 성공(실행 시 오류 MessageBox 없음)이어야 합니다.
