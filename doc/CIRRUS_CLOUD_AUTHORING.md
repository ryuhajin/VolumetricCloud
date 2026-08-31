# Cirrus(권운) 구름 제작 가이드

이 문서는 볼류메트릭 클라우드를 처음 만드는 사용자가 F1 `Cirrus`와 F4
`Desert Cirrus`를 안전하게 조절하기 위한 안내서다. 현재 구현에서 권운은 별도의
노이즈 텍스처를 새로 만드는 구름이 아니다. 둥근 적운·층운과 같은 3D 노이즈를
**다른 좌표계와 비율로 읽는 구름 형상**이다.

## 1. 권운이 둥근 구름과 다른 이유

적운은 위로 솟은 덩어리, 층운은 넓게 이어진 낮은 층이 핵심이다. 따라서 일반
Weather Physical 경로는 Weather G로 층운↔적운 두께와 세로 프로파일을 섞고,
약한 열의 바닥을 올리며 높이에 따라 footprint를 바꾼다.

권운은 높은 고도에서 바람을 따라 길게 늘어난 얇은 섬유가 핵심이다. 같은 방식으로
둥근 덩어리를 만든 뒤 가로로 늘리면 솜뭉치가 길어진 것처럼 보이거나 규칙적인
막대(barcode)가 된다. 현재 구현은 권운을 `CloudTypeMode`의 새 Weather 종류로 만들지
않고 `CloudShapeMode::CirrusPhysicalLayer`라는 형상 family로 구분한다.

- Weather G는 권운/적운을 선택하지 않는다. 권운에서는 shape family가 이미 결정돼
  있으므로 F1의 Weather G 제작 UI를 숨긴다.
- Weather R은 구름이 존재할 큰 영역, B는 밀도 보정, A는 각 열의 로컬 두께를 만든다.
- 권운은 전역 바닥에서 시작하지 않는다. 도메인 안의 `Layer Center`를 중심으로
  Weather A가 정한 얇은 로컬 층을 배치한다.
- `Local Base Lift`와 일반 footprint influence는 권운에 적용하지 않는다.

## 2. 같은 노이즈를 다르게 읽는다

모든 Physical 구름은 다음 GPU 텍스처를 공유한다.

| 텍스처 | 크기 | 내용 | 권운에서 하는 일 |
|---|---:|---|---|
| Base Texture3D | 128³ | Perlin-Worley 큰 질량 | 긴 섬유의 큰 흐름을 만든다 |
| Detail Texture3D | 32³ | Worley 작은 침식 | 가장자리와 내부를 잘게 깎는다 |

F1에서 Flow, Along/Across/Vertical, 두께 또는 profile을 바꿔도 이 텍스처의 해상도,
seed, 주파수와 내용 hash는 바뀌지 않는다. 변경되는 것은 아래 샘플 좌표뿐이다.

```text
along  = dot(stationaryWorld.xz, flowDirection)
across = dot(stationaryWorld.xz, perpendicular(flowDirection))

uvw = (along / alongScale,
       worldY / verticalScale,
       across / acrossScale)
```

`Flow Angle`은 XZ 평면의 정규화된 along 방향으로 바뀐다. across는 그 방향에
수직으로 자동 생성된다. Base와 Detail은 각각 독립적인 Along/Across/Vertical 크기로
같은 좌표 변환을 사용한다. F1과 F4 때문에 셰이더를 따로 만들지는 않는다. CPU는
같은 Cirrus shader variant를 선택하며, variant 분리는 레이마칭 안의 동적 분기를
줄이기 위한 것이다.

## 3. 화면까지 가는 계산 순서

권운 한 점의 밀도는 다음 순서로 정해진다.

```text
Weather R/B/A
  → Flow basis로 회전한 Weather UV
  → A로 Minimum/Maximum Thickness 보간
  → 도메인의 Layer Center에 로컬 층 배치
  → 중심형 Vertical Profile
  → 같은 basis로 Base Texture3D 샘플
  → Detail Texture3D로 erosion
  → Direct/Sky/Ground/Multiple 조명
  → Full-resolution resolve와 Composite
```

Weather UV도 flow basis를 사용한다. Along 방향 주기는 `Weather World Size`, Across
방향 주기는 `Weather World Size × BaseAcross/BaseAlong`이다. 이 때문에 Base의 가로세로
비율과 Weather 섬의 비율이 함께 맞고, 긴 섬유 안에 짧은 Weather 덩어리가 반복해서
끊기는 현상이 줄어든다.

로컬 두께와 바닥은 다음과 같다.

```text
localThickness = lerp(minThickness, maxThickness, WeatherA)
center = domainBottom + domainThickness × layerCenter
localBottom = center - 0.5 × localThickness
localHeight = (worldY - localBottom) / localThickness
```

