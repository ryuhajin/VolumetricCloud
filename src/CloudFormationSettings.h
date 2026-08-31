// ============================================================================
//  CloudFormationSettings.h - Stage 15B 공통 구름 형성 CPU 계약
// ----------------------------------------------------------------------------
//  F1 타입과 F4 콘셉트가 같은 Weather/Shape/Domain 경로를 사용하도록, 화면의
//  구름 형성에 관여하는 값만 한 snapshot으로 묶는다. 품질/조명/시간/offset과
//  Texture3D 생성 규격은 의도적으로 소유하지 않는다.
// ============================================================================
#pragma once

#include "CloudDomainParameters.h"
#include "CloudParameters.h"
#include "CloudShapeDomainContract.h"
#include "CloudShapeParameters.h"
#include "Stage13NoiseVolumeMath.h"
#include "WeatherMap.h"

#include <cstdint>
#include <string>

struct CloudFormationSettings
{
    float coverage = 0.38f;
    float densityMultiplier = 1.10f;
    float extinctionPerMeter = 0.00036f;
    float detailErosion = 0.24f;

    Stage5WeatherPreset weatherPreset = Stage5WeatherPreset::PeriodicPerlin;
    WeatherMapGeneratorSettings weather = {};
    float weatherWorldSizeMeters = 64000.0f;

    CloudShapeParameters shape = {};

    float domainBottomMeters = 1800.0f;
    float domainThicknessMeters = 3700.0f;
    float maximumViewTraceDistanceMeters = 50000.0f;
    float viewTraceFadeStartDistanceMeters = 40000.0f;
    float maximumLightTraceDistanceMeters = 20000.0f;

    // Texture3D의 내용/해상도/seed/frequency가 아니라 월드 샘플링 크기만
    // formation이 소유한다. 따라서 이 값 변경은 texture 재생성을 요구하지 않는다.
    float baseNoiseWorldSizeMeters = stage13noise::kBaseHorizontalWorldSizeMeters;
    float baseNoiseVerticalWorldSizeMeters =
        stage13noise::kBaseVerticalWorldSizeMeters;
    float detailNoiseWorldSizeMeters = 2000.0f;

    DirectX::XMFLOAT3 windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    float windSpeedMetersPerSecond = 12.0f;
};

struct PreparedCloudFormation
{
    CloudFormationSettings settings = {};
    CloudDomainParameters domain = {};
    WeatherMapData weatherMap = {};
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
    const WeatherMapGeneratorSettings& weather,
    const NoiseVolumeParameters& noise);

// ImGui가 runtime 구조체에 먼저 쓴 raw 후보를 감지할 때 사용한다. sanitize로
// 같은 canonical 값이 되는 min/max 역전이나 wind 길이/Y 변경도 놓치지 않는다.
CloudFormationSettings CaptureCloudFormationSettingsUnchecked(
    const CloudParameters& cloud,
    const CloudShapeParameters& shape,
    const CloudDomainParameters& domain,
    Stage5WeatherPreset weatherPreset,
    const WeatherMapGeneratorSettings& weather,
    const NoiseVolumeParameters& noise);

// 이미 Prepare를 통과한 값을 현재 CPU 상태의 formation 소유 필드에만 쓴다.
// ray step, offset, texture seed/resolution, 조명과 Temporal 값은 보존한다.
void WriteCloudFormationToRuntime(
    const PreparedCloudFormation& formation,
    CloudParameters& cloud,
    CloudShapeParameters& shape,
    CloudDomainParameters& domain,
    Stage5WeatherPreset& weatherPreset,
    WeatherMapGeneratorSettings& weather,
    NoiseVolumeParameters& noise);

bool CloudFormationSettingsEqual(
    const CloudFormationSettings& a,
    const CloudFormationSettings& b,
    float epsilon = 1.0e-6f);
std::uint64_t HashCloudFormationSettings(
    const CloudFormationSettings& value);
