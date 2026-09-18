"""기본 직접광만 k배이고 나머지 성분이 불변인지 HDR ROI에서 검사한다."""
from pathlib import Path
import json
import sys
import numpy as np

root=Path(sys.argv[1])
def read(path):
    return np.fromfile(path,'<f2').reshape(1080,1920,4)[560:752,624:880].astype(np.float32)
rows=[]
for alt in (18,5):
    def load(name,pack):
        return read(root/f'az-90-alt{alt}-B-direct{name}-pack{pack}.rgba16f')
    base=load('1',1)
    mask=base[:,:,3]<.9
    for name,k in [('1',1),('075',.75),('05',.5)]:
        p1=load(name,1)
        assert np.array_equal(p1[:,:,1:],base[:,:,1:])
        for pack in (2,3,7):
            assert np.array_equal(load(name,pack),load('1',pack))
        expected=k*(base[:,:,0]-base[:,:,1])+base[:,:,1]
        residual=float(np.abs(p1[:,:,0]-expected).max())
        assert residual<.001
        assert np.isfinite(p1).all()
        rows.append(dict(altitude=alt,k=k,directLuma=float(p1[:,:,0][mask].mean()),
            rimLuma=float(p1[:,:,1][mask].mean()),equationMaxError=residual,
            directRGB=load(name,5)[:,:,:3][mask].mean(axis=0).tolist()))
diff=None
if len(sys.argv)>2:
    other=Path(sys.argv[2])
    diff=max(float(np.abs(read(p)-read(other/p.name)).max()) for p in root.glob('*-pack*.rgba16f'))
    assert diff<.001
result=dict(roi=[624,560,880,752],mask='View opacity > .1',debugReleaseMax=diff,cases=rows)
(root/'direct-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
