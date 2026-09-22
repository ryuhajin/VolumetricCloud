# 일반 앱 빌드와 로컬 테스트

## 일반 앱 (원격 배포 기본)

원격에는 일반 실행 코드·필수 빌드 설정·원본 프리셋·공식 `doc/` 문서를 반영한다.
테스트 소스·분석 스크립트·검사용 셰이더·개인 노트·캡처·실행 파일은 배포하지 않는다.
기존 Git 이력의 테스트 자료는 역사 자료이며 현재 일반 빌드의 입력이 아니다.

```powershell
git submodule update --init --recursive
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --build build --config Debug
```

`VCLOUD_BUILD_TESTS=OFF`가 기본이다. `build/Release/VolumetricCloud.exe` 또는 Debug 실행 파일은
일반 창·F1~F4·F5~F8·카메라·렌더 루프만 실행한다. 자동 테스트 CLI는 제공하지 않는다.
`src/`, `shaders/`, `presets/`, ImGui submodule과 CMake만으로 빌드되며 `tests/`나 검사 도구가 없어도 된다.
셰이더는 소스 우선/실행 파일 옆 폴백, 프리셋은 원본 8JSON의 기존 저장·배치 계약을 유지한다.
대기 조회 거리2배, 정규화 Detail remap, 64³ 무보정 Worley fBm은 이미 일반 렌더의 기본값이다.

## 로컬 검증 패키지가 있는 개발 체크아웃

로컬 `tests/`를 보존한 개발 체크아웃에서만 다음 명령을 사용한다.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DVCLOUD_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -j1
```

일반 앱과 별도로 `build/validation/Release/VolumetricCloudTestRunner.exe`가 만들어진다.
기존 `--high-cloud-smoke-test`, `--detail-core-slider-smoke` 등의 CLI는 이 검사 앱에서 실행한다.
Debug도 동일하게 `build/validation/Debug/`를 사용한다. 기본 CPU/기능 테스트와 성공 산출물 정리 계약은 유지한다.
테스트 패키지가 없는 공개 체크아웃에서 ON을 요청하면 명확한 구성 오류를 내며 일반 앱으로 조용히 대체하지 않는다.
`VCLOUD_ENABLE_DIAGNOSTIC_TESTS=ON`은 `VCLOUD_BUILD_TESTS=ON`일 때만 사용할 수 있다.

일반 앱은 `VCLOUD_TEST_HOOKS=0`, 검사 앱은1로 공통 렌더 소스를 각각 컴파일한다.
readback/결정성 구현과 테스트 진입점은 검사 앱에만 연결한다. 일반 런타임의 셰이더 의존성에는 probe 파일이 없다.
로컬 검사는 빌드할 때 `shaders/` 원본을 `build/validation-shaders/`에 복사하고 검사 helper를 추가한다.
원본 수식은 하나이며 진단 파일을 일반 원본으로 복사하지 않는다. 로컬 검사에서 원본 shader 수정 후에는
먼저 빌드해 복사본을 동기화한다. 일반 앱의 원본 핫 리로드와 검사 앱의 격리 핫 리로드는 각각 유지한다.

## 문서와 원격 이력

다른 문서의 과거 `VolumetricCloud.exe --...test` 예시는 로컬 검사 앱의 경로로 바꿔 실행한다.
공개 체크아웃에서 CTest 검사가 없다고 해서 실행 성공이나 화면 품질이 검증되었다고 기록하지 않는다.
상세 검증/업로드/삭제 기록은 개인 노트에 보관한다. 화면 품질 승인과 main 병합은 별도 사용자 게이트다.
테스트를 포함한 로컬 미전송 commit을 그대로 push하지 않는다. 기존 원격 HEAD에 일반 코드만 반영한
커밋을 만들어 fast-forward로 반영하며, 원격 소스와 독립된 로컬 검증 이력을 보존한다.
