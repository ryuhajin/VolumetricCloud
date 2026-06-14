# 로드맵

볼류메트릭 클라우드라는 최종 목표까지 가는 단계별 계획입니다.
한 단계를 완료하면 체크박스를 채우고, 관련 문서를 갱신하세요. (→ [CONTRIBUTING.md](CONTRIBUTING.md))

최종 목표: [`chihirobelmo/volumetric-cloud-for-directx11`](https://github.com/chihirobelmo/volumetric-cloud-for-directx11)
수준의 실시간 볼류메트릭 구름 렌더링.

---

## ✅ 1단계 — 반투명 안개 구 (현재)

레이마칭의 뼈대를 익히는 단계. **noise·light 없음.**

- [x] DirectX11 + HLSL 환경 구축 (CMake, Win32 창, D3D11 렌더러)
- [x] 풀스크린 삼각형 + 픽셀 셰이더 레이마칭
- [x] 해석적 ray-sphere 교차
- [x] Beer-Lambert 밀도 적분 → 반투명 구
- [x] 마우스 오빗 카메라
- [x] 문서화 (doc/, CLAUDE.md, README.md)

## ⬜ 2단계 — 형상 일반화 (박스 / SDF)

- [ ] ray-box(AABB) 교차로 박스 볼륨 추가
- [ ] SDF 기반 형상(구/박스/혼합)으로 밀도 영역 정의
- [ ] 셰이더 상수로 형상 전환

## ⬜ 3단계 — Noise (구름 형태)

- [ ] 3D 값/Perlin/Worley noise 함수 (또는 3D noise 텍스처)
- [ ] `density = shape × noise`로 뭉게구름 실루엣
- [ ] FBM(다중 옥타브)으로 디테일
- [ ] `time`으로 noise 이동 → 흐르는 구름

## ⬜ 4단계 — Light (산란/그림자)

- [ ] 적분 스텝마다 태양 방향 보조 레이로 self-shadow 적분
- [ ] Henyey-Greenstein 위상 함수로 전방 산란
- [ ] 다중 산란 근사, ambient

## ⬜ 5단계 — 구름 완성 / 최적화

- [ ] 하늘 모델 / 톤매핑
- [ ] 깊이 버퍼 통합(씬과 합성)
- [ ] 적응형 스텝, 조기 종료(early-out), 해상도 분리로 성능 확보
- [ ] (선택) ImGui로 파라미터 실시간 조절

---

### 진행 규칙
- 각 단계는 별도 `feature/<단계>` 브랜치에서 진행합니다.
- 단계 완료 시 이 문서의 체크박스와 [ARCHITECTURE.md](ARCHITECTURE.md)·[RAYMARCHING.md](RAYMARCHING.md)를 갱신합니다.
