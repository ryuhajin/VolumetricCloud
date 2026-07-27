# 셰이더 컴파일 · `.cso` · 노이즈 캐시 관리

> "로딩 속도를 줄이려고 미리 컴파일해 둔다"는 게 처음이라면 이 문서부터. 왜 미리 컴파일하면
> 빨라지는지, 그리고 CPU가 컴파일된 바이트코드를 어떻게 GPU로 올리는지를 코드 기준으로 설명.

---

## 1. 왜 "미리 컴파일"하는가

HLSL(`.hlsl`)은 **사람이 읽는 소스 코드**다. GPU는 이걸 바로 실행하지 못한다.
중간 단계인 **DXBC 바이트코드**로 컴파일해야 하고, 그걸 드라이버가 각 GPU에 맞는
기계어로 다시 변환한다.

```
 HLSL 소스(.hlsl) ──D3DCompile──▶ DXBC 바이트코드(.cso) ──CreateXxxShader──▶ GPU 셰이더 객체
   사람이 읽음                       중간 표현(이식 가능)                       드라이버가 GPU 코드로
```

이 프로젝트는 **런타임 컴파일**과 **미리 컴파일된 `.cso` 로드**를 둘 다 지원하는 하이브리드다.

| | 런타임 컴파일 | 미리 컴파일된 `.cso` 로드 |
|---|---|---|
| 시점 | 첫 실행 / 소스 변경 시 | 두 번째 실행 이후(캐시 히트) |
| 하는 일 | `D3DCompileFromFile`로 HLSL을 그 자리에서 컴파일 (셰이더 5개) | 파일에서 바이트코드를 읽어 바로 셰이더 객체 생성 |
| 노이즈 볼륨 | 컴퓨트 셰이더로 128³+64³ 새로 굽기 | 디스크(`.vcnoise`)에서 읽어 바로 업로드 |
| 대략 비용 | 컴파일 5회(수백 ms) + 굽기(~2 s) ≈ **~3 s** | 컴파일 0회 + 파일 로드 ≈ **~300 ms** |

**미리 컴파일의 이점 정리**
- **시작 지연 제거**: 매 실행마다 컴파일러(`d3dcompiler`)를 돌리는 비용을 없앤다.
- **노이즈 굽기까지 생략**: 셰이더뿐 아니라 3D 노이즈 볼륨도 함께 캐시 → 컴퓨트 디스패치 0회.
- **결정적(재현 가능)**: 같은 소스 → 같은 바이트코드. 실행마다 결과가 흔들리지 않는다.
- **런타임 컴파일러 의존 축소**: 배포판에서 컴파일 실패 리스크가 줄어든다.

> 참고: 이 프로젝트는 빌드 타임에 `fxc`/`dxc`로 미리 컴파일하지 않는다. 대신
> **"한 번 런타임 컴파일한 결과를 `.cso`로 캐시"**해 두 번째 실행부터 재사용하는 방식이다.

---

## 2. CPU → GPU 업로드 흐름 (핵심)

`.cso` 파일 하나가 실제로 GPU에서 돌아가는 셰이더가 되기까지의 데이터 이동:

```
 [디스크]  main_ps.cso
    │  ① ReadFile
    ▼
 [CPU RAM]  std::vector<unsigned char>   (원시 바이트)
    │  ② D3DCreateBlob + memcpy  →  ID3DBlob
    │     D3DReflect 로 "진짜 유효한 DXBC냐" 검증
    ▼
 [CPU RAM]  ID3DBlob  (GetBufferPointer() / GetBufferSize())
    │  ③ CreateVertexShader / CreatePixelShader / CreateComputeShader(ptr, size)
    ▼
 [GPU VRAM]  ID3D11PixelShader ...   ← 드라이버가 바이트코드를 GPU로 올려 셰이더 객체 생성
```

