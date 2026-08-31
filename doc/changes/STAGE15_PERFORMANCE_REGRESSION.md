# Stage 15 성능 회귀 조사와 해결 기록

이 문서는 Stage 15 품질·콘셉트 프리셋 구현 중 발견한 Stage 14 호환 경로의
GPU Cloud 성능 회귀를 시간순으로 기록한다. 최종 결과만 정리하는 문서가 아니라,
당시의 가설·측정·실패한 실험까지 보존해 이후 회고와 재발 방지에 사용한다.

이 기록을 실제 화면 증상과 Debug View에 연결해 따라가는 절차는
[Stage 15 프리셋 사용자 검증 가이드](../STAGE15_PRESET_VALIDATION_GUIDE.md)의 `증상별 진단`과
`디버깅 회고`를 사용한다.

## 상태

- 조사 시작일: 2026-08-28
- 작업 브랜치: `feature/stage15-final-quality`
- 분기 기준: `e934228` (`stage14`)
- 현재 결론: **성능 root cause 해결·기능/화질/High Full clean 성능 gate 통과**
- 사용자 화면 미학 승인: 별도 대기

강제 Shadow 갱신 상태의 Stage 14 회귀와 당시 Stage 15 48-case 성능 gate가 통과해 코드 성능
root cause는 2026-08-28 해결로 확정했다. 이후 올바른 화질 readback을 위해 일반 `CloudUpsample`
bytecode가 바뀌었고, 최신 Release 96-case 화질 gate는 High Full과 cloud-edge/Temporal 수렴을
포함해 모두 통과했다. High Full 성능 첫 재실행은 `The Message From Deep Space`(PID 22488)의
GPU 3D 약 37%와 동시 fixture로
오염되어 무효였다. 게임 종료 뒤 최신 성능 48-case와 독립 3-block probe는 clean 단독 프로세스로
통과했으며 사용자 화면 미학 승인만 별도 단계다.

## 증상과 고정 기준

Release, 1920x1080, VSync/UI/preview Off, DenseHorizon, Full/Balanced,
Temporal Off, Balanced512에서 120프레임 warmup 뒤 서로 다른 GPU timestamp
600개를 수집한다.

| 항목 | GPU Cloud p95 | 의미 |
|---|---:|---|
| Stage 14 승인 기준 | 7.592960ms | `captures/stage14/performance.json`의 Stage14Physical DenseHorizon |
| Stage 15 저장 결과 | 8.187900ms | `captures/stage15/performance.json`, ratio 1.07835 |
| Stage 15 마지막 단일 probe | 8.109056ms | 화면 PS variant 분리 뒤 재측정, 아직 JSON 미반영 |
| 같은 날 다시 빌드한 `stage14` | 7.304192ms | GPU 일중 변동만으로 현재 회귀를 설명할 수 없는 교차 확인 |
| 합격 상한 | 7.8207488ms | 승인 기준의 103% |

- GPU: NVIDIA GeForce RTX 4080 SUPER
- Driver: 32.0.15.9186
- 빌드: CMake Release, MSVC/Windows SDK FXC, 기본 `/O1` shader compile
- 정식 명령: `build/Release/VolumetricCloud.exe --stage15-performance-test`
- 분해 진단 명령: `--stage15-stage14-regression-probe`

## 조사 연표

| 순서 | 변경 또는 실험 | 결과 | 판단 |
|---:|---|---|---|
| 1 | Stage 15 초기 성능 측정 | Stage 14 대비 약 17.7% 회귀 | 원인 조사 시작 |
| 2 | 화면 Raymarch PS를 일반/Cirrus variant로 분리 | 회귀가 약 6.8%까지 감소 | 동적 Cirrus 경로가 hot path에 영향을 줌 |
| 3 | FXC `/O3` 실험 | 회귀가 약 20.4%로 악화 | 채택하지 않고 기본 `/O1`로 원복 |
| 4 | Temporal low-resolution direct 3x3 resolve | Resolve p95 2ms gate 복구, Stage 11 smoke 통과 | 채택. Cloud Raymarch 원인과는 분리 |
| 5 | Stage 14 태그를 같은 날 재빌드 | 7.304192ms | 단순 GPU clock/driver 변동 가설 기각 |

