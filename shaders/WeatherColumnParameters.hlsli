#ifndef WEATHER_COLUMN_PARAMETERS_HLSLI
#define WEATHER_COLUMN_PARAMETERS_HLSLI
// b10: 공통 두께와 타입. 예약 칸은 CPU에서0, G는 참조하지 않는다.
cbuffer WeatherColumnCB : register(b10)
{
 float minimumThicknessMeters;
 float maximumThicknessMeters;
 float weatherThicknessPadding0;
 float weatherThicknessPadding1;
 float maximumBaseLiftMeters;
 float fixedType;
 float weatherSelectionPadding;
 float weatherColumnPadding0;
};
float ResolveEffectiveCloudType() { return saturate(fixedType); }
#endif
