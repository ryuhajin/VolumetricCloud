# 프로젝트 현황 & 프레임 흐름

> 복귀 시 현재 구현과 한 프레임의 작업을 빠르게 확인하는 문서.
> Stage 4 / `feature/cloud-layer-weather-map` 기준.

## 현재 구현

| 영역 | 상태 |
|---|---|
| 레이마칭 | 평면 구름층 교차, 최대 거리, 48~128 view step, weather 4×/2×/1× 진행 |
| 광역 분포 | 512² RGBA weather map, coverage/type/base-height/thickness |
| 형태 | periodic Perlin-Worley + Worley base 3밴드 + detail 4옥타브 |
| 높이 | 하단이 좁고 중단이 부푸는 Cumulus profile |
| 조명 | 8-step self-shadow, 정규화 dual-lobe phase, powder, silver lining |
| 다중 산란 | light visibility의 3-octave 저비용 근사 |
| 캐시 | v5 `.cso` 7개 + 128³ base + 64³ detail + 512² weather |
| 진단 | 20개 렌더 모드, 4-MRT Noise Inspector, weather preview, GPU ms |
| 검증 | slab 교차, seam, 3D/weather RGBA 분산, cache round-trip, 산란 수치 |

형태와 조명 수식은 구현됐지만 최종 심미 품질과 30 FPS 목표는 사용자 장비에서 승인해야 한다. 자동 테스트는 스크린샷의 미적 합격 여부를 판단하지 않는다.

## GPU 리소스

| 슬롯 | 리소스 | 내용 |
|---|---|---|
| b0 | `CameraCB` | 역 view-projection, 카메라와 호환용 기존 볼륨 필드 |
| b1 | `CloudCB` | 176바이트 노이즈·weather·구름층·조명·샘플링 파라미터 |
| b2 | Preview/Generation CB | 단면 또는 볼륨 생성 설정 |
| t0 | base Texture3D | 128³ RGBA8, Perlin-Worley + Worley 3밴드 |
| t1 | detail Texture3D | 64³ RGBA8, Worley 4옥타브 |
| t2 | weather Texture2D | 512² RGBA8, coverage/type/base-height/thickness |
| s0 | wrap sampler | 선형 Texture3D 샘플링 |

메인 ray/light loop는 캐시 Texture3D만 읽는다. 절차식 RGBA 노이즈는 Inspector와 cache difference의 단일 지점 진단에서만 평가해 fxc 컴파일 시간과 프레임 비용을 제한한다.

## 한 프레임 순서

```text
ImGui Begin
  → UI 변경 및 캐시 명령 처리
  → CameraCB / CloudCB 업로드
  → [생성 파라미터 변경 시] base/detail/weather compute dispatch
  → [Inspector 열림 + dirty] 4-MRT preview
  → fullscreen triangle
      → cloud slab intersection
      → jittered/adaptive view march
      → density가 있을 때 light march
      → Beer–Lambert + dual-lobe + multiple-scatter approximation
  → ImGui draw
  → Present
  → HLSL hot reload 확인
```

정상 v5 캐시 시작에서는 HLSL runtime compile과 noise dispatch가 모두 0회다.

## F1 진단

- 형태: Final Density, Base Shape, Detail, Height, Base R/G/B/A, Weather R/G/B/A
- 조명: Transmittance, Light Visibility, Phase, Ambient, Direct
- 일관성: Procedural/Cache Difference, Seam Difference
- 프리셋: Default, Cumulus, Stratus, Cumulus Showcase
- 통계: FPS, CPU frame, view/light step, cache 상태

## 남은 우선순위

1. 사용자 시각 승인과 Wide Showcase 값 미세 조정
2. 사용자 GPU에서 실제 GPU frame time 확인
3. 장면 depth 합성
4. 반해상도 temporal reprojection
5. 구형 대기층과 지구 곡률

변경 이유와 대안은 [feature-cloud-layer-weather-map 변경 기록](changes/feature-cloud-layer-weather-map.md)을 참고한다.