## 확인된 원인

### 1. non-Cirrus density 분기가 FXC의 dead-code 제거를 방해

Stage 14의 optimized density는 `cloudShapeMode != WeatherPhysical` 조건을 사용해
else 블록이 반드시 Weather Physical임을 FXC가 증명할 수 있었다. Stage 15에서 Cirrus를
추가하며 이 조건을 `cloudShapeMode == Legacy`로 바꾸자 non-Cirrus variant에서도
Physical/추가 shape 계산 일부가 남았다.

고정 FXC `/O1`, `mainOptimizedData`, `VCLOUD_CIRRUS_VARIANT=0` 비교:

| 바이너리 | 크기 | slots | temps |
|---|---:|---:|---:|
| Stage 14 | 118,048B | 3,377 | 55 |
| 수정 전 Stage 15 | 119,588B | 3,426 | 56 |

`EvaluateBaseCloudDensityOptimized`와 `EvaluateLightCloudDensity`의 두 분기를
non-Cirrus compile-time 조건으로 Stage 14 형태로 복원했다. 실제 Release compile flag와 같은
고정 FXC `/O1` 결과는 Stage 14 CSO와 크기·SHA-256이 모두 같다.

- 크기: `118,048B`
- SHA-256: `E0B473599E65AA42FC5E6AAAF1921613EDFD900285A90F182D83E7397289DD9F`

### 2. Deep Shadow CS가 일반/Cirrus 공용 dynamic shader

화면 PS는 macro 0/1 variant를 만들지만 `CloudDeepShadow.hlsl`은 macro 없이 한 번만
컴파일된다. 따라서 일반 구름에서도 b7 전체 112바이트와 Cirrus 분기·연산이 light-density
inner loop에 남는다.

| Deep Shadow CS | b7 선언 | temps | div | rsq |
|---|---:|---:|---:|---:|
| Stage 14 | CB7[4] | 21 | 33 | 2 |
| Stage 15 generic | CB7[7] | 24 | 43 | 3 |
| Stage 15 non-Cirrus 임시 variant | CB7[4] | 21 | 31 | 검증 기준 이하 |

렌더러도 `VCLOUD_CIRRUS_VARIANT=0/1` CS를 둘 다 생성하고 sanitize된 `shapeMode`로 고른다.
hot reload는 두 CS가 모두 성공해야 원자 교체한다. non-Cirrus 결과는 Stage 14와 같다.

- 크기: `28,264B`
- SHA-256: `8BA92CF46FAD586EEEDAE7A6F2E8AC19370530E5E7F2CD9B77BC3C56401CCB01`

## 원인이 아니거나 아직 확정하지 않은 항목

- 화면 non-Cirrus PS는 b7을 `CB7[4]`, 즉 앞 64바이트만 참조한다. CPU/HLSL ABI를
  112바이트로 확장한 사실만으로 화면 Raymarch 회귀를 설명할 수 없다.
- Stage 15 네 Weather preset cache는 CPU `WeatherMapData`이며 회귀 fixture 전에 기본
  concept도 적용하지 않는다. steady GPU Cloud p95의 직접 원인이 아니다.
- 자동 측정에서 `SetNoiseLabVisible(false)`는 F1 패널만 숨기고 새 compact overlay는
  계속 그린다. 이는 동일 fixture 계약 위반이며 Frame/CPU 측정을 오염시키지만 Cloud
  timestamp 바깥이므로 현재 Cloud 회귀의 직접 원인이라고 단정하지 않는다. 이후
  `SetAutomatedRenderMode(true)`가 렌더 프레임의 모든 ImGui·overlay·preview와 수동 네 PNG export를
  생략하도록 고정했다. 화면·history를 바꾸지 않는 Temporal 통계용 네 번째 MRT, mip 생성과
  readback도 자동 quality/performance에서는 생략한다. preset smoke의 schema 37 metadata-only
  JSON은 프레임 밖에서 한 번 내보낸다.
