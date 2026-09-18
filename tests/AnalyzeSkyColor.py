"""고정 LUT 하늘광 분리의 성분 불변성과 선형 RGB를 검사한다."""
import json
import sys
from pathlib import Path
import numpy as np

root = Path(sys.argv[1])
def read(path):
    return np.fromfile(path, '<f2').reshape(1080,1920,4)[560:752,624:880].astype(np.float32)

rows=[]
for az in (-90,0,90):
    for alt in (18,5):
        def load(name, pack):
            return read(root/f'az{az}-alt{alt}-B-sky-{name}-pack{pack}.rgba16f')
        for pack in (1,3,5):
            assert np.array_equal(load('before',pack),load('after',pack))
        b,a=load('before',2),load('after',2)
        assert np.array_equal(b[:,:,1:],a[:,:,1:])
        assert np.all(a[:,:,0]>=b[:,:,0])
        mask=load('before',1)[:,:,3]<.9
        row=dict(azimuth=az,altitude=alt,pixels=int(mask.sum()))
        for name in ('before','after'):
            sky=load(name,7)
            assert np.isfinite(sky).all()
            row[name+'SkyRGB']=sky[:,:,:3][mask].mean(axis=0).tolist()
            row[name+'SkyLuma']=float(load(name,2)[:,:,0][mask].mean())
        row['directRGB']=load('before',5)[:,:,:3][mask].mean(axis=0).tolist()
        row['multipleLuma']=float(b[:,:,2][mask].mean())
        rows.append(row)
diff=None
if len(sys.argv)>2:
    other=Path(sys.argv[2])
    diff=max(float(np.abs(read(p)-read(other/p.name)).max()) for p in root.glob('*-pack*.rgba16f'))
    assert diff<.001
result=dict(roi=[624,560,880,752],debugReleaseMax=diff,cases=rows)
(root/'sky-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
