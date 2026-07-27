# feature/cloud-thickness-debug-ui 변경 기록

## 1. 목표와 배경

- 광역 적운의 수직 규모를 직접 실험할 수 있도록 F1의 `Cloud thickness` 허용 범위를 3~16 km로 넓힌다.
- F2 HUD가 생긴 뒤에도 F1 Stats에 남아 있던 중복 계측값을 제거한다.
- 긴 파라미터 목록과 고정적으로 큰 Inspector 때문에 구름이 가려지는 문제를 줄이고, 작은 창에서도 모든 기능에 접근할 수 있게 한다.

## 2. 기존 구현과 관찰된 문제

- `cloudThickness`의 셰이더 값에는 8 km 상한이 없지만 F1 슬라이더가 0.5~8 km로 제한되어 8 km 이상의 실험이 불가능했다.
- Parameters 탭은 모든 슬라이더를 평면 목록으로 표시해 형태·조명·샘플링의 관계를 파악하기 어려웠다.
- F1 Stats의 FPS, CPU/GPU 시간, 마칭 step, 캐시 상태는 F2 HUD와 중복됐다.
- 최초 창 크기가 620×720이고 Noise Inspector가 항상 2열이어서 작은 창으로 줄이면 프리뷰와 조작부 사용성이 나빠졌다.
- Dear ImGui의 일반 트리 열림 상태는 프로세스의 window state storage에만 있으므로 stable label만으로는 재실행 후 카테고리 상태가 복원되지 않는다.

## 3. 원인 분석과 근거

- `CloudCB`와 HLSL은 `cloudThickness`를 float로 받아 `max(cloudThickness, 0.1)`만 적용한다. 따라서 이번 상한 변경은 상수버퍼·캐시·프리셋 형식 변경이 아니라 UI 입력 범위 변경이다.
- view march는 `clamp(viewSteps, 48, 128)`과 정적 128회 loop 상한을 사용한다. 두께가 커져도 loop 수가 자동 증가하지 않으므로 비용이 두께에 정비례하지는 않지만, 물리적 샘플 간격이 커지고 밀도가 있는 sample에서 light march가 더 자주 실행될 수 있다.
- 지역 두께는 다음 식을 사용한다.

```text
localThickness =
    cloudThickness * (1 + (weather.a * 2 - 1) * thicknessVariation)
```

- 예를 들어 기준 두께 16 km와 기본 Wide variation 0.42를 조합하면 지역 두께는 9.28~22.72 km다. UI 최대 variation 0.8에서는 3.2~28.8 km까지 가능하므로 16 km는 실제 지역 상한이 아니라 기준값 상한이다.

## 4. 검토한 대안

- HLSL loop 상한을 즉시 192/256으로 증가: 두꺼운 층의 디테일은 개선할 수 있지만 GPU 예산과 light march 품질을 함께 다시 정해야 하므로 사용자 화면에서 128-step 부족이 확인될 때 별도 작업으로 진행한다.
- 두께에 비례해 step을 자동 증가: 예측 가능한 성능 튜닝과 사용자의 직접 비교를 어렵게 하므로 채택하지 않았다.
- 모든 카테고리를 기본으로 열기: 기존 평면 목록과 가림 문제가 그대로 남아 채택하지 않았다.
- 카테고리 상태를 별도 설정 파일에 저장: 이미 사용하는 `imgui.ini`와 상태 파일이 분산되므로 전용 ImGui settings handler를 선택했다.
- 프리뷰 이미지를 창 폭에 맞춰 축소: 픽셀 진단 크기가 매번 변하므로 256×256은 유지하고 열 수만 1/2열로 전환한다.

## 5. 선택한 해결 방법

- `Cloud thickness` 슬라이더만 3~16 km로 확장하고 기본값·기본 프리셋·CloudCB·HLSL·cache v5는 유지한다.
- 파라미터를 `Shape & Noise`, `Animation`, `Lighting`, `Sampling`, `Wide Cloud Layer`, `Presets`의 접이식 그룹으로 나눈다.
- 최초에는 `Shape & Noise`만 열고, 이후 상태는 `imgui.ini`의 `[CloudDebug][Parameters]`에 저장한다.
- F1 Stats에는 Inspector/메인 source, 캐시 규격, 캐시 조작 버튼만 남긴다.
- 창 최초 크기를 480×560, 최소 크기를 360×280으로 바꾸고 viewport 크기를 최대 제약으로 사용한다.
- Noise Inspector는 가용 폭 540 px 미만에서 1열, 이상에서 2열을 사용한다.