- concept+quality 중복 적용과 전체 constant-buffer 반복 Map은 전환 hitch와 CPU/driver
  비용 문제다. GPU Cloud 0.3~0.6ms 회복의 직접 원인으로 과장하지 않는다.

## 구현·실험 로그

아래 표는 변경 하나마다 바로 추가한다. 서로 다른 변경을 한 행에 섞지 않는다.

| 날짜/commit | 단일 변경 | 예상 | Shadow p95 | Raymarch p95 | Resolve p95 | Cloud p95 | 결과 |
|---|---|---|---:|---:|---:|---:|---|
| 2026-08-28 / 수정 전 | 현재 Stage 15 화면 variant | 동적 화면 분기 일부 제거 | 미수집 | 미수집 | 2ms 이하 | 8.109056 | 실패, 분해 계측 필요 |
| 2026-08-28 / 작업 트리 | non-Cirrus density 조건 복원 | 화면 PS Stage 14 codegen 복원 | - | 정적 bytecode 동일 | - | GPU 재측정 대기 | 채택 |
| 2026-08-28 / 작업 트리 | Deep Shadow CS 0/1 분리 | 일반 CS의 Cirrus 분기 제거 | 정적 bytecode 동일 | - | - | GPU 재측정 대기 | 채택 |
| 2026-08-28 / 작업 트리 | 자동 렌더 모드+3회 분해 probe | fixture/UI 오염 제거, 구간별 원인 분리 | 0.715776~0.760832 | 7.520192~8.055808 | 1.008640~1.019904 | 중앙값 8.702976 | 외부 게임 GPU 사용으로 무효 |
| 2026-08-28 / 작업 트리 | Temporal low-res 직접 3×3 | Resolve 중복 reconstruction 제거 | - | - | 2ms gate 통과 | - | 채택, Stage 11 smoke 통과 |
| 2026-08-28 / 작업 트리 | ChatGPT 창 최소화 fixture | 벤치마크와 같은 GPU를 쓰는 host UI 제거 | 0.411648 | 5.193728 | 0.060416 | 중앙값 5.659648 | probe 통과, ratio 0.745381 |
| 2026-08-28 / 작업 트리 | 당시 48-case(`CloudUpsample` readback 수정 전) | 절대·상대·Stage 14 회귀 전체 확인 | 회귀 0.575488 | 회귀 6.508544 | 회귀 0.680960 | 회귀 6.979584 | 당시 성능 gate 통과, ratio 0.919218; 당시 최신 shader 재실행 필요 |
| 2026-08-31 / 최신 source | 게임 종료 뒤 clean 48-case | High Full 절대·Low/Medium 상대·Stage14 내장 회귀 | 회귀 0.723968 | 회귀 5.719040 | 회귀 0.440320 | 회귀 6.376450 | 48/48, ratio 0.839784; High Cloud 최악 9.95738ms |
| 2026-08-31 / 최신 source | 독립 3-block probe | 일중 변동과 상태 불변 교차 확인 | 0.582656~0.659456 | 5.600256~5.880832 | 0.479232~0.488448 | 중앙값 6.213632 | ratio 0.818341, state/component/D3D 통과 |

초기 probe 때 `FieldsOfMistria.exe`가 같은 RTX 4080 SUPER에서 약 8~24% GPU를 사용했다.
상태 fingerprint와 timestamp 합계는 맞았지만 독립적인 GPU fixture가 아니므로
당시 수치는 공식 판정에서 제외했다. 게임 종료 뒤에도 Codex를 표시하는 `ChatGPT.exe`가
Windows 3D 엔진을 약 11~28% 사용해 Cloud p95를 `6.36~8.30ms` 사이로 흔들었다. 당시 성능 측정은
ChatGPT 창을 최소화하고 종료 즉시 복원했으며, 그 결과를 `captures/stage15/*.json,csv`에 보존했다.

## 구조 최적화 결과