`localHeight`가 0~1 밖이면 밀도는 0이다. 그 안에서는 `Vertical Profile Half Width`가
중앙의 두꺼운 영역과 위·아래 fade 폭을 정한다. 저장 또는 적용 전에 최대 로컬 층과
위·아래 200m 경계 여유가 도메인 안에 들어오는지 검사한다.

## 4. F1 Cirrus와 F4 Desert Cirrus의 관계

둘은 계산 경로는 같지만 저장 슬롯은 다르다.

- F1 `Cirrus`는 구름 형성만 연습하는 독립 Type 슬롯이다.
- F4 `Desert Cirrus`는 독립 Concept 슬롯이며 구름 형성에 더해 사막 지면, 태양,
  대기, Physical Fill 같은 장면 설정을 적용한다.
- 최초 내장 F1 Cirrus 값은 Stage 15B 도입 시점의 F4 Desert 구름 값을 복사했다.
  그 이후 두 resolver와 사용자 파일은 서로 연결되지 않는다.
- 같은 `CloudFormationSettings`를 두 진입점에 넣으면 Weather/Shape/Domain과 최종
  밀도 경로는 같다. 최종 색은 F4의 장면 조명 때문에 달라질 수 있다.

F4 Concept를 선택한 뒤 F1/F2의 구름 슬라이더를 조절하면 편집 대상은 그 F4 Concept로
유지된다. F1 Type 버튼을 누르면 Concept target을 해제하고 해당 Type을 편집한다.
F1 상단의 `Target`, `Source`, `Unsaved` 표시를 저장 전에 반드시 확인한다.

## 5. 파라미터가 화면에 미치는 영향

Cirrus가 활성일 때 F1 `Physical Column Shape`에서 다음 값을 편집한다. 길이 슬라이더는
넓은 meter 범위를 다루기 위해 로그 배율이다.

| 파라미터 | 범위 | 커질 때 | 먼저 의심할 실패 |
|---|---:|---|---|
| Flow Angle | -180~180° | 긴 결 전체가 회전 | 카메라 회전과 함께 결이 회전하면 world anchoring 오류 |
| Base Along | 100~200,000m | 흐름 방향 덩어리가 길고 완만 | 너무 크면 화면이 거의 한 덩어리 |
| Base Across | 100~200,000m | 결의 폭이 넓고 둥글어짐 | Along과 비슷하면 솜구름화 |
| Base Vertical | 50~20,000m | 높이 방향 변화가 완만 | 너무 작으면 수평 층·barcode |
| Detail Along | 50~100,000m | 흐름 방향 침식이 길어짐 | Base와 같으면 작은 결이 사라짐 |
| Detail Across | 50~100,000m | 가로 침식이 넓어짐 | 너무 작으면 톱니·반짝임 |
| Detail Vertical | 25~10,000m | 높이 침식이 완만 | ray step보다 작으면 깜빡임 |
| Min/Max Thickness | 50~6,000m | 실제 권운 층이 두꺼워짐 | 너무 두꺼우면 적운처럼 보임 |
| Layer Center | 0~1 | 도메인 안에서 층 중심이 위로 이동 | 카메라와 층의 고도 관계 오판 |
| Profile Half Width | 0.01~1 | 중앙 질량이 넓고 위·아래 fade가 짧아짐 | 너무 크면 도메인 절단처럼 보임 |

공통 `Coverage`, `Density`, `Extinction`, `Detail Erosion`, `Wind`, Weather R/B/A와
`Weather World Size`도 계속 사용한다.

- Coverage는 Base noise에서 살아남는 양을 조절한다. 지나치게 올리면 섬유 사이가
  메워져 넓은 흰 판이 된다.
- Density는 살아남은 밀도의 양, Extinction은 같은 밀도가 빛을 가리는 강도다.
  두 값을 동시에 크게 올리면 내부가 빠르게 검어져 형상 판단이 어려워진다.
- Erosion은 Detail로 가장자리를 깎는 힘이다. 결 방향을 먼저 잡기 전에 크게 올리면
  노이즈 문제를 형상 문제로 오인하기 쉽다.
- Weather World Size가 작으면 50km 장면 안에서 같은 256² 지도가 자주 반복되고,
  작은 Weather texel과 Base/Detail이 겹쳐 얼룩·반짝임이 늘어난다. 제작 UI 하한은
  17.6km이며 기본 Desert는 128km다.

## 6. 권장 튜닝 순서

한 번에 여러 종류의 크기를 바꾸지 말고 다음 순서를 지킨다.

