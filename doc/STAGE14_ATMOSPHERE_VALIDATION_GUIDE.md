# Stage 14 대기·지면 조명 사용자 검증 가이드

> 보관 문서(2026-09-01): Stage 14의 수학·화면 판정 의도만 보존한다. 아래 절차는 현재
> F3/F4 이름에 맞게 동기화했으며, 폐기된 Full/저해상도/Temporal·Desert·Time playback
> 조작은 더 이상 검증 항목이 아니다. 현재 최종 판정은
> [Stage 15 검증 가이드](STAGE15_PRESET_VALIDATION_GUIDE.md)를 따른다.

이 문서는 자동 수치 검증이 대신할 수 없는 하늘, 구름, 지면, 대기 원근과 HDR 출력의
화면 품질을 판정한다. 일반 실행 뒤 F1이 열려 있으면 F1로 닫고 F3를 연다.
F3 `Lighting Atmosphere Tone`과 F4 `Concepts Diagnostics`에서 아래 항목을 조작한다.
숫자 0~9와 F5~F8 계약은 이전 단계와 같다.

## 1. LUT가 실제로 생성됐는지 확인

F4 `Atmosphere view`에서 Transmittance, Multi Scattering, Sky View, Sky Irradiance,
Aerial Radiance/T를 차례로 고른다. `LUT previews`를 펼치면 `t8~t13` 실제 SRV thumbnail을
함께 볼 수 있다. 이 조작은 물리 설정을 바꾸지 않고 현재 LUT 결과를 표시한다.

정상 결과는 값이 방향·고도·거리에 따라 부드럽게 변하는 것이다. 전체 검정,
자홍(NaN/Inf), 빨강(음수), 노랑(표시 범위 초과)이 넓게 나타나거나
32개 slice 사이에 강한 밝기 점프가 있으면 실패다. 행성에 막힌 방향의 검정은 정상이다.

## 2. 태양 고도에 따른 공통 변화

먼저 F3 `Tone Mapping`에서 Exposure를 `0 EV`, White Balance를 `6500K`로 고정한다.
그 다음 F3 `Sun altitude`를 18° → 70° → 3° 순서로 움직이고 `Sun azimuth`도 바꾼다.
현재 UI 범위는 0~90°이며 Twilight -4° preset이나 Time-of-Day 재생 버튼은 없다.
이 조작은 같은 태양 방향을 Sky View, 대기 투과 태양광, 구름 직접광과 지면 조명에 전달하는지
검증한다. 정상이라면 태양이 낮아질수록 경로가 길어져 하늘과 직접광이 따뜻해지고 어두워지며,
하늘·구름·지면이 한 프레임 안에서 같은 방향으로 연속 변화한다.

한 프레임 전 하늘색이 남거나, 하늘만 노을인데 구름/지면은 정오색인 경우, Twilight에서
지면 직접광이 남는 경우, 기존 수동 warm tint가 중복되는 경우는 실패다.

## 3. 지면 재질과 구름 하부 반사광

F4의 Urban → Meadow → Snow를 적용해 Concrete → Grass → Snow를 바꾸거나, F3 `Ground albedo`와
`Ground bounce`를 직접 편집한다. Snow는 구름 하부가
가장 밝고, Grass는 상대적으로 G 성분이 Concrete보다 커야 한다. 색은 구름 하부의
가려진 영역에 제한적으로 보여야 하며 구름 전체가 초록이나 모래색으로 칠해지면 실패다.

Ground Bounce를 0으로 내리면 세 재질의 구름 차이는 사라지되 지면 표시색은 유지돼야 한다.
다시 1로 올렸을 때만 구름 하부 차이가 돌아와야 한다.

## 4. 대기 원근과 합성 경계

F6 Ground Horizon에서 먼 지면과 건물을 본다. 가까운 표면은 재질과 그림자가 읽히고,
거리가 멀어질수록 Aerial Radiance가 더해지고 Aerial T가 줄어 하늘 공기색으로 점진적으로
흐려져야 한다. 수평선 한 줄 seam, 물체 외곽의 검은 테두리, 32-slice 띠나 거리별 색 점프는 실패다.

현재 Full-resolution High 한 경로에서 구름이 없는 픽셀은 정확히 clear background로,
불투명 구름은 앞쪽 공기와 구름 radiance만 남아야 한다. 카메라 이동 때 배경 밝기가 한 frame
늦거나 구름 외곽에 이전 하늘색이 남으면 공통 합성 계약 실패다.

## 5. 조명 성분 분리

F4 `Surface Transmittance`, 숫자 `7` Direct, F4 `Aerial Radiance/Transmittance`를 본다.
Surface T는 지면/건물 그림자 원인, Direct는 태양 노출 면, Aerial은 거리에 따라 쌓이는
공기 radiance/T를 보여야 한다. Sky/Ground/Multiple 구름 성분은 HLSL debug ID 53~55에
남아 있지만 현재 F4 핵심 목록에는 노출하지 않는다. 성분별 의미와 코드 조절은
[구름·빛 튜닝 가이드](CLOUD_LIGHTING_TUNING_GUIDE.md)를 따른다.

## 6. Tone Mapping과 수동 태양 방향

F3 `Tone Mapping`에서 Exposure `-2→0→+2 EV`와 White Balance
`3500→6500→10000K`를 천천히 움직인다. Tone 값은 LUT나 Deep Cache를 재생성하지 않고 최종
출력만 연속적으로 바꿔야 한다. 현재 최종 모드는 ACES fitted 하나를 사용한다.

F3 `Sun azimuth/elevation`을 천천히 움직이며 하늘·구름·지면 그림자가 같은 frame에 같은 방향으로
변하는지 확인한다. 현재 일반 UI에는 Time playback, Pause, scrub, Temporal history가 없다.

## 승인 기록

- [ ] LUT thumbnail과 fullscreen 진단 검증
- [ ] 18°/70°/3° 수동 태양 고도 공통 변화
- [ ] Concrete/Grass/Snow 지면과 Ground Bounce Off/On
- [ ] F6 Aerial Perspective와 Full-resolution High 합성
- [ ] Surface/Direct/Aerial 성분 분리
- [ ] Exposure/White Balance 분리와 수동 태양 방향
- [ ] 사용자 최종 승인
