"""Rim depth 비교의 고정 ROI 불변성/기여량 검사. 화면 품질 승인은 별도다."""
import json
import sys
from pathlib import Path
import numpy as np

root = Path(sys.argv[1])
path_mode = (root/'path-contract.json').exists()
names = ('current', 'transport', 'joint') if path_mode else ('05', '1', '2')
base_name = 'current' if path_mode else '1'
def read(path):
    a = np.fromfile(path, dtype='<f2').reshape(1080, 1920, 4)
    return a[560:752, 624:880].astype(np.float32)

results = []
for az in (-90, 0, 90):
    for alt in (18, 5):
        prefix = f'az{az}-alt{alt}-B-' + ('' if path_mode else 'depth')
        baseline = [read(root / f'{prefix}{base_name}-pack{i}.rgba16f') for i in (1, 2, 3)]
        for depth in names:
            packs = [read(root / f'{prefix}{depth}-pack{i}.rgba16f') for i in (1, 2, 3)]
            assert all(np.isfinite(p).all() for p in packs)
            invariant = max(float(np.max(np.abs(packs[i]-baseline[i]))) for i in (1, 2))
            assert invariant == 0
            assert np.array_equal(packs[0][:,:,3], baseline[0][:,:,3])
            delta = packs[0]-baseline[0]
            residual = float(np.max(np.abs(delta[:,:,0]-delta[:,:,1])))
            # 저장된 half 값의 네 번 반올림 오차를 크기에 따라 허용한다.
            tolerance = max(.001, float(np.max(np.abs(packs[0][:,:,:2]))) * .002)
            assert residual < tolerance
            row = dict(azimuth=az, altitude=alt, depth=depth, invariantMax=invariant,
                       directMinusRimDeltaMax=residual, directMean=float(packs[0][:,:,0].mean()),
                       rimMean=float(packs[0][:,:,1].mean()), rimMax=float(packs[0][:,:,1].max()))
            opacity = 1-baseline[0][:,:,3]
            for name, lo, hi in [('thin', .001, .1), ('middle', .1, .5), ('core', .5, 1.001)]:
                mask = (opacity >= lo) & (opacity < hi)
                row[name] = dict(pixels=int(mask.sum()), rimMean=float(packs[0][:,:,1][mask].mean()) if mask.any() else None)
            results.append(row)
        low = read(root / f'{prefix}{"transport" if path_mode else "05"}-pack1.rgba16f')[:,:,1]
        high = read(root / f'{prefix}{"joint" if path_mode else "2"}-pack1.rgba16f')[:,:,1]
        assert np.all(low >= baseline[0][:,:,1]) and np.all(baseline[0][:,:,1] >= high)
comparison = None
if len(sys.argv) > 2:
    other = Path(sys.argv[2])
    comparison = max(float(np.max(np.abs(read(p)-read(other/p.name)))) for p in root.glob('*-pack*.rgba16f'))
    assert comparison < .001
output = dict(roi=[624,560,880,752], classification='View opacity, not physical silhouette thickness',
              debugReleaseMax=comparison, cases=results)
(root/('path-analysis.json' if path_mode else 'depth-analysis.json')).write_text(json.dumps(output, indent=2), encoding='utf-8')
print(json.dumps(output, indent=2))