## 6. 실제 수정 내용

- `src/DebugUI.cpp`
  - 두께 범위와 km 단위 format을 추가했다.
  - Parameters를 6개 `CollapsingHeader`로 재구성했다.
  - 카테고리 상태용 ImGui settings handler와 stable key를 추가했다.
  - Stats 중복 항목을 제거하고 창 크기 제약 및 Inspector 반응형 열 수를 적용했다.
- `src/DebugUI.h`, `src/Renderer.cpp`
  - Stats에서 쓰지 않는 frame interval, CPU/GPU 시간, cache status 인자를 `DebugUI::Draw`에서 제거했다.
- `src/Renderer.cpp`
  - 3/8/16 km와 weather thickness 0/0.5/1 조합의 지역 두께·128-step 간격이 유한한지 code test에 추가했다.
- `doc/PROJECT_STATUS.md`, `doc/RAYMARCHING.md`
  - 현재 F1 구조와 두께/step 절충을 동기화했다.

## 7. 캐시·호환성·성능 영향

- 생성 파라미터, texture layout, 셰이더 구조와 cache manifest가 바뀌지 않아 캐시 버전은 v5를 유지한다.
- 기존 사용자 preset 값과 파일 형식은 그대로 로드된다. 이번 변경은 preset을 불러올 때 값을 강제로 3~16 km로 clamp하지 않는다.
- view step 상한은 128이다. 수직 ray의 단순 기준 간격은 3/8/16 km에서 각각 약 0.023/0.063/0.125 km이며, 실제 간격은 ray 각도·교차 구간·adaptive march에 따라 달라진다.
- 두께를 바꾸는 것만으로 noise texture를 재생성하지 않으며 다음 CloudCB 업로드에 즉시 반영된다.
- 두꺼운 층은 같은 step 수에서 형태가 부드러워질 수 있고, 더 많은 밀도 sample이 light march를 호출하면 GPU 시간이 증가할 수 있다.

## 8. 테스트 및 실행 결과

- 2026-07-27 Debug 빌드: 통과
- 2026-07-27 Release 빌드: 통과
  - 빌드 후처리에서 `pwsh.exe`를 찾지 못했다는 환경 경고가 있었지만 MSBuild 산출물 생성과 HLSL 복사는 성공했다.
- Release `ctest`: 2/2 통과, 91.35초
  - `VolumetricCloud.CacheSmoke`: 2.71초
  - `VolumetricCloud.CodeTests`: 88.59초
- 3/8/16 km와 weather thickness 0/0.5/1 조합의 지역 두께·128-step 간격 유한값 검사는 `CodeTests`에서 통과했다.
- Debug 실행: 오류 MessageBox 없이 렌더 시작 확인
- 수동 UI 확인 항목:
  - 360×280 최소 크기에서 탭·스크롤·카테고리·캐시 버튼 접근
  - 540 px 전후에서 Inspector 1/2열 전환
  - 카테고리 상태 재실행 복원
  - 사용자 preset 저장·재로드
- 자동 F1 캡처는 키 입력이 패널 표시까지 재현되지 않아 위 네 항목을 합격 처리하지 않았다.
- 화면 품질 및 GPU 시간 사용자 승인: 대기

## 9. 남은 문제와 후속 개선

- 고정 카메라에서 3/8/16 km의 `GPU cloud`/`GPU total`을 사용자 GPU로 기록하고, 디테일 손실과 비용을 함께 판단해야 한다.
- 128 step에서 두꺼운 구름의 층상 밴딩이나 경계 손실이 확인되면 step 상한, 최대 물리 step 길이, light march 품질을 별도 브랜치에서 함께 조정한다.
- cloud type별 `cumulusGrowth`와 독립 vertical noise scale은 이번 범위에서 제외했다.
- 날짜·프리셋·카메라·두께·GPU 시간·관찰 결과를 기록한 뒤 사용자가 화면 품질을 최종 승인한다.
