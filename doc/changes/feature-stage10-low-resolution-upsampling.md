# Stage 10 저해상도·업샘플링 이력

Stage 10에서는 50~100% Cloud Data MRT와 Nearest/Bilinear/Joint 공간 복원을 구현해 저해상도 비용과 경계 품질을 비교했다. scattering/transmittance와 cloud/scene depth를 분리하고 불투명 경계 거부를 검증했다.

2026-09-01 High 단일화에서 다음 이유로 런타임과 테스트를 삭제했다.

- 최종 High는 Full-resolution에서 직접 raymarch한다.
- 1:1 복원 pass와 filter 선택이 화면 이득 없이 파이프라인·자원을 늘렸다.
- Temporal 제거 뒤 저해상도 current/history 계약을 유지할 이유가 사라졌다.

현재 저장소에는 저해상도 Cloud RT, cloud-data MRT, UpsamplingCB, filter enum/UI, resolve shader와 Stage 10 CPU math가 없다. 이 문서는 구현했다가 폐기했다는 이력만 보존한다.
