// ============================================================================
//  CloudParameters.h  —  구름 밀도와 디버그 UI가 공유하는 CPU 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>

enum class CloudRenderMode : int
{
    LitCloud = 0,
    FinalDensity,
    BaseShape,
    DetailNoise,
    HeightMask,
    Transmittance,
    CacheDifference,
    SeamDifference,
};

enum class NoiseSliceAxis : int
{
    XY = 0,
    XZ,
    YZ,
};

// HLSL CloudCB(b1)와 정확히 같은 96바이트 레이아웃을 사용한다.
struct alignas(16) CloudParameters
{
    float noiseWorldScale  = 1.0f; // 한 볼륨 안에서 반복되는 타일 수(정수로 사용)
    int   basePeriod       = 4;
    int   detailPeriod     = 16;
    float densityMultiplier= 1.6f;

    float noiseCutoffThreshold = 0.48f;
    float erosionStrength  = 0.35f;
    float bottomFade       = 0.12f;
    float topFade          = 0.25f;

    DirectX::XMFLOAT2 windDirection = { 1.0f, 0.2f };
    float windSpeed        = 0.03f;
    float seed             = 17.0f;

    int baseOctaves        = 4;
    int detailOctaves      = 3;
    int renderMode         = static_cast<int>(CloudRenderMode::LitCloud);
    int useTextureCache    = 1;

    int showBounds         = 0;
    int lightSteps         = 6;
    float sunAzimuth       = 45.0f;
    float sunElevation     = 35.0f;

    float sunIntensity     = 1.8f;
    float ambientIntensity = 0.28f;
    float phaseG           = 0.55f;
    float lightAbsorption  = 1.25f;
};

static_assert(sizeof(CloudParameters) == 96, "CloudParameters must match CloudCB");

struct NoisePreviewSettings
{
    int   axis        = static_cast<int>(NoiseSliceAxis::XY);
    float slice       = 0.5f;
    bool  freeze      = true;
    float previewTime = 0.0f;
};

inline CloudParameters DefaultCloudParameters()
{
    return CloudParameters{};
}

inline CloudParameters CumulusCloudParameters()
{
    CloudParameters p;
    p.basePeriod = 4;
    p.detailPeriod = 16;
    p.noiseCutoffThreshold = 0.44f;
    p.densityMultiplier = 1.9f;
    p.erosionStrength = 0.42f;
    p.bottomFade = 0.10f;
    p.topFade = 0.30f;
    return p;
}

inline CloudParameters StratusCloudParameters()
{
    CloudParameters p;
    p.basePeriod = 6;
    p.detailPeriod = 20;
    p.noiseCutoffThreshold = 0.56f;
    p.densityMultiplier = 1.2f;
    p.erosionStrength = 0.25f;
    p.bottomFade = 0.05f;
    p.topFade = 0.15f;
    return p;
}