- **①②** — `NoiseCacheManager::LoadBlob` (`src/NoiseCacheManager.cpp`). 파일을 읽어 블롭에
  담고 `D3DReflect`로 손상/포맷 검증까지 한다.
- **③** — `Renderer::CreateShadersFromBlobs` (`src/Renderer.cpp:229-275`). 블롭의
  `GetBufferPointer()`/`GetBufferSize()`를 `CreateVertexShader/PixelShader/ComputeShader`에
  넘기면, **이 API 호출 안에서 드라이버가 바이트코드를 GPU로 업로드**하고 셰이더 객체를 만든다.

```cpp
// Renderer.cpp:248  (핵심 한 줄)
m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &newPs);
```

### 노이즈 3D 볼륨은 경로가 다르다
셰이더 바이트코드와 달리, 노이즈 볼륨은 **텍스처 초기 데이터**로 올린다.
`CreateTexture3D` 호출 시 `D3D11_SUBRESOURCE_DATA`(`pSysMem`, `SysMemPitch`,
`SysMemSlicePitch`)에 CPU 버퍼를 넘기면 **생성과 동시에 GPU VRAM으로 복사**된다.
별도의 `CopyResource` 없이 한 번에 올라간다 (`NoiseCacheManager.cpp` `LoadVolume`).

---

## 3. 컴파일 파이프라인 상세

### 빌드 타임 (CMakeLists.txt POST_BUILD, 76-86행)
컴파일이 아니라 **복사**만 한다. 빌드 후 출력 폴더로:
- `shaders/` → `<빌드>/shaders/` (런타임에 HLSL을 읽어 컴파일/핫리로드하기 위함)
- `assets/noise-cache/` → `<빌드>/assets/noise-cache/` (기본 캐시 번들 동봉)

소스 경로는 매크로로 주입된다 (개발 빌드는 원본 폴더를 직접 봄):
`VCLOUD_SHADER_SOURCE_DIR`, `VCLOUD_CACHE_SOURCE_DIR` (CMakeLists 63-64행).

### 런타임 컴파일 (`Renderer::CompileShaderFromFile`, 173-203행)
```cpp
D3DCompileFromFile(path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                   entryPoint, target, compileFlags, 0, &outBlob, &errors);
```
- 엔트리/타겟: `main`(vs_5_0/ps_5_0), `VSMain`/`PSMain`(프리뷰), `CSMain`(cs_5_0).
- `_DEBUG` 빌드는 `D3DCOMPILE_DEBUG | SKIP_OPTIMIZATION`, 릴리스는 최적화.
- 컴파일하는 셰이더 5개: `main_vs`, `main_ps`, `preview_vs`, `preview_ps`, `noise_cs`
  (`Renderer::CreateShaders`, 205-227행).

### 핫리로드 (`Renderer::CheckShaderHotReload`, 287-313행)
매 프레임 끝에 HLSL 파일들의 `last_write_time`을 캐시된 값과 비교한다.
바뀌었으면 `CreateShaders(false)`로 조용히 재컴파일하고, **실패하면 기존 셰이더를 유지**해
매 프레임 같은 에러창이 뜨는 걸 막는다. 개발 중 셰이더를 저장하면 바로 반영된다.

---

## 4. NoiseCacheManager — 캐시 관리

`src/NoiseCacheManager.{h,cpp}`. 셰이더 바이트코드(`.cso`)와 노이즈 볼륨(`.vcnoise`)을
한 **번들**로 묶어 저장/로드한다.

