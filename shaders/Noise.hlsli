// ============================================================================
//  Noise.hlsli - 구름 렌더와 Noise Lab이 함께 사용하는 단계 4 밀도 함수
// ----------------------------------------------------------------------------
//  데이터 흐름
//  1. 월드 위치(m)를 바람이 이동시킨 noise 좌표로 바꾼다.
//  2. value noise와 coverage로 단계 2의 기본 덩어리 밀도를 만든다.
//  3. AABB 바닥/천장 사이의 높이 비율과 부드러운 높이 마스크를 계산한다.
//  4. threshold × 높이 × 배율을 큰 구름 형태인 Base Density로 확정한다.
//  5. Base가 존재할 때만 별도 고주파 Detail Noise를 샘플링해 밀도를 깎는다.
//
//  이번 단계는 단일 Value Noise만 사용한다. 이후 fBm/Worley는
//  SampleDetailErosionNoise 내부만 교체하고 레이마칭 인터페이스는 유지한다.
//  단계 5 Weather, 단계 6 Light는 아직 적용하지 않는다.
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
    float baseDensity;       // 단계 3까지의 큰 구름 형태(0~1).
    float detailNoise;       // 실제 샘플한 고주파 침식 noise(0~1).
    float erosion;           // detailNoise × detailErosionStrength.
    float finalDensity;      // saturate(baseDensity - erosion), 적분 입력.
    float detailSampled;     // Detail 함수를 호출했으면 1, 생략했으면 0.
    float3 noiseUvw;         // Base value noise의 연속 좌표(cycle).
    float3 detailNoiseUvw;   // Detail value noise의 연속 좌표(cycle), 생략 시 0.
};

// 서로 다른 노이즈 알고리즘도 동일한 값+좌표 인터페이스로 연결하기 위한 표본이다.
// 단계 4는 value만 사용하지만 이후 fBm/Worley도 이 구조를 반환하게 한다.
struct NoiseFieldSample
{
    float value;
    float3 uvw;
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
float SampleValueNoise3D(float3 noiseUvw)
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

// 0 벡터 바람은 정규화하지 않아 NaN을 막는다. Base와 Detail이 같은 방향을
// 공유하되 각자의 속도로 이동하므로 표면이 큰 덩어리 위에서 천천히 미끄러질 수 있다.
float3 SafeWindDirection()
{
    float windLength = length(windDirection);
    return windLength > 1e-6 ? windDirection / windLength : 0.0.xxx;
}

// 단계 2부터 사용한 저주파 Base Shape Noise를 별도 함수로 감싼다.
NoiseFieldSample SampleBaseShapeNoise(float3 worldPosition, float timeSeconds)
{
    NoiseFieldSample result = (NoiseFieldSample)0;
    float3 stationaryWorld = worldPosition - SafeWindDirection() *
        max(windSpeed, 0.0) * max(timeSeconds, 0.0);
    result.uvw = stationaryWorld * max(baseNoiseScale, 1e-4) + noiseOffset.xxx;
    result.value = SampleValueNoise3D(result.uvw);
    return result;
}

// 표면 침식 노이즈의 유일한 교체 지점이다. 지금은 고주파 단일 Value Noise지만
// 이후 3-octave fBm 또는 Worley를 도입해도 호출자와 CloudDensitySample은 바꾸지 않는다.
NoiseFieldSample SampleDetailErosionNoise(float3 worldPosition, float timeSeconds)
{
    NoiseFieldSample result = (NoiseFieldSample)0;
    float3 stationaryWorld = worldPosition - SafeWindDirection() *
        max(detailWindSpeed, 0.0) * max(timeSeconds, 0.0);
    result.uvw = stationaryWorld * max(detailNoiseScale, 1e-4) +
        detailNoiseOffset.xxx;
    result.value = SampleValueNoise3D(result.uvw);
    return result;
}

// 단계 2 noise와 단계 3 높이만 계산해 큰 구름 형태를 만든다.
// Detail 설정을 바꿔도 이 함수의 baseDensity는 절대로 바뀌지 않아야 한다.
CloudDensitySample EvaluateBaseCloudDensity(float3 worldPosition, float timeSeconds)
{
    CloudDensitySample sample = (CloudDensitySample)0;
    NoiseFieldSample baseNoise = SampleBaseShapeNoise(worldPosition, timeSeconds);
    sample.noiseUvw = baseNoise.uvw;
    sample.rawNoise = baseNoise.value;

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
    sample.baseDensity = saturate(sample.thresholdDensity *
        sample.heightProfile * max(densityMultiplier, 0.0));
    sample.finalDensity = sample.baseDensity;
    return sample;
}

// Base Shape 뒤에 선택적으로 Detail Erosion을 적용한다.
// sampleDetail=false, 빈 Base, strength=0 경로는 Detail 함수 자체를 호출하지 않는다.
// 이 조기 반환은 단계 4의 기능 요구이며 단계 9의 레이 스텝 최적화와는 별개다.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds,
                                      bool sampleDetail)
{
    CloudDensitySample sample = EvaluateBaseCloudDensity(worldPosition, timeSeconds);
    bool shouldSampleDetail = sampleDetail && sample.baseDensity > 0.0 &&
                              detailErosionStrength > 0.0;
    if (shouldSampleDetail)
    {
        NoiseFieldSample detail = SampleDetailErosionNoise(worldPosition, timeSeconds);
        sample.detailNoiseUvw = detail.uvw;
        sample.detailNoise = detail.value;
        sample.erosion = sample.detailNoise * max(detailErosionStrength, 0.0);
        sample.finalDensity = saturate(sample.baseDensity - sample.erosion);
        sample.detailSampled = 1.0;
    }
    return sample;
}

// 기존 호출부와 이후 View Ray는 기본적으로 Detail을 사용하는 편의 overload다.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds)
{
    return SampleCloudDensity(worldPosition, timeSeconds, true);
}

#endif
