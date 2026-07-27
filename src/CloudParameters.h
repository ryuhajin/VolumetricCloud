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
    BaseChannelR,
    BaseChannelG,
    BaseChannelB,
    BaseChannelA,
    LightVisibility,
    PhaseFunction,
    AmbientLighting,
    DirectLighting,
};

enum class NoiseSliceAxis : int
{
    XY = 0,
    XZ,
    YZ,
};

// HLSL CloudCB(b1)와 정확히 같은 128바이트 레이아웃을 사용한다.
struct alignas(16) CloudParameters
{
    float noiseWorldScale  = 1.0f; // 한 볼륨 안에서 반복되는 타일 수(정수로 사용)
    int   basePeriod       = 4;
    int   detailPeriod     = 16;
    float densityMultiplier= 2.4f;

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
    int lightSteps         = 8;
    float sunAzimuth       = 45.0f;
    float sunElevation     = 35.0f;

    float sunIntensity     = 6.0f;
    float ambientIntensity = 0.90f;
    float phaseG           = 0.55f;
    float lightAbsorption  = 1.05f;

    float coverage             = 0.62f;
    float baseErosion          = 0.24f;
    float powderStrength       = 0.65f;
    float multiScatterStrength = 0.35f;

    float silverLiningStrength = 0.25f;
    float jitterStrength       = 0.80f;
    int   viewSteps            = 96;
    float skyExposure          = 1.0f;
};

static_assert(sizeof(CloudParameters) == 128, "CloudParameters must match CloudCB");

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
    p.noiseCutoffThreshold = 0.46f;
    p.densityMultiplier = 3.0f;
    p.erosionStrength = 0.38f;
    p.bottomFade = 0.08f;
    p.topFade = 0.26f;
    p.coverage = 0.66f;
    p.baseErosion = 0.18f;
    p.powderStrength = 0.72f;
    p.multiScatterStrength = 0.38f;
    p.silverLiningStrength = 0.55f;
    p.sunElevation = 28.0f;
    p.sunIntensity = 6.5f;
    p.ambientIntensity = 1.00f;
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
    p.coverage = 0.78f;
    p.baseErosion = 0.12f;
    p.powderStrength = 0.35f;
    p.multiScatterStrength = 0.45f;
    p.silverLiningStrength = 0.30f;
    return p;
}

inline CloudParameters CumulusShowcaseCloudParameters()
{
    CloudParameters p = CumulusCloudParameters();
    p.noiseWorldScale = 1.0f;
    p.basePeriod = 5;
    p.detailPeriod = 20;
    p.densityMultiplier = 3.20f;
    p.noiseCutoffThreshold = 0.45f;
    p.coverage = 0.78f;
    p.baseErosion = 0.18f;
    p.erosionStrength = 0.36f;
    p.sunAzimuth = 32.0f;
    p.sunElevation = 24.0f;
    p.sunIntensity = 7.5f;
    p.ambientIntensity = 0.88f;
    p.phaseG = 0.62f;
    p.lightAbsorption = 1.25f;
    p.lightSteps = 8;
    p.silverLiningStrength = 0.28f;
    p.viewSteps = 96;
    return p;
}
