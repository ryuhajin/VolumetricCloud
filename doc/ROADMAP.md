# 로드맵

## 완료

- [x] DirectX11 풀스크린 삼각형과 AABB 레이마칭
- [x] Beer–Lambert 투과율, 64 view steps, 조기 종료
- [x] periodic Value/Worley/FBM 및 Perlin–Worley 형상
- [x] 높이 마스크, noise cutoff threshold, detail erosion, X/Z wind
- [x] 128³ base + 64³ detail Texture3D 캐시
- [x] ImGui Noise Inspector와 렌더 디버그 모드
- [x] 사용자 프리셋
- [x] `.cso` + volume + manifest 영구 캐시와 Save/Revert/Rebuild
- [x] 6-step 태양 light march, Beer–Lambert self-shadow, HG, ambient
- [x] GPU seam 검사, 캐시 왕복, 빠른 시작 계측용 코드 테스트

## 다음 품질 단계

- [ ] 사용자의 실제 화면 평가에 따른 density/noise cutoff/light 기본값 튜닝
- [ ] blue-noise jitter로 view-step banding 완화
- [ ] 다중 산란 근사
- [ ] 깊이 버퍼 통합과 장면 오브젝트 교차
- [ ] 반해상도 렌더링 + temporal reprojection
- [ ] 날씨 맵과 더 넓은 월드 볼륨

현재 완료 기준은 심리스 노이즈, 영구 캐시, 단일 산란 라이팅과 코드 검증이다. 시각적 품질 승인은 실제 렌더 화면에서 별도로 수행한다.