### 캐시 위치 2곳 (로드 우선순위: 사용자 → 기본)
| 종류 | 경로 | 용도 |
|------|------|------|
| 사용자 캐시 | `%LOCALAPPDATA%\VolumetricCloud\cache\bundle\` | UI에서 저장한 커스텀 결과 |
| 기본 캐시 | `assets/noise-cache/bundle\` (빌드 시 동봉) | 배포판 기본값 |

`LoadPreferred`가 사용자 캐시를 먼저 시도하고, 없거나 무효면 기본 캐시로 폴백한다.

### 번들 구성
```
bundle/
  ├ manifest.bin       ← 매직/버전/해시/파라미터 (유효성 판정)
  ├ main_vs.cso         ┐
  ├ main_ps.cso         │
  ├ preview_vs.cso      ├ 셰이더 바이트코드 6개
  ├ preview_ps.cso      │
  ├ noise_cs_base.cso   │  (base/detail 컴퓨트 분리)
  ├ noise_cs_detail.cso ┘
  ├ base.vcnoise       ← 128³ R8     (2 MB, 실루엣 단일값)
  └ detail.vcnoise     ← 64³  RGBA8  (1 MB, Worley 옥타브 4채널)
```
`.vcnoise`는 헤더(매직/버전/크기/포맷) + 원시 볼륨 바이트. `.cso`는 순수 DXBC 바이트코드.

### 캐시 검증 — 두 개의 해시 (FNV-1a)
`manifest.bin`에는 두 해시가 저장되고, 로드 시(`LoadBundle`, 225-258행) 각각 다르게 쓰인다.

- **ParameterHash** — 노이즈 생성 파라미터(worldScale/period/seed/octaves)의 해시.
  로드 시 `manifest.parameterHash != ParameterHash(manifest.params)`이면 **캐시를 거부**한다.
  이건 "번들이 손상/변조되지 않았는가"를 확인하는 **무결성 검사**이자, *이 볼륨이 어떤
  파라미터로 구워졌는지*를 기록하는 서명이다. (매직/버전/크기/포맷도 함께 검증)
- **SourceHash** — 모든 HLSL 파일 **내용**의 해시. 로드는 성공시키되
  `sourceModified = (manifest.sourceHash != SourceHash())`로 **소스 변경 여부만 표시**한다
  → 상태가 `"Source Modified"`가 되고, 핫리로드가 재컴파일을 유도한다.

> 주의: **UI에서 노이즈 파라미터를 바꿨을 때**의 재생성은 이 해시와 무관하다. 런타임에서
> 파라미터 변경을 감지하면 `m_noiseCacheDirty`를 세워 `GenerateNoiseVolumes()`로 즉시 다시
> 굽는다 (`Renderer.cpp:483-492`). 해시는 어디까지나 **디스크 캐시를 로드할 때의 검증**용이다.

### 저장 경로 (GPU → 디스크)
GPU 텍스처는 CPU가 직접 못 읽으므로 **스테이징 텍스처**를 거친다:
```
GPU 텍스처 ──CopyResource──▶ STAGING 텍스처 ──Map(READ)──▶ CPU 버퍼 ──write──▶ .vcnoise
                                            (RowPitch/DepthPitch 정렬 보정)
