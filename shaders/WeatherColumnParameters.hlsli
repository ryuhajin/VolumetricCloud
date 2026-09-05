#ifndef WEATHER_COLUMN_PARAMETERS_HLSLI
#define WEATHER_COLUMN_PARAMETERS_HLSLI

// Weather Map의 A/G를 물리 컬럼으로 해석하는 값이다. b7 형상 프로필과 분리한다.
// CPU는 domain thickness >= 선택 모드 max thickness + max lift + 200m를 먼저 검사하며,
// 부족한 값은 상단 clipping을 만들기 때문에 이 cbuffer를 갱신하기 전에 거부한다.
cbuffer WeatherColumnCB : register(b10)
{
    // A=0/1의 층운 두께(m). 증가하면 낮고 넓은 층운의 수직 부피가 커진다.
    float stratusMinimumThicknessMeters;
    float stratusMaximumThicknessMeters;
    // A=0/1의 적운 두께(m). 증가하면 적운 상부가 더 높게 발달한다.
    float cumulusMinimumThicknessMeters;
    float cumulusMaximumThicknessMeters;

    // A가 허용하는 구름 바닥 상승의 최대값(m). 커지면 구름 밑면의 지역 기복이 커진다.
    float maximumBaseLiftMeters;
    // Fixed 모드는 0/0.5/1, Regional은 아래 influence=1로 저장된 G를 사용한다.
    float fixedType;
    float regionalInfluence;
    float weatherColumnPadding0;
};

float ResolveEffectiveCloudType(float storedRegionalType)
{
    return saturate(lerp(fixedType, saturate(storedRegionalType),
                         saturate(regionalInfluence)));
}

#endif