- 프리셋 요청을 `Stage15TransitionRequest` 하나로 합친다. 프레임 경계에서 전체 CPU/Weather/GPU
  resource identity snapshot을 잡고 `Quality → Concept → Temporal override → Diagnostic` 순서로
  적용하며, 뒤 단계가 실패하면 Weather 픽셀까지 snapshot으로 되돌린다. 전부 성공했을 때만 commit을
  한 번 기록한다. 같은 요청 100회는 첫 적용 뒤 Weather upload, resource 생성, history reset,
  commit이 모두 0회여야 한다.
- Weather/Cloud RT/Shadow fault injection은 CPU enum, 실제 resource identity, Weather hash/픽셀과
  history generation을 보존해야 한다. 이 보강 뒤 Debug/Release preset smoke 48/48과
  rollback/idempotency/restore retry가 통과했다.
- 상수버퍼는 canonical byte dirty hash로 같은 pass 공통 값을 중복 `Map`하지 않는다.
- shader bytecode cache는 root HLSL과 재귀적 literal include closure, entry, target, macro, flag,
  compiler identity의 SHA-256 key를 사용한다. include를 안전하게 해석하지 못하면 cache 없이
  compile한다. warm validation은 runtime compile 0회, cache hit 22개 이상이며 cold/warm shader
  hash가 같다. hot reload는 내용이 바뀌지 않은 variant의 cache를 재사용하고 전체 variant 성공 뒤
  세대를 갱신한다.
- exact `ShadowCacheKey` 기반 dispatch 생략은 도입하지 않았다. 계획상 강제 Shadow 재생성 상태의
  3% gate가 먼저 통과하고 48-case의 남은 병목이 Shadow일 때만 허용된다. 현재 유효하지 않은
  probe에서도 Shadow는 약 0.72ms이고 주요 변동은 Raymarch였으므로 캐시로 회귀를 숨길 근거가 없다.

## 2026-08-31 자동 검증 상태

아래 성능 수치와 shader 특수화 결과는 보존된 이전 측정 증거다. 다만 그 뒤 transaction/소유권/UI
잠금과 숨김 quality 출력 ID가 수정되었고, 후자는 일반 `CloudUpsample` bytecode도 바꾼다. 따라서
최신 source를 인증하는 최종 기능·성능 검증과 구분한다.

- 이전 code revision: Debug/Release 빌드와 CTest `51/51`, shader hot reload/warm cache,
  Stage 15 preset smoke `48/48`, Stage 11 Temporal smoke 통과
- 최신 revision: Debug/Release preset smoke 48/48, PMv2/output, Native Capture 4/4,
  확장 rollback/idempotency/restore retry와 D3D 통과
- Release Stage10/11/15 targeted suite `8/8` 통과. Release 전체 첫 실행은 Stage 12 최초 캡처
  크기 drift로 `53/54`였고, 캡처 전 `96×54`를 명시한 뒤 Stage 12 focused Debug/Release
  `2/2`가 `MAE=0.001635`, `P99=0.029349`로 통과. 최종 Debug/Release 전체 CTest도 각각
  `54/54` 통과
- 기존 quality `96/96` 파일: `Stage15ResolvedCloud=79`가 Composite로 sanitize되고 Temporal Off의
  `CloudUpsample`도 ID 8(T)/79(scattering)를 분리하지 않던 수정 전 결과이므로 최종 산란/T gate
  증거에서 제외
- 올바른 readback의 첫 재측정 실패는 회고 자료로 보존한다. 이후 reference T로만 정의한
  cloud/cloud-edge mask를 추가한 최신 Release 1080p 96-case는 High Full, edge 개선,
  Temporal edge 4→16 개선, history age/reset과 readback self-check/D3D를 모두 통과했다.
- 이전 regression probe/48-case와 외부 게임에 오염된 첫 High Full 실행은 회고 기준으로 보존한다.
- 최신 Stage 15 clean 48-case: 절대 실패 0, Low/Medium 상대 품질 16조합 통과,
  component invariant와 D3D11 debug error 0