```
`SaveVolume`이 이 과정을 담당한다.

### 원자적 저장 (안전 교체)
`SaveBundle`은 `bundle.tmp`에 먼저 전부 쓴 뒤 rename으로 교체한다.
중간에 실패하면 `bundle.old` 백업으로 **롤백**한다 → 저장 중 크래시가 나도 기존 캐시가
깨지지 않는다.

### 자체 검증 테스트
`RunRoundTripTest`: 저장→로드→재저장 결과를 **바이트 단위로 비교**하고, manifest를
일부러 손상시켜(버전/해시/포맷) 제대로 **거부**하는지까지 확인한다.
(`main.cpp --cache-smoke-test` 등 CLI 옵션으로 실행)

---

## 5. 캐시 히트 vs 미스 (성능)

| | 캐시 히트 | 캐시 미스 (첫 실행/소스 변경) |
|---|---|---|
| HLSL 컴파일 | 0회 | 5회 (`D3DCompileFromFile`) |
| 노이즈 굽기 | 0회 (파일 로드) | 컴퓨트 디스패치 2회 (128³+64³) |
| 대략 시작 시간 | **~300 ms** | **~3 s** |

→ 미리 컴파일 + 노이즈 캐시로 시작 시간을 **약 10배** 단축.

---

## FAQ (다시 잊었을 때)

### Q. 노이즈 캐시랑 `.cso`는 따로인가? 노이즈 캐시 = 미리 구워진 텍스처?
따로다. **`.cso` = 셰이더 코드(바이트코드)**, **`.vcnoise` = 그 코드가 계산해 둔 노이즈
3D 텍스처 데이터**. "노이즈 캐시 = 미리 구워진 노이즈 텍스처"가 맞는 이해다.

**왜 굽나?** 레이마칭이 픽셀마다 64스텝을 돌고 매 스텝 노이즈를 샘플하는데, 절차식 노이즈
(`PeriodicPerlinWorley` + `PeriodicWorleyFBM`)는 여러 옥타브 FBM+Worley라 매우 무겁다.
노이즈는 매 프레임 동일하므로(바람은 UV 오프셋으로 처리) 한 번 텍스처에 구워두고 이후엔
**싼 trilinear 조회 1번**으로 대체한다 = 공간↔시간 트레이드오프. `useTextureCache`로 절차식↔캐시 전환.

### Q. Compute Shader를 쓰나? PS/VS랑 뭐가 다른가?
쓴다. **`NoiseVolumeCS.hlsl`**의 두 엔트리 `CSBase`/`CSDetail`(`cs_5_0`). 노이즈 굽기가 CS의 일이다.
(base=R8 단일값, detail=RGBA8 옥타브로 형식이 달라 엔트리를 나눴다 — 아래 최적화 항목 참고.)

| | VS / PS | Compute Shader |
|---|---|---|
| 실행 | `Draw()` (래스터화 O) | `Dispatch()` (래스터화 X) |
| 단위 | 정점/픽셀마다 (파이프라인이 지정) | 내가 정의한 스레드 그리드 |
| 출력 | 렌더 타깃의 고정 위치 | **UAV**에 임의 위치 쓰기 (RWTexture3D 등) |

**"스레드 1개 = 복셀 1개"** 구조. `[numthreads(4,4,4)]`, `SV_DispatchThreadID`가 복셀 좌표.
128³ = 약 210만 복셀을 GPU가 병렬로 계산해 `outputBase[id]`/`outputDetail[id]`에 직접 쓴다.
CPU는 `Dispatch(32,32,32)`로 부른다 (`Renderer.cpp:368-397`). 요약: **PS는 "화면에 그리고",
CS는 "데이터를 계산해 메모리에 채운다"**.

### Q. `.vcnoise`는 어떻게 생성되나? 라이브러리를 쓰나?
**라이브러리 없이** 표준 C++ 파일 입출력만 쓴다 (`SaveVolume`, `NoiseCacheManager.cpp:135-177`).
DDS 같은 이미지 포맷도 아니고, 자체 포맷(헤더 + 원시 바이트)이다:
```
GPU 텍스처 ─CopyResource→ STAGING 텍스처 ─Map(READ)→ CPU 버퍼 ─std::ofstream.write→ .vcnoise
             (GPU는 CPU가 못 읽으므로               (RowPitch/DepthPitch 정렬 보정)
              읽기 가능한 스테이징으로 복사)
