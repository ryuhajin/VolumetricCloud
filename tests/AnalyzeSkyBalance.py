"""두 하늘광 차폐 방식에서 Sky fill/Multiple 에너지의 독립성을 검사한다."""
import json
import sys
from pathlib import Path
import numpy as np

root=Path(sys.argv[1])
def read(path):
    return np.fromfile(path,'<f2').reshape(1080,1920,4)[560:752,624:880].astype(np.float32)
def load(alt,mode,candidate,pack):
    return read(root/f'az-90-alt{alt}-B-sky-{mode}-{candidate}-pack{pack}.rgba16f')

rows=[]
for alt in (18,5):
    reference={p:load(alt,'before','base',p) for p in (1,2,3,5,7)}
    mask=reference[1][:,:,3]<.9
    for mode in ('before','after'):
        base=load(alt,mode,'base',2)
        for candidate in ('base','sky','multi','both'):
            p2=load(alt,mode,candidate,2)
            for p in (1,3,5):
                assert np.array_equal(load(alt,mode,candidate,p),reference[p])
            assert np.array_equal(p2[:,:,1],reference[2][:,:,1])
            assert np.array_equal(p2[:,:,3],reference[2][:,:,3])
            assert np.isfinite(p2).all()
            if candidate in ('base','multi'):
                assert np.array_equal(p2[:,:,0],base[:,:,0])
            else:
                assert np.max(np.abs(p2[:,:,0]-base[:,:,0]*2/.85))<.00005
            if candidate in ('base','sky'):
                assert np.array_equal(p2[:,:,2],base[:,:,2])
            else:
                assert np.all(p2[:,:,2]<=base[:,:,2])
            assert np.array_equal(p2[:,:,2],load(alt,'before',candidate,2)[:,:,2])
            assert np.all(load(alt,'after',candidate,2)[:,:,0]>=load(alt,'before',candidate,2)[:,:,0])
            sky=float(p2[:,:,0][mask].mean())
            multiple=float(p2[:,:,2][mask].mean())
            rows.append(dict(altitude=alt,mode=mode,candidate=candidate,skyLuma=sky,
                multipleLuma=multiple,skyToMultiple=sky/max(multiple,1e-20),
                directLuma=float(reference[1][:,:,0][mask].mean()),
                skyRGB=load(alt,mode,candidate,7)[:,:,:3][mask].mean(axis=0).tolist()))
        assert np.array_equal(load(alt,mode,'both',2)[:,:,0],load(alt,mode,'sky',2)[:,:,0])
        assert np.array_equal(load(alt,mode,'both',2)[:,:,2],load(alt,mode,'multi',2)[:,:,2])
diff=None
if len(sys.argv)>2:
    other=Path(sys.argv[2])
    diff=max(float(np.abs(read(p)-read(other/p.name)).max()) for p in root.glob('*-pack*.rgba16f'))
    assert diff<.001
result=dict(roi=[624,560,880,752],mask='View opacity > .1',debugReleaseMax=diff,cases=rows)
(root/'balance-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
