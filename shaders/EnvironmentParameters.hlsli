// ============================================================================
//  EnvironmentParameters.hlsli - CPU와 공유하는 64바이트 EnvironmentCB(b4)
// ----------------------------------------------------------------------------
//  외부 Cube Map이나 간접광 텍스처 없이 linear RGB 상수색과 현재 표본의 높이·
//  밀도로 간접광을 근사한다. 단계 14에서 skyColor 평가 부분만 실제 대기/환경
//  입력으로 교체할 수 있도록 LightCB와 분리한다.
// ============================================================================
#ifndef VCLOUD_ENVIRONMENT_PARAMETERS_HLSLI
#define VCLOUD_ENVIRONMENT_PARAMETERS_HLSLI

cbuffer EnvironmentCB : register(b4)
{
    float3 skyColor;                        // CPU skyColor. linear RGB 하늘 환경광.
    float skyStrength;                      // CPU skyStrength. 하늘광 세기.
    float3 groundColor;                     // CPU groundColor. linear RGB 지면 반사색.
    float groundStrength;                   // CPU groundStrength. 지면 반사광 세기.
    float ambientOcclusionStrength;         // CPU 동일 필드. 밀도 기반 환경광 감쇠 강도.
    float ambientHeightInfluence;           // CPU 동일 필드. 0=높이 무관, 1=하늘광이 높이에 비례.
    float multipleScatteringEnabled;        // CPU 동일 필드. 0이면 다중 산란 결과가 정확히 0.
    uint multipleScatteringOctaves;         // CPU 동일 필드. 저비용 반복 수 [0,4].
    float multipleScatteringAttenuation;    // CPU 동일 필드. octave별 에너지 감소.
    float multipleScatteringExtinctionFactor;// CPU 동일 필드. 반복 광학 깊이 비율.
    float multipleScatteringPhaseFactor;    // CPU 동일 필드. 반복 Phase 방향성 비율.
    float environmentPadding;               // 16바이트 정렬 예약. 사용하지 않는다.
};

#endif
