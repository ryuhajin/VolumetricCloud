// ============================================================================
//  Stage12ShadowParameters.hlsli - CPU Stage12ShadowParameters와 같은 b12
// ============================================================================
#ifndef VCLOUD_STAGE12_SHADOW_PARAMETERS_HLSLI
#define VCLOUD_STAGE12_SHADOW_PARAMETERS_HLSLI

static const uint kStage12DirectReference = 0u;
static const uint kStage12DeepCache = 1u;

cbuffer ShadowCB : register(b12)
{
    uint stage12ShadowMode;
    uint stage12ShadowPreset;
    uint stage12NearResolution;
    uint stage12FarResolution;

    float3 stage12LightRight;
    float stage12NearWidthMeters;

    float3 stage12LightUp;
    float stage12FarWidthMeters;

    float3 stage12LightForward;
    float stage12CloudBottomMeters;

    float3 stage12NearCenter;
    float stage12CloudTopMeters;

    float3 stage12FarCenter;
    float stage12MaximumOpticalDepth;

    uint stage12NearSliceCount;
    uint stage12FarSliceCount;
    uint stage12SurfaceShadowEnabled;
    uint stage12CacheReady;

    float stage12NearCoreEnd;
    float stage12NearBlendEnd;
    float stage12FarFadeStart;
    float stage12FarFadeEnd;

    float stage12SurfaceShadowStrength;
    float stage12SurfaceAmbientFloor;
    float stage12MinimumSunY;
    float stage12CacheDebugExposure;

    uint stage12DispatchCascade;
    uint stage12DebugNearSlice;
    uint stage12DebugFarSlice;
    uint stage12Padding0;
};

#endif
