// ============================================================================
//  Noise.hlsli - 구름 렌더와 Noise Lab이 함께 사용하는 단계 3 밀도 함수
// ----------------------------------------------------------------------------
//  데이터 흐름
//  1. 월드 위치(m)를 바람이 이동시킨 noise 좌표로 바꾼다.
//  2. value noise와 coverage로 단계 2의 기본 덩어리 밀도를 만든다.
//  3. AABB 바닥/천장 사이의 높이 비율과 부드러운 높이 마스크를 계산한다.
//  4. 기본 밀도 × 높이 마스크 × 밀도 배율을 최종 Beer-Lambert 밀도로 쓴다.
//
//  단계 4 Detail Erosion, 단계 5 Weather, 단계 6 Light는 아직 적용하지 않는다.
// ============================================================================
#ifndef VCLOUD_NOISE_HLSLI
#define VCLOUD_NOISE_HLSLI

#include "CloudParameters.hlsli"

#ifndef VCLOUD_NOISE_TEST_BIAS
#define VCLOUD_NOISE_TEST_BIAS 0.0
#endif

struct CloudDensitySample
{
    float rawNoise;          // threshold 전 원본 value noise(0~1).
    float thresholdDensity; // coverage만 적용한 단계 2 기본 밀도(0~1).
    float heightFraction;    // AABB 바닥=0, 천장=1인 정규화 월드 Y 높이.
    float heightProfile;     // 위·아래 경계를 부드럽게 지우는 마스크(0~1).
    float finalDensity;      // threshold × heightProfile × multiplier 결과(0~1).
    float3 noiseUvw;         // value noise를 조회한 연속 좌표(cycle).
};

// 월드 Y 위치(m)를 구름층 안의 0~1 높이로 바꾼다.
// cloudBoundsMax.y <= cloudBoundsMin.y인 잘못된 AABB는 두께가 없으므로 0을 반환한다.
// 이 분기는 0 나눗셈과 NaN이 검정 화면이나 번쩍임으로 번지는 것을 막는다.
float EvaluateHeightFraction(float worldY)
{
    float cloudThickness = cloudBoundsMax.y - cloudBoundsMin.y;
    float validThickness = cloudThickness > 1e-6 ? 1.0 : 0.0;
    float safeThickness = max(cloudThickness, 1e-6);
    return saturate((worldY - cloudBoundsMin.y) / safeThickness) * validThickness;
}

// 정규화 높이에서 바닥 fade와 꼭대기 fade를 곱해 구름층 마스크를 만든다.
// bottomFadeEnd가 커지면 바닥의 흐린 구간이 넓어지고, topFadeStart가 작아지면
// 꼭대기의 흐린 구간이 넓어진다. 두 값이 교차해도 곱은 유효하지만 중앙의
// 완전한 밀도(plateau)가 사라진다. 0/1 경계는 smoothstep의 동일 edge를 피한다.
float EvaluateHeightProfileFromFraction(float heightFraction)
{
    float safeBottomEnd = clamp(bottomFadeEnd, 0.01, 0.99);
    float safeTopStart = clamp(topFadeStart, 0.01, 0.99);
    float bottomFade = smoothstep(0.0, safeBottomEnd, saturate(heightFraction));
    float topFade = 1.0 - smoothstep(safeTopStart, 1.0, saturate(heightFraction));
    return saturate(bottomFade * topFade);
}

// 정수 격자 모서리를 재현 가능한 0~1 난수로 바꾼다.
// 이 함수나 아래 보간식을 저장하면 Cloud PS와 Noise Lab PS가 함께 핫리로드된다.
float HashNoiseCorner(float3 latticePoint)
{
    float3 scrambled = frac(latticePoint * 0.1031);
    scrambled += dot(scrambled, scrambled.yzx + 33.33);
    return frac((scrambled.x + scrambled.y) * scrambled.z);
}

// 셀 여덟 모서리의 hash 값을 Hermite 곡선과 삼선형 보간으로 연결한다.
float SampleBaseNoise(float3 noiseUvw)
{
    float3 cell = floor(noiseUvw);
    float3 local = frac(noiseUvw);
    float3 smoothLocal = local * local * (3.0 - 2.0 * local);

    float n000 = HashNoiseCorner(cell + float3(0.0, 0.0, 0.0));
    float n100 = HashNoiseCorner(cell + float3(1.0, 0.0, 0.0));
    float n010 = HashNoiseCorner(cell + float3(0.0, 1.0, 0.0));
    float n110 = HashNoiseCorner(cell + float3(1.0, 1.0, 0.0));
    float n001 = HashNoiseCorner(cell + float3(0.0, 0.0, 1.0));
    float n101 = HashNoiseCorner(cell + float3(1.0, 0.0, 1.0));
    float n011 = HashNoiseCorner(cell + float3(0.0, 1.0, 1.0));
    float n111 = HashNoiseCorner(cell + float3(1.0, 1.0, 1.0));

    float x00 = lerp(n000, n100, smoothLocal.x);
    float x10 = lerp(n010, n110, smoothLocal.x);
    float x01 = lerp(n001, n101, smoothLocal.x);
    float x11 = lerp(n011, n111, smoothLocal.x);
    float y0 = lerp(x00, x10, smoothLocal.y);
    float y1 = lerp(x01, x11, smoothLocal.y);
    return saturate(lerp(y0, y1, smoothLocal.z) + VCLOUD_NOISE_TEST_BIAS);
}

// 월드 위치(m)와 시간(s)을 구름 렌더와 Noise Lab이 공유하는 밀도 표본으로 변환한다.
// 입력: 카메라와 무관한 월드 위치, 음수가 아닌 애니메이션 시간.
// 출력: noise 중간값, 높이 중간값, Beer-Lambert 적분에 넣을 최종 밀도.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds)
{
    CloudDensitySample sample = (CloudDensitySample)0;

    float windLength = length(windDirection);
    float3 safeWindDirection = windLength > 1e-6
        ? windDirection / windLength
        : 0.0.xxx;
    float3 stationaryWorld = worldPosition -
        safeWindDirection * max(windSpeed, 0.0) * max(timeSeconds, 0.0);
    float safeScale = max(baseNoiseScale, 1e-4);
    sample.noiseUvw = stationaryWorld * safeScale + noiseOffset.xxx;
    sample.rawNoise = SampleBaseNoise(sample.noiseUvw);

    float safeCoverage = saturate(coverage);
    if (safeCoverage > 1e-4)
    {
        float threshold = 1.0 - safeCoverage;
        sample.thresholdDensity = saturate(
            (sample.rawNoise - threshold) / safeCoverage);
    }
    // 높이 마스크가 없으면 AABB 바닥과 천장이 칼로 자른 듯 보인다. 단계 3은
    // X/Z 덩어리 위치를 바꾸지 않고 Y 경계에서만 밀도를 0으로 부드럽게 줄인다.
    sample.heightFraction = EvaluateHeightFraction(worldPosition.y);
    sample.heightProfile = EvaluateHeightProfileFromFraction(sample.heightFraction);
    sample.finalDensity = saturate(sample.thresholdDensity *
        sample.heightProfile * max(densityMultiplier, 0.0));
    return sample;
}

#endif