```
`VolumeHeader`(매직/버전/크기/포맷) 40바이트 뒤에 복셀 데이터를 그대로 붙인다.
base = 128³ × 1바이트(R8) = 2 MB, detail = 64³ × 4바이트(RGBA8) = 1 MB.

### Q. 볼륨 크기/채널 최적화는 어떻게 됐나? (v3에서 적용됨)
예전엔 두 볼륨 모두 `R8G8B8A8`인데 CS가 `float4(v,v,v,1)`로 같은 값을 네 채널에 복제해 굽고
셰이더는 `.r`만 읽어 **G·B·A 3채널이 낭비**됐다(8 MB + 1 MB). v3에서 볼륨 성격에 맞게 나눴다:

| 볼륨 | 이전 | 현재 | 효과 |
|------|------|------|------|
| base (실루엣 단일값) | 128³ RGBA8 (8 MB) | **128³ R8 (2 MB)** | 손실 0(읽는 `.r` 비트 동일), **4배↓** |
| detail (침식 Worley) | 64³ RGBA8, 옥타브를 평균해 R에 (1 MB) | 64³ RGBA8, **옥타브 4개를 R/G/B/A에 분리** (1 MB) | 크기 동일, 침식 품질↑ + 렌더 시점 튜닝 |

- detail은 `DetailNoiseOctaves`가 Worley 옥타브 4개를 채널로 굽고,
  `DetailErosionFromChannels`(가중치 상수)가 렌더 시점에 재합성 → **재굽기 없이 HLSL에서 결 조절**.
- 기각한 대안: **DDS/DirectXTex** — D3D11은 Texture3D에 **BC 압축 미지원**이라 크기 이득이 없고
  의존성만 늘어 커스텀 포맷을 유지했다. **해상도 축소/디스크 압축**은 각각 디테일 손실/런타임 해제
  비용이 있어 보류.
- 전체 9 MB → **3 MB**. 관련 코드: `shaders/NoiseVolumeCS.hlsl`, `CloudNoise.hlsli`,
  `NoiseCacheManager.cpp`(볼륨별 포맷 `kBaseVolumeFormat`/`kDetailVolumeFormat`, `kCacheVersion=3`).

### Q. 해시가 꼭 필요한가? 파일 수정 시각(타임스탬프)으로 체크하면 안 되나?
사실 이 프로젝트는 **둘 다** 쓴다. 용도가 다르다.
- **타임스탬프** — 실행 중 셰이더 파일이 바뀌면 즉시 재컴파일하는 **핫리로드**에 쓴다
  (`CheckShaderHotReload`, `last_write_time`). 한 세션 안에서 "방금 저장했나?"엔 타임스탬프가 싸고 충분.
- **내용 해시** — 디스크에 저장돼 **배포·공유되는 영구 캐시** 검증에 쓴다. 여기선 타임스탬프가
  못 미덥다:
  - git clone/checkout/복사/압축해제하면 내용이 같아도 **타임스탬프가 리셋** → 거짓 "변경됨".
  - 다른 PC로 캐시를 동봉하면 타임스탬프는 **의미 없음**.
  - 고쳤다 되돌리면 내용은 같은데 **타임스탬프만 달라짐** → 거짓 양성.
  - `ParameterHash`는 애초에 "시각"이 아니라 *어떤 값으로 구웠나*의 서명이라 타임스탬프로 표현 불가.

  내용 해시는 **실제 내용 변화만** 잡고, 결정적이며 기계 간 이식 가능하다. 대신 검사 때
  파일을 읽어 해싱하는 비용이 있지만(셰이더 6개) 무시할 수준이다.

→ 요약: **세션 내 즉각 감지 = 타임스탬프, 영구·이식 캐시 검증 = 해시.** 각자 맞는 자리에 쓰인다.

---

## 참조 코드
- 런타임 컴파일: `src/Renderer.cpp:173-203` (`CompileShaderFromFile`), `:205-227` (`CreateShaders`)
- 블롭 → GPU 셰이더: `src/Renderer.cpp:229-275` (`CreateShadersFromBlobs`)
- `.cso` 로드/검증: `src/NoiseCacheManager.cpp` (`LoadBlob`)
- 볼륨 로드/저장: `src/NoiseCacheManager.cpp` (`LoadVolume` / `SaveVolume`)
- 번들 로드/저장/해시: `src/NoiseCacheManager.cpp` (`LoadPreferred`/`LoadBundle`/`SaveBundle`/`SourceHash`/`ParameterHash`)
- 빌드 복사: `CMakeLists.txt:76-86`
- `.cso`는 git 추적 안 함: `.gitignore` (`*.cso`)
