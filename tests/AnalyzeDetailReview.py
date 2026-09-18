"""Detail 강도 비교의 ROI 수치와 원본 PNG 갤러리. 시각적 승인은 별도."""
import json
import sys
from pathlib import Path
import numpy as np

root = Path(sys.argv[1])
names = ['B-detail0', 'B-detailDefault', 'B-detail05', 'B-detail1', 'D-detail1']
labels = ['Detail 0', '현재 0.24', 'Detail 0.5', 'Detail 1.0', 'Detail 1.0 + 그림자에도 침식 적용 (진단)']

def read(p):
    a = np.fromfile(p, '<f2').reshape(1080, 1920, 4)[560:752, 624:880].astype(np.float32)
    assert np.isfinite(a).all(), p
    return a

rows = []
sections = []
for az in (90, 0):
    for alt in (18, 5):
        prefix = f'az{az}-alt{alt}'
        load = lambda n, p: read(root / f'{prefix}-{n}-pack{p}.rgba16f')
        mask = load('B-detailDefault', 1)[:, :, 3] < .9
        # 그림자만 바꾼 쌍은 View T/tau와 phase가 정확히 같아야 한다.
        assert np.array_equal(load('B-detail1', 3)[:, :, 1:], load('D-detail1', 3)[:, :, 1:])
        cards = []
        for name, label in zip(names, labels):
            a, b = load(name, 1), load(name, 3)
            for pack in (2, 5, 7):
                load(name, pack)
            row = dict(azimuth=az, altitude=alt, variant=name,
                       opacity=float((1-a[:, :, 3])[mask].mean()),
                       viewTau=float(b[:, :, 1][mask].mean()),
                       weightedSunT=float(b[:, :, 0][mask].mean()),
                       direct=float(a[:, :, 0][mask].mean()))
            rows.append(row)
            file = f'{prefix}-{name}-composite.png'
            assert (root / file).exists()
            cards.append(f'<figure><figcaption>{label}</figcaption><a href="release/{file}"><img src="release/{file}"></a><small>ROI 不透明度 {row["opacity"]:.3f} · View τ {row["viewTau"]:.3f} · Direct {row["direct"]:.4f}</small></figure>')
        direction = '순광 · 태양을 등짐' if az == 90 else '측광'
        sections.append(f'<h2>{direction} / 고도 {alt}°</h2><div class="grid">'+''.join(cards)+'</div>')
diff = None
if len(sys.argv) > 2:
    other = Path(sys.argv[2])
    diff = max(float(np.abs(read(p)-read(other/p.name)).max()) for p in root.glob('*-pack*.rgba16f'))
    assert diff < .002, diff
result = dict(roi=[624,560,880,752], mask='fixed default opacity > .1', debugReleaseMax=diff, cases=rows)
(root/'detail-analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
html = '''<!doctype html><html lang="ko"><meta charset="utf-8"><title>Detail 파임 비교</title><style>
body{background:#13171f;color:#eee;font:16px/1.6 sans-serif;margin:24px}h1,h2{color:#ffd493}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}figure{margin:0;background:#232a35;padding:10px}img{width:100%}small{color:#aebccc}a{color:#9fd6ff}@media(max-width:800px){.grid{grid-template-columns:1fr}}</style>
<h1>Detail 강도와 그림자 밀도 표현 비교</h1><p>Urban / F5 / 71초 · Density shaping .70 · Base 1.50 · sigma 1 · Rim 2 / Depth 1 · Powder Off. 조명·노출·노이즈 크기는 고정. 이미지 클릭 시 원본.</p>
<p>처음 네 장은 현재 Base 그림자를 유지하고 View 침식 강도만 변경합니다. 마지막 장은 Detail 1과 같은 View 밀도에서 그림자에도 침식을 적용한 진단입니다. 일반 실행 기본값은 변경하지 않았습니다.</p>
<p>ROI 수치는 현재 0.24에서 불투명도 &gt;.1인 동일 픽셀 집합입니다. 화면 평균이나 선명도 점수가 아닙니다. 가중 Sun T는 밀도 변경 시 가중 위치도 바뀝니다.</p>'''+''.join(sections)+'''<h2>사용자 확인</h2><ul><li>0→0.24→0.5→1에서 외곽과 골이 실제로 드러나는지, 구름만 얇아지는지 비교합니다.</li><li>Detail 1 두 장을 비교하여 같은 형태에 조명만 바뀌는지 봅니다. 그림자 표현 정합이 곧 더 강한 그림자를 뜻하지는 않습니다.</li><li>작은 구름 소실, 검은 껍질, 거친 무늬는 실패 징후입니다. 화면 채택은 미승인입니다.</li></ul></html>'''
(root.parent/'gallery.html').write_text(html.replace('不透明度','불투명도'), encoding='utf-8')
print(json.dumps(result, indent=2))
