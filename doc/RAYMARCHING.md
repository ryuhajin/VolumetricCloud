# 단계 0: 월드 레이와 깊이 복원

현재 단계는 레이 마칭을 수행하지 않는다. 이후 볼륨 교차와 적분이 사용할 입력을 먼저 검증한다.

## 화면 UV에서 월드 레이 복원

DirectX의 장치 깊이는 0이 near plane, 1이 far plane이다. UV의 Y축을 뒤집어 NDC로 바꾼 뒤 far plane 점을 역투영한다.

```hlsl
float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
float4 farH = mul(float4(ndc, 1, 1), invViewProj);
float3 farPosition = farH.xyz / farH.w;
float3 rayDirection = normalize(farPosition - cameraPos);
```

`invViewProj`는 meter 단위 월드 좌표를 반환한다. 0 나누기를 피하기 위해 homogeneous `w`의 절댓값이 `1e-6`보다 작은 경우 안전값을 사용한다.

## Scene Depth에서 월드 위치 복원

진단 장면의 `D32_FLOAT` 깊이를 `R32_FLOAT` SRV로 읽고 동일한 역투영을 수행한다.

```hlsl
float depth = sceneDepthTexture.SampleLevel(pointSampler, uv, 0);
float4 worldH = mul(float4(ndc, depth, 1), invViewProj);
float3 worldPosition = worldH.xyz / worldH.w;
float sceneDistance = length(worldPosition - cameraPos);
```

깊이가 1이면 불투명 기하가 없는 하늘 픽셀로 취급하고 `farPlane`을 장면 거리로 사용한다. 단계 1의 볼륨 추적은 이 거리보다 뒤로 진행하지 않는다.

## 단계 0 합성

향후 모든 구름 패스는 다음 관계를 유지한다.

```text
finalColor = cloudScattering + backgroundColor * cloudTransmittance
```

현재는 합성 경로를 확인하기 위한 반투명 진단 원판만 출력한다. AABB, 밀도, Beer-Lambert 적분은 단계 1에서 추가한다.
