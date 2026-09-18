// [학습 지도] CPU Stage12ShadowParameters → b8(160B) → Deep Shadow CS/조회. 고정 규격과 매 frame 파생 basis를 구분.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  Stage12ShadowParameters.hlsli - 고정 Balanced512 Deep Cache b8 계약
// ============================================================================
#ifndef VCLOUD_STAGE12_SHADOW_PARAMETERS_HLSLI
#define VCLOUD_STAGE12_SHADOW_PARAMETERS_HLSLI

cbuffer ShadowCB : register(b8)
{
    // [고정 품질] Near 가로/세로 512 texel. sanitize가 복원하며 단독 수정 금지.
    uint stage12NearResolution;
    // [고정 품질] Far 가로/세로 512 texel. Near보다 넓은 월드를 담아 texel당 m가 크다.
    uint stage12FarResolution;
    // [패딩] 사용하지 않던 enabled의 정렬 공간. 0 유지; 표면 대비는 strength/floor가 담당.
    uint stage12PaddingEnabled;
    // [파생 값] 자원+태양 basis+최소 고도로 결정하는 0/1, 초기 0. 0이면 cone/중립 표면 fallback.
    uint stage12CacheReady;

    // [파생 값] 태양 좌표 가로 xyz 단위벡터. 초기 (1,0,0), 매 프레임 basis로 교체. 직접 조절 금지.
    float3 stage12LightRight;
    // [고정 품질] Near 수평 기준 폭 24000m. up 투영 폭은 태양 고도와 층 두께에서 계산한다.
    float stage12NearWidthMeters;

    // [파생 값] 태양 좌표 세로 xyz 단위벡터. 초기 (0,0,1), 월드 +Y와 다르다. UV 투영에 사용.
    float3 stage12LightUp;
    // [고정 품질] Far 수평 기준 폭 128000m. right 폭과 up 폭은 저고도에서 다르다.
    float stage12FarWidthMeters;

    // [파생 값] 표본→태양 xyz 단위벡터. 초기 (0,1,0); y=sin(고도), 세로 간격을 광선 길이로 바꾼다.
    float3 stage12LightForward;
    // [파생 값] b5 바닥 월드 Y(m), 구조체 초기 1500. cache slice 0의 높이.
    float stage12CloudBottomMeters;

    // [파생 값] xyz 월드 m, 카메라 중심을 Near texel 크기에 snap. 이동 때 그림자가 미세하게 흔들리는 현상을 줄인다.
    float3 stage12NearCenter;
    // [파생 값] b5 바닥+두께 월드 Y(m), 구조체 초기 7500. 마지막 slice의 높이.
    float stage12CloudTopMeters;

    // [파생 값] xyz 월드 m, 카메라 중심을 Far texel 크기에 snap. Near와 격자 간격이 다르다.
    float3 stage12FarCenter;
    // [고정 품질] 무차원 tau 상한 9.21034037=-ln(0.0001). 이미 거의 불투명한 누적을 제한한다.
    float stage12MaximumOpticalDepth;

    // [고정 품질] Near 높이 표본 80장. 높이 간 tau는 조회 시 두 slice를 보간한다.
    uint stage12NearSliceCount;
    // [고정 품질] 06 사용자 승인: Far 높이 표본 79장. Near와 함께 R32 배열 159MiB.
    uint stage12FarSliceCount;
    // [파생 값] 현재 CS 출력 0=Near/1=Far, 초기 0, clamp <=1. Renderer가 각 Dispatch 직전에 쓴다.
    uint stage12DispatchCascade;
    // [패딩] 16바이트 packing 예약 칸, 0 유지. 화면 효과 없음; 삭제/재배치 금지.
    uint stage12Padding0;

    // [고정 품질] 중심 0/가장자리 1인 Near 거리 0.80까지 Near 100%.
    float stage12NearCoreEnd;
    // [고정 품질] Near 거리 0.95에서 Far로 전환 완료. 0.80~0.95에서 부드럽게 혼합.
    float stage12NearBlendEnd;
    // [고정 품질] Far 거리 0.90부터 그림자를 중립 T=1로 희미하게 한다.
    float stage12FarFadeStart;
    // [고정 품질] Far 거리 1.00에서 T=1. 영역 밖을 검은 그림자로 늘리지 않는다.
    float stage12FarFadeEnd;

    // [직접 조절] F3/scene 그림자 강도 [0,1], 구조체 초기 1/내장 0.40~0.65 권장. 증가하면 지면/건물 그림자 대비 증가.
    float stage12SurfaceShadowStrength;
    // [직접 조절] F3/scene 표면 그림자 계수 하한 [0,1], 기본/권장 0.35. 증가하면 깊은 표면 그림자가 밝아진다.
    float stage12SurfaceAmbientFloor;
    // [고정 품질] sin(3도)=0.052335956. 낮은 태양에서 긴 적분과 불안정한 cache 투영을 피한다.
    float stage12MinimumSunY;
    // [직접 조절: 진단] cache 표시 [0.1,20], 기본 4. 1-exp(-tau*exposure)를 밝게 만들며 실제 그림자는 불변.
    float stage12CacheDebugExposure;

    // [직접 조절: 진단] Near 배열 index [0,79], 기본 0. 증가하면 높은 곳의 남은 tau를 표시.
    uint stage12DebugNearSlice;
    // [직접 조절: 진단] Far 배열 index [0,78], 기본 0. 증가하면 높은 곳의 남은 tau를 표시.
    uint stage12DebugFarSlice;
    // [패딩] 16바이트 정렬, 항상 0.
    uint stage12Padding1;
    // [패딩] 16바이트 packing 예약 칸, 0 유지. 화면 효과 없음; 삭제/재배치 금지.
    uint stage12Padding2;
};

// CPU center snap / CS texel 배치 / PS UV 조회가 공유하는 투영 규약.
// 수평 W, 수직 H인 영역을 태양 평면에 투영하면 up 폭은 W*sin(a)+H*cos(a)다.
// UV는 계속 태양 광선을 따라 불변이며 높이 slice는 기존 월드 Y를 사용한다.
float Stage12CacheUpWidth(float widthMeters)
{
#if !defined(VCLOUD_TEST_LEGACY_SHADOW)
    float sunY = saturate(stage12LightForward.y);
    float height = max(stage12CloudTopMeters - stage12CloudBottomMeters, 1.0);
    return max(widthMeters * sunY + height * sqrt(saturate(1.0 - sunY * sunY)), 1.0);
#else
    return widthMeters;
#endif
}

// 05-C: cache 사용 시작(3도)과 완전 전환(5도)을 분리한다. CB/High cone 규격은 불변.
float Stage12SunTransitionWeight()
{
#if defined(VCLOUD_TEST_LEGACY_SUN_TRANSITION) || defined(VCLOUD_TEST_LEGACY_SHADOW)
    return 1.0;
#else
    return smoothstep(stage12MinimumSunY, 0.087155743, stage12LightForward.y);
#endif
}


#endif
