// ============================================================================
//  CloudLodParameters.hlsli - CPU CloudLodParameters와 공유하는 16바이트 b8
// ============================================================================
#ifndef VCLOUD_CLOUD_LOD_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_LOD_PARAMETERS_HLSLI

cbuffer CloudLodCB : register(b8)
{
    uint detailLodEnabled;       // 0이면 거리와 무관하게 원본 Detail을 유지한다.
    float detailLodStartMeters;  // 원본 Detail을 평균으로 바꾸기 시작하는 View 거리(m).
    float detailLodEndMeters;    // 이 거리부터 Texture3D를 읽지 않고 평균만 쓴다(m).
    float detailNeutralValue;    // 실제 Detail RGBA와 weights로 계산한 평균값.
};

float EvaluateDetailLodFactor(float viewDistanceMeters)
{
    float lodFactor = 1.0;
    if (detailLodEnabled != 0u)
    {
        const float safeStart = max(detailLodStartMeters, 0.0);
        const float safeEnd = max(detailLodEndMeters, safeStart + 1.0);
        lodFactor = 1.0 - smoothstep(safeStart, safeEnd, max(viewDistanceMeters, 0.0));
    }
    return lodFactor;
}

#endif
