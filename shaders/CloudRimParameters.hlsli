// ============================================================================
//  CloudRimParameters.hlsli - Stage 15B full-resolution NTE rim contract
// ============================================================================
#ifndef VCLOUD_RIM_PARAMETERS_HLSLI
#define VCLOUD_RIM_PARAMETERS_HLSLI

// CPU CloudRimParameters와 같은 세 레지스터(48바이트)다.
// D3D11/SM5 pixel shader의 유효 CB 슬롯은 b0~b13뿐이다. 원 계획의 b14는
// FXC X4567로 컴파일되지 않으므로, Stage10 CB를 읽지 않는 이 독립 Composite
// pass에서 비어 있는 b10을 pass-local 슬롯으로 재사용한다.
cbuffer CloudRimCB : register(b10)
{
    uint cloudRimEnabled;
    float cloudRimWidthPixels;
    float cloudRimIntensity;
    float cloudRimOpacityThreshold;

    float cloudRimOpacitySoftness;
    float cloudRimSunAlignment;
    float cloudRimSunPower;
    float cloudRimDepthRejectionThreshold;

    float3 cloudRimTint;
    float cloudRimRadianceClamp;
};

#endif
