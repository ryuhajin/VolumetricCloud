// [학습 지도] CPU NoiseVolumeParameters → b6(96B) → 생성 CS와 Noise 조회. 해상도 texel, frequency cycle/타일, 크기 m/타일.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  NoiseVolumeParameters.hlsli - 단계 13-4 CPU/HLSL 공유 Texture3D 계약
// ============================================================================
#ifndef VCLOUD_NOISE_VOLUME_PARAMETERS_HLSLI
#define VCLOUD_NOISE_VOLUME_PARAMETERS_HLSLI

cbuffer NoiseVolumeCB : register(b6)
{
    // [고정 품질] Base 한 축 128 texel(128³ RGBA8). 텍스처 할당·검증 계약과 함께 고정.
    uint baseVolumeResolution;
    // [고정 품질] Detail 한 축 64 texel(64³ RGBA8). world size와 달리 내용 생성 규격.
    uint detailVolumeResolution;
    // [고정 품질] 3D Noise seed uint [0,4294967295], 기본/권장 1337. 같은 seed는 같은 무늬; 변경은 재생성이 필요하며 크기/밀도와 무관.
    uint noiseVolumeSeed;
    // [직접 조절] F2 "Near micro tile" m/반복(옛 패딩 칸, offset 12). Formation [200,2000], 기본 570, 타입별 저장.
    // 근경 미세 Detail(Noise.hlsli)만 읽고 생성 CS는 읽지 않는다.
    float nearMicroTileMeters;

    // [직접 조절] F2/Formation Base XZ m/반복. Formation [1,200000], 기본/권장 12000. 늘리면 덩어리가 넓어지며 재생성 불필요.
    float baseVolumeWorldSizeMeters;
    // [직접 조절] F2/Formation Detail XYZ m/반복. Formation [1,100000], 기본/권장 2000. 늘리면 표면 파임이 커진다.
    float detailVolumeWorldSizeMeters;
    // [직접 조절] F2/Formation Base Y m/반복. Formation [1,200000], 기본/권장 12000. 늘리면 세로 무늬가 늘어진다.
    float baseVolumeVerticalWorldSizeMeters;
    // [03 임시 실험] F2 선택; 생성에만 사용. Custom 저장 대상 아님.
    float baseMidOctaveExtra; // 03 세션 실험, CPU 정규화 0/.25/.5, offset28.

    // [고정 품질] x/y/z/w={4,9,17,23} cycle/타일. R fBm의 4옥타브, G/B/A Worley는 xyz 사용. 생성 경로 최소 1, 권장 현행 유지.
    uint4 baseVolumeFrequencies;
    // [고정 품질] x/y/z/w=RGBA Worley {2,3,4,5} cycle/타일. 생성 최소 1, 표본 한계는 resolution/2; 현행 유지.
    uint4 detailVolumeFrequencies;
    // [직접 조절: 코드] x/y/z=Base G/B/A Worley 가중치 (0.625,0.25,0.125), w=0 미사용. 별도 CPU clamp/정규화 없음; 권장 비음수 합 1 유지.
    float4 baseVolumeWeights;
    // [직접 조절: 코드] x/y/z/w=Detail R/G/B/A 가중치 (0.50,0.30,0.15,0.05). CPU clamp 없음, 결과 saturate. 권장 비음수 합 1; 큰 주파수 비중↑면 파임이 잘게 된다.
    float4 detailVolumeWeights;
};

#endif
