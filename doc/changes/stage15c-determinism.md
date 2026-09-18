# Stage 15C 결정성 검증 및 수정

## 2026-09-05 검증 정책 변경 (현재 규약)

사용자 요청으로 기본 Release를 주력 TC/성능/tolerance 이미지 회귀 대상으로 되돌렸다.
`VCLOUD_STRICT_VALIDATION=OFF`가 기본이며 기존 Weather의 `/Gis`만 유지한다.
ON은 별도 디렉터리에 필요 시 만드는 Strict Validation으로 모든 패스에 `/Gis`를 적용한다.
`DeterminismSmoke`는 ON에서만 등록하며 `RendererStateSmoke`는 양쪽에서 실행한다.
CameraCB 실제 결함 수정, High 상수, ABI와 기본값은 그대로 유지한다.
DXBC 키에 컴파일 플래그가 포함되며 진단 로그에 빌드 정책을 출력한다.

아래 all-strict 37/38 및 성능 실패는 정책 분리 전의 역사적 결과다. 배포 Release까지
bit-exact를 요구했던 이전 계약은 변경되었으며, 기존 계약을 달성한 것으로 간주하지 않는다.
Strict 성능 실패를 이제 배포 gate로 삼지 않지만 OFF Release 성능은 별도로 재검증한다.
최종 HDR/Tone tolerance 이미지 회귀 TC는 아직 없으며 baseline/임계값 확정과 구현은 후속이다.
명령과 승인 규약은 [CONTRIBUTING](../CONTRIBUTING.md)에 기록한다.

정책 분리 검증: OFF Debug/Release 및 ON Release 실행 파일 빌드 성공.
CTest 목록은 OFF 38개(Strict matrix 제외), ON 39개(포함)다.
OFF Release 전체 37/38 통과. 유일한 실패는 Meadow InsideLayer Cloud p95
10.289ms > 10ms이며 Frame p95는 10.833ms다 (`build/build-policy-release.log`).
컴파일 작업과 일부 시간이 겹친 측정이므로 과거 10.544ms와의 차이를 순수 `/Gis` 비용으로 단정하지 않는다.
ON Release 보조 TC 3/3 통과 (`build/build-policy-strict.log`): 독립 캐시 3세트의
cold/warm/detail matrix, 비교기 자기 검사, CameraCB 상태 회귀다. OFF Debug의
High/상태 GPU smoke도 2/2 통과 (`build/build-policy-debug.log`). `git diff --check` 통과.
Strict 마지막 상태 검사와 Debug 검사의 실행 시간이 일부 겹쳐 Strict 상태 검사는
별도 직렬 실행으로도 확인한다 (`build/build-policy-strict-state-isolated.log`).

## 계약

동일 GPU/드라이버/컴파일러/빌드에서 320×180, 3 scene × 4 camera × 첫 4 frame의
Weather/HDR/Tone 비트를 비교한다. Debug와 Release 사이의 해시는 비교하지 않는다.
각 빌드는 독립 DXBC 파일 캐시 3세트에서 cold 1회, warm 2회, 상세 readback 1회를
실행한다. cold는 앱 파일 캐시에 한정하며 시스템/드라이버 캐시는 지우지 않는다.
사용자 저장 설정과 핫리로드는 테스트에 영향을 주지 않도록 격리한다.

## 확인한 원인과 수정

1. 기존 Weather만 IEEE strictness를 사용하고 다른 패스는 연산 재배치를 허용했다.
   동일 DXBC/CPU 키/Noise/LUT/CB 내용에도 특정 frame의 Shadow 또는 Tone 출력이 달랐다.
   Shadow bytecode의 기존 `mad`와 새 `[precise]` 연산을 fxc dumpbin으로 확인했다.
   모든 활성 프로그램에 `D3DCOMPILE_IEEE_STRICTNESS`를 적용한 matrix는 통과했고,
   Cloud/Shadow/Tone 세 프로그램만 적용하면 다른 scene의 HDR에서 다시 실패했다.
   CameraCB 수정은 유지하고 strictness만 제외한 실행 파일도 다시 실패했다.
   **연산 재배치 허용 계약이 현재 환경의 비트 결정성 요구를 충족하지 못한 것**으로
   결론 내린다. 드라이버 내부 JIT의 동작 방식이나 특정 드라이버 버그를 단정하지 않는다.
2. 대기 LUT의 CameraCB 직접 Map은 time=0을 쓰지만 기존 업로드 캐시는 이전 time을
   기억했다. 같은 시간/카메라에서 LUT 재생성 후 Cloud 업로드가 생략되는 별도 결함이다.
   직접 쓰기 성공 직후 CameraCB 업로드 캐시만 무효화한다. GPU b0의 time을 읽는 회귀는
   수정 전 FAIL, 수정 후 PASS, 이 무효화만 제외하면 다시 FAIL이다.
