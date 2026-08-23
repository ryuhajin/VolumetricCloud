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

> AI 에이전트는 작업 시작 전 [AGENTS.md](../AGENTS.md)를 먼저 읽고, 끝낼 때 위 동기화를 확인하세요.

## 2. 브랜치 규칙

| 브랜치 | 용도 |
|--------|------|
| `main` | GitHub 기본 브랜치이자 승인된 안정 상태. 직접 commit/push 금지, PR merge commit만 허용 |
| `feature/stage<번호>-<설명>` | 로드맵 단계 전용 작업 (예: `feature/stage12-shadow`) |
| `feature/<설명>` | 단계와 무관한 새 기능 |
| `fix/<설명>` | 버그 수정 (예: `fix/resize-crash`) |
| `fix/rollback-stage<번호>` | 승인 태그에서 시작하는 복구 작업 |
| `doc/<설명>` | 문서만 수정 (예: `doc/raymarching-math`) |

- 로드맵의 각 단계는 최신 `origin/main`에서 만든 전용 `feature/stage<번호>-<설명>` 브랜치에서만
  진행합니다. Stage 12/14/15의 이름은 각각 `feature/stage12-shadow`,
  `feature/stage14-atmosphere-integration`, `feature/stage15-final-quality`입니다.
- 단계 코드·테스트·공식 문서는 같은 단계 브랜치에 둡니다. 다른 단계 구현을 섞지 않습니다.
- 작업 완료 뒤 브랜치를 최신 `origin/main` 기준으로 갱신하고 Debug/Release 빌드, 전체 CTest,
  해당 GPU smoke와 사용자 렌더 승인을 다시 확인합니다.
- GitHub PR은 **merge commit** 방식으로만 병합합니다. squash/rebase merge와 main 직접 push는
  사용하지 않습니다.
- 병합된 `origin/main` commit에 annotated `stage<번호>-approved` 태그를 만들고 원격에 push합니다.
  태그의 dereference SHA와 `origin/main` SHA가 같은지 확인한 뒤 단계 브랜치를 로컬·원격에서
  삭제합니다. 기존 `stage11`은 이름을 바꾸지 않는 역사적 예외입니다.

### 2.1 `main` 보호 계약

GitHub의 기본 브랜치는 `main`이며 다음 branch protection을 유지합니다.

- PR 필수, 필수 승인 리뷰 수 0, conversation resolution 필수
- 관리자에게도 규칙 적용
- force push와 branch deletion 금지
- merge commit 허용, squash/rebase merge와 linear history 요구는 비활성
- CI 도입 전에는 required status check를 지정하지 않고 로컬 검증 결과를 PR에 기록

### 2.2 승인 태그와 롤백

- 승인 태그는 이동하거나 덮어쓰지 않습니다. 같은 이름이 이미 있으면 작업을 중단합니다.
- 과거 버전 확인은 `git switch --detach <승인 태그>`로 수행합니다.
- 실제 롤백은 이전 승인 태그에서 `fix/rollback-stage<번호>`를 만들고 새 PR로 병합합니다.
- `main`에 `reset --hard` 또는 force push를 사용해 이력을 되감지 않습니다.

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