1. F1 `Cirrus` 또는 F4 `Desert Cirrus`를 불러오고 Temporal을 잠시 Off로 둔다.
2. `Flow Angle`만 바꿔 원하는 바람 결 방향을 정한다.
3. `Base Along`을 충분히 길게, `Base Across`를 그보다 짧게 두어 큰 aspect를 만든다.
4. `Weather World Size`를 조절해 큰 구름 띠의 간격과 화면 반복 횟수를 맞춘다.
5. `Min/Max Thickness`와 `Layer Center`로 실제 고도와 두께를 정한다.
6. `Profile Half Width`로 위·아래 fade를 다듬는다.
7. Detail Along/Across/Vertical과 Erosion으로 가장자리만 정리한다.
8. 마지막에 Coverage, Density, Extinction과 조명을 맞춘다.
9. F1 상단 target을 확인하고 `Save to Preset` 또는 `Save as Custom`을 누른다.

권장 시작 비율은 Base `20/4/1km`, Detail `6/1.5/0.5km`다. 절대값보다
`Along > Across > Vertical`의 계층과 ray step보다 충분히 큰 Detail 파장이 중요하다.

## 7. 흔한 문제와 고치는 순서

### Barcode처럼 평행한 줄이 반복된다

- Detail Across/Vertical이 너무 작지 않은지 확인한다.
- Weather World Size와 Base Along이 너무 작은 정수 비율로 반복되지 않는지 확인한다.
- Erosion을 낮춘 뒤 Base 형상부터 본다.
- F1 `Raw Noise → Base Density → Final Density` 순서로 처음 줄이 생기는 단계를 찾는다.

### 권운이 둥근 솜구름처럼 보인다

- Base Along을 키우거나 Base Across를 줄여 aspect를 키운다.
- Min/Max Thickness를 줄이고 Profile Half Width를 다듬는다.
- Coverage를 낮춰 서로 붙은 큰 덩어리를 분리한다.

### 한 높이에서 칼로 자른 것처럼 보인다

- `Local Thickness`와 `Local Height`로 실제 로컬 층 범위를 확인한다.
- Layer Center, Max Thickness, 도메인 두께가 200m 여유를 포함해 맞는지 확인한다.
- Profile Half Width가 1에 가까워 위·아래 fade가 사라지지 않았는지 확인한다.
- 권운은 Local Base Offset을 사용하지 않으므로 그 진단이 검정인 것은 정상이다.

### 멀리서 과도하게 반복되거나 움직일 때 반짝인다

- Weather World Size를 먼저 키운다.
- Detail 파장이 현재 view step보다 지나치게 작지 않게 한다.
- Temporal을 Off로 두고 원본 공간 aliasing인지, On에서만 생기는 history 문제인지 나눈다.
- Noise texture seed나 해상도를 바꾸는 문제로 오인하지 않는다. Cirrus 제작 값은
  texture를 재생성하지 않는다.

## 8. 진단 화면 읽기

F1의 Debug View를 다음 순서로 본다.

| 순서 | 진단 | 확인하는 것 |
|---:|---|---|
| 1 | Weather Coverage | R 채널의 큰 구름 띠와 빈 영역 |
| 2 | Weather Thickness Potential | A가 Min~Max 두께를 어디서 선택하는지 |
| 3 | Local Thickness | 각 열의 실제 meter 두께 |
| 4 | Local Height | 로컬 바닥 0, 상단 1과 범위 밖 0 |
| 5 | Raw Noise | 회전·비등방 샘플링 자체의 연속성 |
| 6 | Base Density | Weather, 두께, profile과 Base가 결합된 결과 |
| 7 | Detail Noise | 작은 침식 주파수와 방향 |
| 8 | Final Density | 최종 적분에 들어가는 밀도 |

`Cloud Type`은 일반 Weather Physical에서 Stratus/Cumulus 혼합을 보는 진단이다.
Cirrus 여부를 판정하는 화면으로 사용하지 않는다. F1 `Cirrus`와 F4 `Desert Cirrus`에
같은 formation 값을 넣었을 때 위 진단과 Weather hash가 같아야 한다. Composite 색만
다르면 F3/F4의 태양·대기·지면·Fill 차이를 확인한다.

## 9. 저장과 복원

권운 Type과 Desert Concept 파일은 각각 다음 위치에 원자 저장된다.

```text
captures/noise-lab/cloud-presets/types/cirrus.json
captures/noise-lab/cloud-presets/concepts/desert-cirrus.json
```

`Save to Preset`은 현재 target 하나만 덮어쓴다. `Save as Custom`은 현재 formation을
`cloud-presets/custom.json`에 저장하고 target을 Custom으로 바꾼다. `Restore Built-in`은
확인 뒤 현재 F1/F4 override 파일 하나만 제거한다. 손상되거나 범위를 벗어난 Type/Concept
파일은 보존한 채 내장값으로 fallback하고, Custom은 저장 파일이 없으면 현재 상태를
유지하며 저장된 formation이 없다는 상태를 표시한다. 자동 smoke/performance 실행은 재현성을 위해
사용자 override를 읽지 않는다.