- 품질별 최대 Frame/Cloud/Resolve p95: Low `2.64397/2.59891/1.12026ms`, Medium
  `3.86970/3.82566/0.956416ms`, High `10.0014/9.95738/0.892928ms`
- Stage 15 성능 명령 내부 Stage 14 회귀: `6.37645ms`, 승인 기준 대비 `0.839784×`, 통과
- 최신 독립 Stage 14 3-block: `6.57203/6.15219/6.21363ms`, 중앙값 `6.21363ms`,
  승인 기준 대비 `0.818341×`, 동일 fingerprint/component invariant와 D3D 통과
- 최악 Low/Medium `Shadow+Raymarch` owned-average ratio `0.811623`

### 상대 품질 fixture 보정

무부하 첫 48-case 실행은 모든 절대 gate와 Stage 14 회귀 gate를 통과했지만
`Desert Cirrus/AboveLayer` 한 곳의 Frame 평균 상대 순서만 실패했다. 이 카메라는 Cirrus 층 밖이라
세 품질의 Raymarch가 모두 약 `0.1ms`인 빈 장면이다. 품질과 무관하게 동일해야 할 Resolve 평균이
Low/Medium/High `0.414/0.320/0.237ms`로 변해 전체 Frame을 역전했다. 반면 품질 소유 비용인
`Shadow+Raymarch`는 `0.239/0.324/0.289ms`로 Low가 Medium보다 약 26% 낮았다.

당시에는 3%/180% 기준을 유지하고 상대 비교 metric만 `Shadow+Raymarch` 600-sample 평균으로
고쳤다. 15A에서 High가 50%에서 Full RT/Full-resolution Temporal로 승격된 뒤에는 High와
Medium의 픽셀 수·Resolve 구조가 달라졌다. 따라서 최신 fixture는 Low≤0.97×Medium 상대 gate만
유지하고 High는 Frame/Cloud/Resolve 절대 p95로만 판정한다. 품질 descriptor나 화면 알고리즘을
이 보정 때문에 바꾸지는 않는다.

각 행에는 별도 부록으로 bytecode hash, slots, temps, div/rsq, adapter/driver,
state/resource fingerprint와 결과 JSON/CSV 경로를 남긴다.

## Stage 15B boundary refinement 실험 철회

Stage 15B에서는 빈↔밀도 전환마다 이분 탐색하고 fine interval을 pending 상태로 유지하는
boundary refinement를 별도로 구현·측정했다. 실험을 숨기지 않고 실패 근거를 다음과 같이
남긴다.

- 실제 GPU contour 개선은 Medium 약 `0.003%`, High 약 `0.99%`였다. 결정적 synthetic
  fixture의 큰 개선율은 실제 비단조 구름 표면을 대표하지 못하므로 승인 gate에서 제거했다.
- 빠른 Raymarch 대체 측정은 기준선보다 약 `18~35%` 느렸다.
- FXC `/O1` 통계는 약 `60→166` temps, `22,436→60,933` instruction slots,
  `589KB→1.58MB`로 증가했다.
- 구름 density가 단조 signed-distance 경계가 아니라 비단조 3D noise volume이므로, 모든
  내부 전환을 이분 탐색하면서 여러 sample과 pending 상태를 동시에 살려 두는 방식은 실제
  실루엣 이득에 비해 texture fetch, register pressure와 shader 규모가 지나치게 컸다.

따라서 전용 CLI와 boundary 화질·성능 gate를 제거하고, 런타임은 승인된 Stage 9의
`coarse 후보 탐색 → 한 coarse interval rewind → fine march` 계약으로 복구했다.
`OptimizationCB` offset 44는 다시 호환용 padding이며 diagnostic ID 82는 enum 호환을 위해
예약하되 무효 출력으로 남긴다. Stage 15B P3에서 채택한 외곽 개선은 resolve 이후의
full-resolution NTE rim뿐이다.

향후에는 conservative occupancy/max-mip으로 빈 공간을 보수적으로 분류하거나 최종 density
구간에만 제한적인 quadrature를 적용하는 방법을 별도 연구할 수 있다. 이번 단계에는 어느
쪽도 구현하지 않는다.

