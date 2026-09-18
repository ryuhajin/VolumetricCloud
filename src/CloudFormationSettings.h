// ============================================================================
//  CloudFormationSettings.h - 최종 High 공통 구름 형성 CPU 계약
// ----------------------------------------------------------------------------
//  F1의 네 슬롯이 같은 Weather/Shape/Domain 경로를 사용하도록, 화면의
//  구름 형성에 관여하는 값만 한 snapshot으로 묶는다. 품질/조명/시간/offset과
//  Texture3D 생성 규격은 의도적으로 소유하지 않는다.
// ============================================================================
#pragma once

#include "CloudDomainParameters.h"
#include "CloudParameters.h"
#include "CloudTypeSelection.h"
#include "CloudShapeDomainContract.h"
#include "CloudShapeParameters.h"
#include "Stage13NoiseVolumeMath.h"
#include "WeatherMap.h"

#include <cstdint>
#include <string>

struct CloudFormationSettings
{
    // [직접 조절] Formation/F1, 무차원 [강제 범위] [0,1]. [권장 범위] 내장 0.38~0.90; 증가하면 noise 문턱이 낮아져 구름이 넓어진다.
    float coverage = 0.38f;
    // [직접 조절] Formation/F1 밀도 배율. [강제 범위] [0,5]. [권장 범위] 내장 1.10~1.25; 증가하면 광학적으로 두꺼워진다.
    float densityMultiplier = 1.10f;
    // [직접 조절] Formation.extinctionPerMeter/F1, 단위 1/m. [강제 범위] Formation [0.000001,0.01]. [권장 범위] 내장 0.00035~0.00046; 증가하면 T가 작아진다.
    float extinctionPerMeter = 0.00036f;
    // [직접 조절] Formation.detailErosion/F1. [강제 범위] [0,1]. [권장 범위] 내장 0.10~0.24; 증가하면 얇은 경계가 더 깎인다.
    float detailErosion = 0.24f;

    // [직접 조절] 0 Uniform/1 PeriodicPerlin(권장)/2 ChannelDebug. validate는 그 밖 값을 거부; sanitize는 1로 복원.
    Stage5WeatherPreset weatherPreset = Stage5WeatherPreset::PeriodicPerlin;
    // [직접 조절] 생성기/물리 column/월드 크기 원본. Custom schema 4 저장 대상; 타입 선택과 분리.
    WeatherMapDefinition weather = {};
    // [직접 조절] 고정 타입 선택. Weather G는 예약값이며 조회하지 않는다.
    CloudTypeSelection typeSelection = {};

    // [직접 조절] 공통 높이 profile/타입별 footprint, 각 필드 범위는 CloudShapeParameters 참조. 두께는 weather.column 소유.
    CloudShapeParameters shape = {};

    // [직접 조절] Formation/F1 절대 월드 Y(m). Formation [-100000,100000], domain sanitize는 finite만 보장. 이 구조체/Urban 초기 1800; 올리면 층 전체 상승.
    float domainBottomMeters = 1800.0f;
    // [직접 조절] Formation/F1 도메인 두께 m. Formation [1,100000], domain sanitize >=0.0001. 이 구조체/Urban 초기 3700. 최대 local 두께+lift+200m를 담아야 한다.
    float domainThicknessMeters = 3700.0f;
    // [직접 조절] Formation 최대 시선 거리 m. Formation [1,200000], domain sanitize >=0.0001. 기본/권장 50000; 늘리면 원경과 비용이 증가한다.
    float maximumViewTraceDistanceMeters = 50000.0f;
    // [직접 조절] Formation 거리 fade 시작 m. [강제 범위] [0,maxViewTraceDistance], 기본/권장 40000. 낮추면 원경이 더 일찍 희미해진다.
    float viewTraceFadeStartDistanceMeters = 40000.0f;
    // [직접 조절] Formation cone 추적 거리 m. Formation [1,200000], domain sanitize >=0.0001. 기본/권장 20000; Deep Cache 크기와는 별개다.
    float maximumLightTraceDistanceMeters = 20000.0f;