3. Debug의 독립 재컴파일은 SPDB debug 정보만 달라졌다. RDEF/ISGN/OSGN/SHEX/STAT은
   같음을 확인했다. 원본 DXBC hash를 보존하고 비교용 hash만 debug 정보를 제거한다.
   실행 blob과 reflection, 텍스처 해시는 변경하거나 정규화하지 않는다.

`/Gis`는 IEEE 규칙을 깨는 최적화를 제한하는 옵션이며 `/Od` 전체 최적화 해제와 다르다.
근거: [D3DCOMPILE 상수](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/d3dcompile-constants),
[precise 연산 계약](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/precise).

## 재현 증거

- 기존 실패: `build/determinism-before/610196254e7e4af3b144a16638fdaa84`.
- 상세 실패: `build/determinism-trace/9b9b8fdc048d4940abc3c396c05d8624`.
  frame 8 Near shadow 최초 차이는 (149,0,slice 0), `0x3ac720fa → 0x3ac7201a`,
  차이 원소 3,706,175개, 최대 절대 차이 0.00002324953675.
  HDR은 (55,124), R `0x2f53 → 0x2f54`, 차이 원소 2개, 최대 0.00006103515625.
- strictness 제외 대조: `build/determinism-ab/without-strict/d5c545a92fdd4691949c92eca6325c79`.
- CameraCB 무효화 제외 실행 파일: `build/determinism-ab/WithoutCameraInvalidation.exe`.
- 중간 성공 로그: `build/determinism-release.log`, `build/determinism-debug-final.log`.
- 초기 strictness 성능 검사에서는 Meadow InsideLayer p95=10.544ms로 10ms gate를 넘었다.
  결정성 성공만으로 완료하지 않고 성능도 재검증한다.
- O3만 적용한 실험은 10.287ms, precheck 중복 계산 재사용 실험은 10.544ms로 실패했다.
  두 실험은 효과가 입증되지 않아 제거했다. 최종 코드는 기존 최적화 level과 HLSL 수식을 유지한다.

## 검증 인터페이스

- `--determinism-smoke-test`: UI/자동 scan 없이 모든 frame을 기록한다.
- `--determinism-detailed`: Base/Detail, 6 LUT, Near/Far, Scene/Depth와 CB 내용을 추가한다.
- `--determinism-state-test`: 같은 시간 LUT 재생성과 resize/복귀 후 GPU CameraCB를 검사한다.
- `VCLOUD_TEST_SHADER_CACHE`: 결정성 모드에서만 사용하는 전용 캐시.
- `VCLOUD_TEST_DUMP_FRAME`, `VCLOUD_TEST_DUMP_ROOT`: 지정 프레임의 원본 바이너리 진단.
- `tests/RunDeterminism.ps1`: 새 프로세스 직렬 실행, 캐시 카운터/누락/중복/비트 비교.
- `tests/CompareDeterminismDump.ps1`: 원본 좌표·채널·비트·수치 차이 확인.

캐시/덤프/로그는 build 하위 UUID 디렉터리에 보존한다. 기존 캐시나 증거를 삭제하지 않는다.
캡처 생성/Map 실패, 비유한 HDR, device removal, 누락 기록은 실패다. 행/면 padding은 제외한다.
기존 LastCloudFrameHash는 여전히 Tone 이후 RGBA8 해시다. HDR은 별도로 기록한다.

## 검증 및 승인 상태

Debug/Release 빌드 성공. Release 전체 CTest **37/38 통과**이며 유일한 실패는
HighPerformance의 Meadow InsideLayer(Cloud p95 10.544ms > 10ms)다.
로그는 `build/determinism-release-full.log`. 전체 합산 p95가 gate 아래여도 case별 실패를 무시하지 않는다.
Debug GPU High/Atmosphere와 독립 캐시 3세트 결정성 matrix는 통과했으며
`build/determinism-debug-final.log`와 인계 재검증 `build/determinism-debug-handoff.log`에 남긴다.
인계 Debug 검증은 3/3 통과했으며 matrix의 상태 전환 검사도 PASS다.
각 matrix는 9개 기본 계측 프로세스와 3개 상세 계측 프로세스의 48 frame을 비교하고,
같은 시간 LUT 재생성 및 resize/복귀 상태 회귀를 추가로 실행한다.
실패·성공·원인 수정만 제외한 재실패 대조를 보존했다. **자동검증 최종 완료는 성능 gate 때문에 보류**한다.
High 상수·기본값·cbuffer ABI는 변경하지 않았다.
연산 보존에 따라 과거 출력과 비트 단위 차이가 생기므로 과거 해시를 새 정답으로 덮어쓰지 않는다.
화면 외관 재확인은 사용자에게 요청하며 기존 2026-09-05 승인을 이번 수정의 새 승인으로 간주하지 않는다.