### Stage 15B 롤백 후 최종 측정

2026-08-31 17:14~17:17에 RTX 4080 SUPER/driver `32.0.15.9186`, Release
1920×1080에서 boundary 실험을 제거한 최종 후보를 다시 측정했다.

- Stage 15 quality: `96/96 PASS`.
- Stage 15 performance: 120 warmup+600 timestamp, `48/48 PASS`.
- Stage 14 Cloud p95: `6.966272ms`, 승인 기준 대비 `0.917465×`.
- 48-case 최악 Cloud p95: Meadow High AboveLayer `9.224192ms`.
- Full-resolution Composite/rim 최악 p95: `0.167936ms`, `0.75ms` 예산 통과.
- state/component invariant와 D3D11 debug gate 통과.

근거 파일은 `captures/stage15/quality.json,csv`와
`captures/stage15/performance.json,csv`다.

## 최종 회고

1. 코드 root cause는 generic Cirrus 분기가 non-Cirrus FXC dead-code 제거를 막은 두 지점과
   공용 Deep Shadow CS였다. 일반 Raymarch/Shadow를 compile-time variant로 복원하자 Stage 14와
   CSO가 바이트 단위로 같아졌다.
2. 남은 측정 변동의 root cause는 같은 GPU에서 실행되던 게임과 ChatGPT 데스크톱 3D UI였다.
   앱 내부 UI 생략만으로는 host UI를 제거할 수 없으므로 GPU 프로세스 카운터 확인과 창 최소화가
   benchmark fixture에 필요했다.
3. `/O3`는 약 20.4%까지 악화되어 원복했다. b7 확장 자체와 compact overlay는 Cloud timestamp의
   직접 원인이 아니었다. exact Shadow dispatch cache도 필요하지 않아 도입하지 않았다.
4. non-Cirrus 최적화 경로 출력 동일성은 CSO SHA-256 동등성과 Stage 11 smoke로 확인했다. 숨김 ID
   79와 Temporal Off spatial resolve의 8/79 출력 수정 뒤 첫 Stage 15 end-to-end 96-case는 기존
   Temporal not-worse 한 건이 실패했다. 이 실패는 회고 자료로 보존했다. 이후 reference T로 고정한
   cloud/cloud-edge mask를 추가한 최신 Release 재측정은 High Full과 edge/Temporal 수렴을 포함해
   `96/96` 통과했다. 전체 화면 T의 작은 bias도 JSON에 그대로 남겼다.
5. 향후 shape mode 추가 시 PS와 CS를 함께 특수화하고 assembly/CB 참조 범위를 먼저 비교한다.
   벤치마크 전에는 Windows GPU Engine에서 외부 3D 프로세스와 host UI를 확인한다.
6. 재현 결과는 `captures/stage15/regression_probe.json,csv`와 `performance.json,csv`다.
   외부 게임과 겹친 첫 High Full 파일은 무효로 판정했으며, 게임 종료 뒤 2026-08-31 00:59~01:00에
   생성한 clean 파일이 최신 공식 성능 판정 자료다.
7. 자동 수치는 화면 미학을 승인하지 않는다. 사용자는 공식 Stage 15 검증 가이드에서 두 overlay를
   숨긴 고정 카메라 화면과 각 Debug View를 별도로 판정한다.

## 재발 방지 체크리스트 초안

- 실시간 hot shader는 새 mode를 추가할 때 generic dynamic 분기에 의존하지 않는다.
- 공통 HLSL 함수를 유지하더라도 PS/CS entry는 compile-time variant로 검사한다.
- total instruction 수만 보지 않고 temps/div/rsq/sample과 실제 실행 entry를 확인한다.
- Cloud Total과 함께 Shadow/Raymarch/Resolve 원시 p95를 항상 저장한다.
- benchmark는 모든 UI와 preview가 실제로 꺼졌음을 counter/fingerprint로 증명한다.
- 캐시 생략 최적화는 강제 재생성 성능 gate를 통과한 뒤에만 평가한다.
