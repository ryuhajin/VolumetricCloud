# Stage 14 대기·지면 조명 사용자 검증 가이드

이 문서는 자동 수치 검증이 대신할 수 없는 하늘, 구름, 지면, 대기 원근과 HDR 출력의
화면 품질을 판정한다. 일반 실행 뒤 F1이 열려 있으면 F1로 닫고 F3를 연다.
최상단 `Atmosphere Debug`, 독립 `Tone Mapping`, 그 아래 `Lighting & Atmosphere`에서 아래 항목을 조작한다.
숫자 0~9와 F5~F8 계약은 이전 단계와 같다.

## 1. LUT가 실제로 생성됐는지 확인

F3 `Atmosphere Debug`에서 Transmittance, Multi Scattering, Sky View, Sky Irradiance를
차례로 고른다. 이 조작은 최종 렌더값을 바꾸지 않고 `t8~t11` 실제 SRV를 thumbnail로 보여준다.
각 그림에는 크기/포맷, generation/hash, 축 의미와 현재 lookup 십자선이 표시돼야 한다.
Aerial Radiance/T는 slice를 0~31로 움직여 fullscreen으로 본다.

정상 결과는 값이 방향·고도·거리에 따라 부드럽게 변하고 십자선과 slice 번호가 조작을
따라가는 것이다. 전체 검정, 자홍(NaN/Inf), 빨강(음수), 노랑(표시 범위 초과)이 넓게 나타나거나
32개 slice 사이에 강한 밝기 점프가 있으면 실패다. 행성에 막힌 방향의 검정은 정상이다.

## 2. 태양 고도에 따른 공통 변화

먼저 F3 독립 `Tone Mapping`에서 Exposure를 `0 EV`, White Balance를 `6500K`로 고정한다.
그 다음 `Lighting & Atmosphere → Sun Direction & Time → Angle & Directional Light`에서 Sun preset을
Morning 18° → Noon 70° → Sunset 3° → Twilight -4° 순서로 누른다.
이 조작은 같은 태양 방향을 Sky View, 대기 투과 태양광, 구름 직접광과 지면 조명에 전달하는지
검증한다. 정상이라면 태양이 낮아질수록 경로가 길어져 하늘과 직접광이 따뜻해지고 어두워지며,
하늘·구름·지면이 한 프레임 안에서 같은 방향으로 연속 변화한다.

한 프레임 전 하늘색이 남거나, 하늘만 노을인데 구름/지면은 정오색인 경우, Twilight에서
지면 직접광이 남는 경우, 기존 수동 warm tint가 중복되는 경우는 실패다.

## 3. 지면 재질과 구름 하부 반사광

F5와 F7에서 `Environment, Ground & Cloud Multiple Scattering → Ground Reflection`의
Ground Preset을 Concrete → Grass → Snow → Desert로 바꾼다.
지면 자체 linear albedo와 전역 Ground Bounce가 함께 바뀌는지 비교한다. Snow는 구름 하부가
가장 밝고, Grass는 상대적으로 G, Desert는 R 성분이 Concrete보다 커야 한다. 색은 구름 하부의
가려진 영역에 제한적으로 보여야 하며 구름 전체가 초록이나 모래색으로 칠해지면 실패다.

Ground Bounce를 0으로 내리면 네 재질의 구름 차이는 사라지되 지면 표시색은 유지돼야 한다.
다시 1로 올렸을 때만 구름 하부 차이가 돌아와야 한다.

## 4. 대기 원근과 합성 경계

F6 Ground Horizon에서 먼 지면과 건물을 본다. 가까운 표면은 재질과 그림자가 읽히고,
거리가 멀어질수록 Aerial Radiance가 더해지고 Aerial T가 줄어 하늘 공기색으로 점진적으로
흐려져야 한다. 수평선 한 줄 seam, 물체 외곽의 검은 테두리, 32-slice 띠나 거리별 색 점프는 실패다.

Full, 50% Spatial, Temporal On을 차례로 비교한다. 구름이 없는 픽셀은 정확히 clear background로,
불투명 구름은 앞쪽 공기와 구름 radiance만 남아야 한다. 경로 전환 때 배경 밝기가 바뀌거나
구름 외곽에 이전 하늘색이 남으면 공통 합성 계약 실패다.

## 5. 조명 성분 분리

Stage 12 Surface T, 구름 Direct, Sky Ambient, Ground Bounce, Aerial Only를 차례로 본다.
Surface T는 지면/건물과 구름 자기 그림자 원인, Direct는 태양 노출 면, Sky는 위쪽의 넓은 fill,
Ground는 구름 아래쪽의 재질색 fill, Aerial Only는 거리에 따라 쌓이는 공기색만 보여야 한다.
서로 같은 그림을 반복하거나 한 성분을 꺼도 다른 이름의 출력까지 사라지면 실패다.

## 6. Tone Mapping과 시간 재생

F3 독립 `Tone Mapping`에서 Exposure `-2→0→+2 EV`와 White Balance
`3500→6500→10000K`를 천천히 움직인다. 같은 섹션의 읽기 전용 `Temporal Status`에서
history valid와 accumulated가 유지되는지 확인한다. 실제 Temporal On/Off·Reset·Weight 조작은
F1 `Temporal`에만 있다. 최종 화면만 연속적으로 변하고 LUT generation/hash와 Temporal history는 유지돼야 한다.
ACES를 끈 Linear Debug는 HDR clipping 확인용이며 최종 미학 판정에는 ACES On을 사용한다.

Time of Day를 Play, 속도 30 simulated min/s로 두고 카메라를 이동한다. 프레임당 태양 변화가
0.25° 이하일 때 history가 유지되면서 하늘 banding, 구름 ghost, 수평선 seam과 이전 LUT의
한 프레임 노출이 없어야 한다. 19:30 뒤 05:30으로 항상 이어져야 하며, Pause를 누르면
그 순간 방향이 Angle로 인계되어 태양·그림자가 점프하지 않아야 한다.

## 승인 기록

- [ ] LUT thumbnail/축/십자선/slice 검증
- [ ] Morning/Noon/Sunset/Twilight 공통 변화
- [ ] 네 지면 재질과 Ground Bounce Off/On
- [ ] F6 Aerial Perspective와 Full/50%/Temporal 합성
- [ ] Surface/Direct/Sky/Ground/Aerial 성분 분리
- [ ] Exposure/White Balance/history 분리와 시간 재생
- [ ] 사용자 최종 승인
