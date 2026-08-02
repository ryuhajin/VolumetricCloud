# VolumetricCloud

DirectX 11 + HLSL로 볼류메트릭 클라우드를 기능별로 검증하며 다시 구축하는 학습 프로젝트입니다.

현재는 **재구축 단계 1**로, 단계 0에서 승인된 레이·깊이 기반 위에 다음 기능을 검증합니다.

- 평면과 두 박스로 구성된 불투명 진단 장면
- 샘플 가능한 Scene Depth와 월드 위치 복원
- 화면 UV에서 월드 공간 카메라 레이 생성
- 안전한 slab 방식 Ray-AABB 교차와 카메라 내부 처리
- Scene Depth보다 뒤쪽 안개를 제외하는 구간 제한
- 상수 밀도 Beer-Lambert 적분과 별도 풀스크린 합성
- 디버그 모드 0~9와 볼륨·step 검증 프리셋

## 빌드와 실행

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\VolumetricCloud.exe
```

## 단계 1 조작

| 입력 | 동작 |
|---|---|
| 마우스 왼쪽 드래그 | 오빗 회전 |
| 휠 | 줌 |
| `0` | 실제 AABB 안개와 장면 합성 |
| `1` | 월드 레이 방향 RGB |
| `2` | 복원된 월드 거리 |
| `3` | 복원된 월드 위치 밴드 |
| `4` | 화면 UV |
| `5` | AABB 진입 거리 |
| `6` | Scene Depth로 제한한 이탈 거리 |
| `7` | 실제 view step 수 |
| `8` | 최종 투과율 |
| `9` | hit 영역의 상수 밀도 |
| `F5`~`F7` | 외부 고정 검증 카메라 |
| `F8` | AABB 내부 카메라 |
| `Q` / `W` / `E` | 기본 / 얇은 Z / 두꺼운 Z 볼륨 |
| `R` / `T` | fine 0.025m / coarse 0.5m step |

현재는 밀도를 모든 위치에서 0.35로 고정하고 산란색도 고정합니다. 3D noise, 태양광, phase function과 early exit는 이후 단계까지 의도적으로 구현하지 않습니다.

## 자동 검사

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`FoundationMath`/`FoundationSmoke`는 단계 0 회귀를 검사합니다. `Stage1VolumeMath`는 AABB 경계 조건과 분석적 Beer-Lambert 값을, `Stage1Smoke`는 숨김 D3D11 창에서 디버그 모드 0~9의 실제 렌더 경로를 검사합니다.

자세한 구조와 단계는 [아키텍처](doc/ARCHITECTURE.md), [AABB 레이 마칭](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요.
