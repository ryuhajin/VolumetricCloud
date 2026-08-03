// ============================================================================
//  Noise.hlsli - 구름 렌더와 Noise Lab이 함께 사용하는 단계 2 3D value noise
// ============================================================================
#ifndef VCLOUD_NOISE_HLSLI
#define VCLOUD_NOISE_HLSLI

#include "CloudParameters.hlsli"

#ifndef VCLOUD_NOISE_TEST_BIAS
#define VCLOUD_NOISE_TEST_BIAS 0.0
#endif

struct CloudDensitySample
{
    float rawNoise;
    float thresholdDensity;
    float finalDensity;
    float3 noiseUvw;
};

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

// 월드 위치와 명시적인 시간을 동일한 3D 밀도 표본으로 변환한다.
// 사용자 조절값은 CloudCB에서 오고, 알고리즘만 이 공용 파일에 존재한다.
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
    sample.finalDensity = saturate(
        sample.thresholdDensity * max(densityMultiplier, 0.0));
    return sample;
}

#endif
