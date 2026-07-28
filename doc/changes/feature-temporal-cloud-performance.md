# feature/temporal-cloud-performance 변경 기록

## 1. 목표와 배경

- 기본 Release 1280×720 장면의 시작 구간에서 30 FPS가 지속된 뒤 60 FPS로 바뀌는 원인을 코드와 GPU 수치로 분리한다.
- 목표 성능은 GPU total median 16.0 ms 이하, p95 16.67 ms 이하이다.
- 화면 캡처나 심미적 자동 평가는 하지 않고 사용자가 최종 화면을 승인한다.

## 2. 기존 구현과 관찰된 문제

- 기본 렌더는 모든 1280×720 픽셀에서 구름 view/light ray march를 실행하고 `Present(1)`로 수직 동기화한다.
- 기존 Release benchmark의 3.8 km/128 step GPU total median은 31.2975 ms다.
- 정상 cache hit에는 런타임 HLSL compile과 noise dispatch가 없고, 코드에는 1~2분 뒤 품질이나 FPS를 바꾸는 타이머가 없다.
- `Present(1)` 대기와 GPU 실행 시간이 현재 telemetry에서 분리되지 않아 30→60 전환의 직접 원인을 확정할 수 없다.

## 3. 원인 분석과 근거

- 16.67 ms를 넘는 프레임은 60 Hz 수직 동기에서 다음 refresh를 기다려 30 FPS처럼 표시될 수 있다.
- 시작 120초 동안 frame interval, CPU submit, Present wait, GPU raymarch/reconstruction/total과 cache 이벤트를 프레임별로 기록해 GPU 부하 변화와 표시 주기 변화를 대조한다.
- 배포 `main_ps.cso`와 `/O3` 임시 compile은 크기 1,105,116바이트와 약 36,187 instruction slot이 같았다. Debug cache 오염 가설은 제외한다.

## 4. 검토한 대안

- 전체 해상도에서 step 수만 줄이면 구조는 단순하지만 얇은 경계와 조명 안정성이 쉽게 손상된다.
- 계층형 weather/base/detail early-out은 결과를 유지하면서 texture sample을 줄이지만 화면 전체 픽셀 비용은 남는다.
- half-resolution raymarch와 temporal reconstruction은 history rejection과 ghosting 관리가 필요하지만 가장 큰 픽셀 비용 절감이 가능하다.

## 5. 선택한 해결 방법

- weather potential과 base shape가 비어 있으면 detail texture를 읽지 않는 보수적 early-out을 view/light/refinement에 공통 적용한다.
- beauty mode는 0.5배 축 해상도로 raymarch하고 full-resolution history reprojection으로 복원한다.
- 4-frame jitter, 이전 view-projection과 바람 이동, depth rejection, 3×3 neighborhood clamp를 사용한다.
- debug mode와 F1 reference 선택은 기존 full-resolution 경로를 사용한다.
- cache shader 목록 변경으로 cache v7을 사용하고 Release 기본 bundle을 재생성한다.

## 6. 실제 수정 내용

