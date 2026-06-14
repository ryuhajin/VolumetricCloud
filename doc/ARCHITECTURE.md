# 아키텍처

이 문서는 모듈 구조, 렌더 파이프라인, 데이터 흐름(특히 상수버퍼)을 설명합니다.
코드 동작을 바꾸면 이 문서도 함께 갱신하세요. (→ [CONTRIBUTING.md](CONTRIBUTING.md))

## 1. 전체 구조

이 프로젝트는 "정점 버퍼 없이 화면 전체를 덮는 삼각형 1개를 그리고, 픽셀 셰이더에서
레이마칭으로 볼륨을 그린다"는 **풀스크린 포스트프로세스 형태**의 렌더러입니다.
지오메트리(메시)는 없습니다 — 구는 픽셀 셰이더 안에서 수학적으로만 존재합니다.

```
┌─────────────┐   입력(마우스)    ┌──────────┐
│   Window    │ ───────────────► │  Camera   │
│ (Win32)     │                   │ (오빗)    │
└─────┬───────┘   리사이즈        └────┬─────┘
      │                                │ view/proj/invViewProj
      │ WM_SIZE                        │
      ▼                                ▼
┌──────────────────────────────────────────────┐
│                  Renderer (D3D11)              │
│  device · context · swapchain · RTV            │
│  상수버퍼(cbCamera) 업데이트 → Draw(3)         │
└───────────────────────┬────────────────────────┘
                        │ 파이프라인
                        ▼
       Fullscreen.hlsl (VS)  →  RaymarchSphere.hlsl (PS)
       화면 덮는 삼각형         픽셀마다 레이마칭 → 색
```

## 2. 모듈 책임

| 모듈 | 파일 | 책임 |
|------|------|------|
| 진입점 | `src/main.cpp` | 객체 생성·연결, 타이머, 메인 루프 |
| 윈도우 | `src/Window.*` | Win32 창 생성, 메시지 펌프, 마우스 입력 → Camera, 리사이즈 → Renderer |
| 카메라 | `src/Camera.*` | yaw/pitch/distance 궤도 → view·proj·invViewProj, 카메라 위치 |
| 렌더러 | `src/Renderer.*` | D3D11 초기화, HLSL 런타임 컴파일, 상수버퍼 갱신, 드로우/Present |
| VS | `shaders/Fullscreen.hlsl` | `SV_VertexID`로 풀스크린 삼각형 생성, uv 전달 |
| PS | `shaders/RaymarchSphere.hlsl` | uv→레이, ray-sphere 교차, 밀도 적분, 합성 |

## 3. 렌더 파이프라인 (한 프레임)

1. `main` 루프: 경과 시간 계산, 종횡비 갱신.
2. `Renderer::Render(camera, time)`:
   - `camera`에서 `invViewProj`·위치를 받아 **상수버퍼(cbCamera)** 를 채운다.
   - DirectXMath 행렬을 `XMMatrixTranspose` 후 업로드 (HLSL `mul(vector,matrix)` 규약).
   - 백버퍼 클리어 → VS/PS/상수버퍼 바인딩 → `Draw(3, 0)` → `Present`.
3. GPU: VS가 삼각형 3정점을 만들고, PS가 픽셀마다 레이마칭을 수행.

## 4. 데이터 흐름 — 상수버퍼 `cbCamera`

CPU(`Renderer::CameraCB`)와 GPU(`cbCamera`)의 메모리 레이아웃은 **정확히 일치**해야 합니다.
HLSL은 16바이트 단위로 패킹되므로 순서/패딩에 주의하세요. (총 112바이트)

| 필드 | 타입 | 의미 |
|------|------|------|
| `invViewProj` | float4x4 | 역 뷰-투영. 픽셀→월드 레이 생성 (transpose 업로드) |
| `cameraPos` | float3 | 레이 원점 (카메라 월드 위치) |
| `time` | float | 경과 시간 (현재 미사용, 추후 애니메이션) |
| `sphereCenter` | float3 | 안개 구 중심 |
| `sphereRadius` | float | 안개 구 반지름 |
| `screenSize` | float2 | 화면 픽셀 크기 (현재 미사용) |
| `densityScale` | float | 밀도(불투명도) |
| `_pad` | float | 16바이트 정렬 패딩 |

> **주의:** 이 구조를 바꾸면 `Renderer.h`의 `CameraCB`와 `RaymarchSphere.hlsl`의
> `cbCamera`를 **동시에** 수정하고, 위 표도 갱신하세요.

## 5. 셰이더 컴파일 전략

- 셰이더는 **런타임에** `D3DCompileFromFile`로 컴파일합니다 (오프라인 .cso 아님).
- 따라서 HLSL만 수정 후 재실행하면 바로 결과를 볼 수 있습니다 (C++ 재빌드 불필요,
  단 셰이더가 exe 옆 `shaders/`에 있어야 함 — CMake POST_BUILD가 복사).
- 컴파일 실패 시 에러 메시지를 MessageBox로 그대로 표시합니다.

## 6. 의도적으로 생략한 것 (현재 1단계)

- noise(밀도 변조), light(산란/그림자), 깊이 버퍼, ImGui UI, 알파 블렌드 스테이트.
- 합성은 셰이더 내부에서 절차적 하늘 위에 직접 수행하므로 하드웨어 블렌딩이 필요 없습니다.

다음 단계는 [ROADMAP.md](ROADMAP.md)를 참고하세요.