    // Texture3D의 내용/해상도/seed/frequency가 아니라 월드 샘플링 크기만
    // formation이 소유한다. 따라서 이 값 변경은 texture 재생성을 요구하지 않는다.
    // [직접 조절] F2/Formation Base XZ m/반복. Formation [1,200000], 기본/권장 12000. 늘리면 덩어리가 넓어지며 재생성 불필요.
    float baseNoiseWorldSizeMeters = stage13noise::kBaseHorizontalWorldSizeMeters;
    // [직접 조절] F2/Formation Base Y m/반복. Formation [1,200000], 기본/권장 12000. 늘리면 세로 무늬가 늘어진다.
    float baseNoiseVerticalWorldSizeMeters =
        stage13noise::kBaseVerticalWorldSizeMeters;
    // [직접 조절] F2/Formation Detail XYZ m/반복. Formation [1,100000], 기본/권장 2000. 늘리면 표면 파임이 커진다.
    float detailNoiseWorldSizeMeters = 2000.0f;

};

struct PreparedCloudFormation
{
    // [파생 값] Prepare가 검증/보정한 원자 적용 후보. 검증 실패 시 현재 runtime을 교체하지 않는다.
    CloudFormationSettings settings = {};
    // [파생 값] settings로 만든 바깥 추적 영역과 조명 대표 고도.
    CloudDomainParameters domain = {};
    // [파생 값] 선택 타입의 최대 두께+lift+200m가 domain에 들어가는지 검사한 결과.
    cloudshapedomain::FitResult fit = {};
};

// 파일 load는 범위 밖 값을 clamp해 받아들이지 않는다. 먼저 strict validation을
// 통과한 뒤 sanitize/방향 정규화와 파생 조명 고도 계산을 수행한다.
bool IsValidCloudFormationSettings(const CloudFormationSettings& value);
CloudFormationSettings SanitizeCloudFormationSettings(
    const CloudFormationSettings& value);
bool PrepareCloudFormationSettings(
    const CloudFormationSettings& value,
    PreparedCloudFormation& outPrepared,
    std::string& status,
    float requiredTopHeadroomMeters = 200.0f);

CloudFormationSettings CaptureCloudFormationSettings(
    const CloudParameters& cloud,
    const CloudShapeParameters& shape,
    const CloudDomainParameters& domain,
    Stage5WeatherPreset weatherPreset,
    const WeatherMapDefinition& weather,
    const CloudTypeSelection& typeSelection,
    const NoiseVolumeParameters& noise);

// ImGui가 runtime 구조체에 먼저 쓴 raw 후보를 감지할 때 사용한다. sanitize로
// 같은 canonical 값이 되는 min/max 역전이나 wind 길이/Y 변경도 놓치지 않는다.
CloudFormationSettings CaptureCloudFormationSettingsUnchecked(
    const CloudParameters& cloud,
    const CloudShapeParameters& shape,
    const CloudDomainParameters& domain,
    Stage5WeatherPreset weatherPreset,
    const WeatherMapDefinition& weather,
    const CloudTypeSelection& typeSelection,
    const NoiseVolumeParameters& noise);

// 이미 Prepare를 통과한 값을 현재 CPU 상태의 formation 소유 필드에만 쓴다.
// ray step, offset, texture seed/resolution과 조명 값은 보존한다.
void WriteCloudFormationToRuntime(
    const PreparedCloudFormation& formation,
    CloudParameters& cloud,
    CloudShapeParameters& shape,
    CloudDomainParameters& domain,
    Stage5WeatherPreset& weatherPreset,
    WeatherMapDefinition& weather,
    CloudTypeSelection& typeSelection,
    NoiseVolumeParameters& noise);

bool CloudFormationSettingsEqual(
    const CloudFormationSettings& a,
    const CloudFormationSettings& b,
    float epsilon = 1.0e-6f);
std::uint64_t HashCloudFormationSettings(
    const CloudFormationSettings& value);
