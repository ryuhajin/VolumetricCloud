# 레이마칭 (Ray Marching) 이해하기

이 문서는 `RaymarchSphere.hlsl`에서 사용하는 레이마칭 수식을 단계별로 설명합니다.
볼류메트릭 클라우드의 핵심 뼈대가 모두 여기에 들어 있습니다.

## 0. 큰 그림

레이마칭은 카메라에서 픽셀 방향으로 **광선(ray)** 을 쏘고, 그 광선을 따라
**조금씩 전진(march)** 하며 공간을 샘플링하는 기법입니다. 표면(메시)이 없어도
"공간에 퍼진 밀도(density)"를 적분해서 안개·구름 같은 볼륨을 그릴 수 있습니다.

```
카메라 ●───────► (픽셀 레이)
            t0│■■■■■■■■■│t1   ← 구 내부 구간을 조금씩 적분
              구 진입   구 탈출
```

## 1. 픽셀 → 월드 레이

각 픽셀의 화면 좌표 `uv`(0~1)를 NDC(-1~1)로 바꾸고, 역 뷰-투영 행렬로
근/원 평면 위 점을 역투영해 레이를 만듭니다.

```hlsl
float2 ndc = float2(uv.x*2-1, 1 - uv.y*2);          // y 뒤집기
float4 nearH = mul(float4(ndc, 0, 1), invViewProj); // 근평면
float4 farH  = mul(float4(ndc, 1, 1), invViewProj); // 원평면
float3 nearP = nearH.xyz / nearH.w;                 // 원근 나눗셈
float3 farP  = farH.xyz  / farH.w;
float3 ro = cameraPos;            // 레이 원점
float3 rd = normalize(farP-nearP);// 레이 방향
```

> **행렬 규약:** DirectXMath는 row-major(행 벡터) 규약이라, C++에서
> `XMMatrixTranspose` 후 업로드하고 HLSL에서 `mul(vector, matrix)`로 곱합니다.

## 2. 해석적 ray-sphere 교차

구는 SDF로도 행진할 수 있지만, 구는 **수식으로 정확히** 교차점을 구할 수 있어 효율적입니다.
레이 `p(t) = ro + t·rd`가 구 `|p - c| = r`와 만나는 `t`는 2차 방정식의 해입니다
(rd가 정규화되어 있어 `a=1`).

```
oc = ro - center
b  = dot(oc, rd)
c  = dot(oc, oc) - r²
판별식 h = b² - c
  h < 0  → 빗나감 (교차 없음)
  h ≥ 0  → t0 = -b - √h (진입),  t1 = -b + √h (탈출)
```

`t0`은 카메라가 구 안/뒤에 있을 수 있으니 `max(t0, 0)`으로 클램프합니다.

## 3. 밀도 적분 + Beer-Lambert 법칙

`[t0, t1]` 구간을 일정한 스텝 `dt`로 나눠 전진하며 각 지점의 밀도를 적분합니다.
빛이 매질을 통과할 때 남는 비율(**투과율, transmittance**)은 Beer-Lambert 법칙을 따릅니다.

```
T(통과 거리 dt) = exp(-density · dt)
누적 투과율  transmittance *= exp(-density · dt)   (스텝마다)
최종 불투명도 alpha = 1 - transmittance
```

현재는 구 내부 밀도를 **상수**(`densityScale`)로 둡니다. 그래서:

- 레이가 구의 **중심**을 지나면 통과 거리가 길어 `alpha`가 커지고(불투명),
- **가장자리**를 스치면 통과 거리가 짧아 `alpha`가 작아집니다(투명).

이 거리 차이가 "가장자리가 부드럽게 비치는 반투명 구"를 만듭니다.

```hlsl
float dt = (t1 - t0) / STEPS;
float transmittance = 1.0;
for (int i = 0; i < STEPS; ++i) {
    float density = densityScale;            // (추후 여기에 noise)
    transmittance *= exp(-density * dt);     // (추후 여기에 light)
}
float alpha = 1.0 - transmittance;
```

## 4. 합성

절차적 하늘색 위에 균일한 안개색을 `alpha`로 보간합니다.

```hlsl
float3 color = lerp(skyColor, fogColor, alpha);
```

## 5. SDF 행진 방식과의 비교 (참고)

| 방식 | 설명 | 장단점 |
|------|------|--------|
| **해석적 교차 + 내부 행진** (채택) | 구 교차를 수식으로 구하고 내부만 행진 | 빠르고 정확. 구름(임의 밀도장)으로 자연 확장 |
| **SDF 스피어 트레이싱** | 거리장(SDF)으로 표면까지 점프 행진 | 임의 형상에 유연하나, 볼륨 적분엔 추가 작업 필요 |

볼류메트릭 클라우드는 "임의 밀도장을 일정 스텝으로 적분"하는 방식이 표준이라,
이 프로젝트는 해석적 교차 + 내부 행진을 토대로 잡았습니다.

## 6. 여기서 구름으로 가는 길

- `density`에 3D noise(Perlin/Worley)를 곱하면 → 뭉게구름 형태.
- 적분 루프 안에서 **태양 방향으로 추가 레이**를 쏴 그림자를 모으면 → 라이팅.
- 단계별 계획은 [ROADMAP.md](ROADMAP.md) 참고.