- `EvaluateLayerCloudComponents`를 weather → base/macro → detail 순서로 평가하고 potential 또는 macro density가 정확히 0인 영역에서 다음 texture sample을 건너뛰도록 했다. 동일 함수가 view/light/refinement에 공통 사용되며 미세한 light optical depth도 바꾸지 않는다.
- `CameraCB`를 96바이트로 정리하고 `TemporalCB` 96바이트를 추가했다. `CloudParameters`/`CloudCB` 224바이트는 변경하지 않았다.
- raymarch PS는 half-resolution `R11G11B10_FLOAT` color와 `R16_FLOAT` 첫 유효 구름 거리를 MRT로 출력한다.
- `TemporalResolve.hlsl`은 4-frame jitter, 이전 view-projection, wind delta, 화면/depth rejection, 현재 3×3 clamp와 0.85 history weight를 적용한다. 두 벌의 full-resolution color/depth history를 ping-pong한다.
- `Composite.hlsl`은 복원 결과를 back-buffer로 합성한다. debug mode와 reference 선택은 full-resolution 직접 경로를 유지한다.
- resize, UI 파라미터·프리셋·모드 변경, temporal 전환, 1km 초과 카메라 이동에서 history를 무효화한다.
- `--benchmark-view-steps-reference`, `--diagnose-startup-reference`, `--diagnose-startup-temporal`을 추가했다. startup CSV는 120초 동안 frame interval, CPU submit, Present wait, GPU raymarch/reconstruction/total, compile/dispatch와 cache 상태를 기록한다.
- benchmark/startup CSV는 임시 파일을 완전히 쓴 뒤 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`로 교체해 기존 파일이 있어도 재실행할 수 있게 했다.
- F1에 temporal/reference 선택, F2에 raymarch/reconstruction/total GPU 시간을 추가했다.

## 7. 캐시·호환성·성능 영향

- `CloudParameters`/`CloudCB` 224바이트와 noise 생성 파라미터는 유지한다.
- `CameraCB`는 96바이트로 정리하고 별도 96바이트 `TemporalCB`를 추가한다.
- temporal shader가 bundle 필수 항목이 되므로 이전 cache v6은 거부하고 v7을 사용한다.
- 기본 bundle은 Release 설정으로 재생성했으며 main/temporal/composite/preview/noise를 합쳐 `.cso` 9개를 포함한다.
- half-resolution color/depth와 full-resolution history 두 벌이 추가된다. 반면 raymarch 대상 픽셀 수는 full-resolution의 1/4이다.

## 8. 테스트 및 실행 결과

- Debug/Release 빌드 통과.
- `VolumetricClouds.hlsl`, `TemporalResolve.hlsl`, `Composite.hlsl`의 fxc shader model 5.0 컴파일 통과. 기존 `pow` 입력 범위 경고 외 오류 없음.
- Release cache smoke: cache `Saved`, runtime compile 0회, noise dispatch 0회.
- Debug/Release code test: 계층형 early-out 동등성, temporal 행렬·깊이 rejection, history reset, 리소스 크기/포맷, resize 재생성 포함 전 항목 통과. Debug D3D11 warning 이상 메시지 없음.
- `ctest --test-dir build -C Release --output-on-failure`: 2/2 통과.

Release 1280×720, 3.8km/128 step 결과:

| 경로 | raymarch median | reconstruction median | GPU total median | GPU total p95 | 이론 FPS |
|---|---:|---:|---:|---:|---:|
| full-resolution reference | 28.9833ms | 0ms | 28.9833ms | 34.3624ms | 34.50 |
| half-resolution temporal | 11.7827ms | 0.4168ms | 12.2353ms | 13.4062ms | 81.73 |

기존 31.2975ms 대비 계층형 early-out이 적용된 reference median은 약 7.4% 감소했다. temporal total은 새 reference 대비 약 57.8% 감소해 median 16.0ms, p95 16.67ms 목표를 모두 통과했다.

120초 VSync startup 진단:

| 경로 | 프레임 수 | 0~10초 | 10~120초 | Present median | compile/dispatch |
|---|---:|---:|---:|---:|---:|
| reference | 3,575 | 33.358ms, 29.98 FPS | 약 33.33ms, 30 FPS | 약 32.38ms | 0/0 |
| temporal | 7,201 | 16.667ms, 60 FPS | 약 16.67ms, 60 FPS | 약 15.72ms | 0/0 |

통제된 Release 실행에서는 30→60 전환이 재현되지 않았다. reference는 120초 내내 30 FPS였고 temporal은 시작부터 60 FPS였다. 따라서 현재 코드의 1~2분 타이머, compile 또는 noise dispatch가 원인이라는 가설은 배제된다. 직접 확인된 30 FPS 원인은 full-resolution GPU 부하가 한 vblank을 넘고 `Present(1)` 대기까지 결합된 것이다. 사용자가 관찰한 지연 전환은 동일 binary/settings에서 startup CSV로 다시 확인해야 하며, 재현된다면 드라이버 전원·클럭 정책 같은 애플리케이션 외부 상태가 우선 후보이다. VSync 중 temporal GPU timestamp가 약 16.5ms인 것은 uncapped benchmark 12.2ms와 달리 GPU가 refresh 목표에 맞춰 클럭을 낮출 수 있으므로 별도로 해석한다.

## 9. 남은 문제와 후속 개선

- 2026-07-28 사용자 검증에서 시작 직후와 1~2분 뒤 모두 60 FPS가 유지됐다. reference 전환 시 25~30 FPS, frame 35~40ms로 하락해 자동 진단과 같은 성능 차이를 확인했다.
- 같은 검증에서 reference를 끈 뒤 temporal을 다시 켜면 구름 상·하 경계가 떨렸고, `Freeze animation`을 꺼도 구름 전체 이동이 보이지 않았다.
- 떨림 원인은 jittered half-resolution sample을 full-resolution의 같은 UV에 놓아 현재 프레임이 매번 서브픽셀만큼 이동한 채 history와 섞이는 좌표 불일치로 판단했다. resolve에서 jitter를 UV로 역변환해 현재 color/depth와 3×3 범위를 unjittered 출력 위치에 맞춘다.
- 정지처럼 보인 원인은 base/detail 3D noise만 바람으로 이동하고 구름의 큰 실루엣을 정하는 weather map은 월드에 고정돼 있었기 때문이다. weather와 3D noise에 같은 월드 바람 오프셋을 적용해 분포 전체가 함께 이동하도록 한다.
- full-resolution은 활성 temporal 경로의 프레임 비용을 증가시키지 않으며, temporal 오류 비교와 debug 채널의 원본 관찰에 필요하므로 진단용으로 유지한다.
- 수정 후 Debug/Release 빌드와 Release ctest 2/2를 통과했다. Debug code test의 `temporalJitterAlignment`, `weatherAdvection`, `debugLayerClean`을 포함한 전 항목이 통과했고, 갱신한 flat v7 bundle의 cache smoke도 통과했다.
- 사용자 재검증에서 temporal toggle 직후 떨림은 사라졌다. weather를 포함한 구름 이동은 `windSpeed=0.1km/s`부터 명확히 보였지만 이동 중 상·하 경계 떨림이 남았다.
- `windSpeed`는 world km/s이므로 제안 범위 0.1~2.0은 100~2,000m/s다. 2.0km/s는 프레임 사이 이동과 history rejection이 지나치게 커지므로 UI는 0~200m/s로 표시하고 기본 showcase 값은 100m/s로 조정한다.
- 이동 중에는 바람 자체가 시간별 sample phase를 제공하므로 별도 4-frame camera jitter를 끈다. 정지 구름에서는 기존 jitter를 유지해 네 subpixel을 누적하고, animation 상태가 바뀔 때 history를 reset한다.
- Debug/Release 빌드, Release ctest 2/2, flat v7 cache smoke를 통과했다. Debug code test에서 `animatedJitterDisabled`, `temporalJitterAlignment`, `weatherAdvection`, `debugLayerClean`을 포함한 전 항목이 통과했다.
- 수정 후 사용자가 이동 중 상·하 경계 안정성을 다시 승인해야 한다.
- scene depth와의 교차는 아직 없으므로 실제 장면 합성 시 별도 depth rejection 확장이 필요하다.
