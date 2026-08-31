# Stage 11 Temporal Reprojection 이력

Stage 11에서는 4-phase jitter, 이전 행렬 재투영, scene/cloud depth 거부, transmittance 검사와 neighborhood clip을 구현했다. history reset과 카메라·바람 안정성도 별도 smoke로 검증했다.

사용자가 Full-resolution High의 Temporal On/Off를 직접 비교한 결과 최종 화면 이득이 없다고 판단했다. 2026-09-01 High 단일화에서 Temporal shader, CB, history ping-pong, jitter 카메라 API, UI, debug와 CPU/GPU 테스트를 모두 삭제했다.

현재 렌더러는 매 frame의 Full-resolution 결과만 출력한다. ghost나 history reset 대기 구간은 없으며, Temporal 제거로 드러나는 shimmer 허용 수준은 사용자가 최종 화면에서 판정한다. 이 문서는 구현했다가 폐기했다는 이력만 보존한다.
