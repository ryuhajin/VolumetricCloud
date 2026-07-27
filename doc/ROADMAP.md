# 로드맵

## 완료

- [x] DirectX11 풀스크린 삼각형과 AABB 레이마칭
- [x] Beer–Lambert 투과율, 48~128 가변 view steps, 고정 jitter, 조기 종료
- [x] periodic Value/Worley/FBM 및 Perlin–Worley 형상
- [x] 높이 마스크, noise cutoff threshold, detail erosion, X/Z wind
- [x] 128³ RGBA base 형태 밴드 + 64³ RGBA detail Texture3D 캐시 v4
- [x] ImGui Noise Inspector와 렌더 디버그 모드
- [x] 사용자 프리셋
- [x] `.cso` + volume + manifest 영구 캐시와 Save/Revert/Rebuild
- [x] 8-step 태양 light march, dual-lobe HG, powder, silver lining, 다중 산란 근사
- [x] GPU seam 검사, 캐시 왕복, 빠른 시작 계측용 코드 테스트

## 다음 통합 단계

- [ ] 사용자의 실제 화면 평가에 따른 Showcase 프리셋 최종 승인
- [ ] GPU timestamp query 기반 1280×720 성능 계측과 30 FPS 확인
- [ ] 깊이 버퍼 통합과 장면 오브젝트 교차
- [ ] 반해상도 렌더링 + temporal reprojection
- [ ] 날씨 맵과 더 넓은 월드 볼륨

현재 코드 완료 기준은 다중 주파수 형태, 근사 다중 산란, 안정적 샘플링, 영구 캐시와 자동 검사다. 시각적 품질과 성능 목표 승인은 실제 사용자 장비의 렌더 화면에서 별도로 수행한다.
