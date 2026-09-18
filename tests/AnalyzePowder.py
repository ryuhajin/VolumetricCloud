"""Powder 강도·방향 반응과 나머지 성분 불변성 검사. 화면 승인은 별도다."""
import json
import sys
from pathlib import Path
import numpy as np

root=Path(sys.argv[1])
def read(path):
    return np.fromfile(path,'<f2').reshape(1080,1920,4)[560:752,624:880].astype(np.float32)
rows=[]
for az in (-90,0,90):
    for alt in (18,5):
        def load(name,pack):
            return read(root/f'az{az}-alt{alt}-B-powder{name}-pack{pack}.rgba16f')
        base=load('0',1)
        mask=base[:,:,3]<.9
        for name in ('0','025','05'):
            p1=load(name,1)
            assert np.isfinite(p1).all()
            assert np.array_equal(p1[:,:,1:],base[:,:,1:])
            for pack in (2,3,7):
                assert np.array_equal(load(name,pack),load('0',pack))
            assert np.all(p1[:,:,0]<=base[:,:,0]+.000001)
            if az==-90:
                assert np.array_equal(p1,base)
            rows.append(dict(azimuth=az,altitude=alt,strength=name,
                directMean=float(p1[:,:,0][mask].mean()),rimMean=float(p1[:,:,1][mask].mean()),
                changedPixels=int(np.count_nonzero(p1[:,:,0]!=base[:,:,0]))))
        delta25=load('025',1)[:,:,0]-base[:,:,0]
        delta50=load('05',1)[:,:,0]-base[:,:,0]
        assert np.abs(delta50-2*delta25).max()<.001
        assert np.all(load('05',1)[:,:,0]<=load('025',1)[:,:,0]+.000001)
diff=None
if len(sys.argv)>2:
    other=Path(sys.argv[2])
    diff=max(float(np.abs(read(p)-read(other/p.name)).max()) for p in root.glob('*-pack*.rgba16f'))
    assert diff<.001
result=dict(roi=[624,560,880,752],mask='View opacity > .1',debugReleaseMax=diff,cases=rows)
(root/'powder-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
