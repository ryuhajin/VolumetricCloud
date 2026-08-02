# VolumetricCloud

DirectX 11 + HLSL로 볼류메트릭 클라우드를 기능별로 검증하며 다시 구축하는 학습 프로젝트입니다.

현재는 **재구축 단계 0**으로, 구름 밀도와 레이 마칭을 추가하기 전에 다음 기반을 검증합니다.

- 평면과 두 박스로 구성된 불투명 진단 장면
- 샘플 가능한 Scene Depth와 월드 위치 복원
- 화면 UV에서 월드 공간 카메라 레이 생성
- 별도 풀스크린 구름 결과와 배경 합성 경로
- CPU/HLSL 공통 `CloudParameters`

## 빌드와 실행

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\VolumetricCloud.exe
```

## 단계 0 조작

| 입력 | 동작 |
|---|---|
| 마우스 왼쪽 드래그 | 오빗 회전 |
| 휠 | 줌 |
| `0` | 최종 합성과 반투명 진단 원판 |
| `1` | 월드 레이 방향 RGB |
| `2` | 복원된 월드 거리 |
| `3` | 복원된 월드 위치 밴드 |
| `4` | 화면 UV |
| `F5`~`F7` | 고정 검증 카메라 |

단계 0에서는 AABB 교차, 밀도, 3D noise와 조명을 의도적으로 구현하지 않습니다. 사용자 렌더 승인 후 단계 1에서 상수 밀도 AABB를 추가합니다.

## 자동 검사

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`FoundationMath`는 카메라 역투영을, `FoundationSmoke`는 숨김 D3D11 창에서 런타임 셰이더 컴파일과 실제 3프레임 렌더를 검사합니다.

자세한 구조와 단계는 [아키텍처](doc/ARCHITECTURE.md), [레이·깊이 복원](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요.
