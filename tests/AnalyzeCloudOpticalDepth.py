"""06-B: 동일 ROI에서 밀도 표현과 소멸계수의 영향을 별도로 비교한다."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np

p=argparse.ArgumentParser()
p.add_argument("directory",type=Path)
p.add_argument("--cache",action="store_true",help="High View + 80/40 캐시의 1/2/4배 비교")
args=p.parse_args()
root=args.directory
scales=[1,2,4] if args.cache else [1,2,4,8]
contract=json.loads((root/"contract.json").read_text())
x0,y0,x1,y1=contract["roi"]
out=root/"analysis"
out.mkdir(exist_ok=False)

def load(alt,kind,scale,pack):
    return np.fromfile(root/f"alt{alt}-{kind}-scale{scale}-pack{pack}.rgba16f",dtype="<f2").reshape(1080,1920,4)[y0:y1,x0:x1].astype(float)

rows=[]
checks=[]
for alt in contract["altitudes"]:
    base=load(alt,"B",1,1)
    opacity=1-base[...,3]
    masks={"all":opacity>.001,"thin":(opacity>.001)&(opacity<.1),
           "middle":(opacity>=.1)&(opacity<.5),"core":opacity>=.5}
    tau1=load(alt,"B",1,3)[...,1]
    for scale in scales:
        b,d=load(alt,"B",scale,1),load(alt,"D",scale,1)
        tdiff=float(np.max(abs(b[...,3]-d[...,3])))
        assert tdiff==0,"밀도 표현 비교가 View T를 바꿈"
        for kind in ["B","D"]:
            light=load(alt,kind,scale,1)
            ambient=load(alt,kind,scale,2)
            optical=load(alt,kind,scale,3)
            assert np.isfinite(light).all() and np.isfinite(optical).all()
            assert ((light[...,3]>=0)&(light[...,3]<=1)).all()
            tau=optical[...,1]
            terr=abs(light[...,3]-np.exp(-tau))
            tauerr=abs(tau-scale*tau1)
            # half 저장 양자화 허용. 모델 변화의 효과를 숨기는 밝기 오차 허용치가 아니다.
            assert np.max(terr)<.001
            if not args.cache: assert np.max(tauerr)<.005
            checks.append(dict(alt=alt,kind=kind,scale=scale,viewT_BD_max=tdiff,
                beer_max=float(terr.max()),tau_scaling_max=float(tauerr.max())))
            for region,mask in masks.items():
                def mean(v):return float(v[mask].mean())
                rows.append(dict(alt=alt,shadow=kind,scale=scale,region=region,pixels=int(mask.sum()),
                    direct=mean(light[...,0]),rim=mean(light[...,1]),single=mean(light[...,2]),
                    viewT=mean(light[...,3]),sunT=mean(optical[...,0]),tau=mean(tau),
                    sky=mean(ambient[...,0]),ground=mean(ambient[...,1]),multiple=mean(ambient[...,2])))
            col=(x1-x0)//2
            np.savetxt(out/f"alt{alt}-{kind}{scale}-profile.csv",np.column_stack((np.arange(y0,y1),light[:,col,:],optical[:,col,:])),
                delimiter=",",header="y,direct,rim,single,viewT,sunT,tau,phase,viewTcopy",comments="")
with (out/"regions.csv").open("w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
(out/"checks.json").write_text(json.dumps(checks,indent=2),encoding="utf-8")
for alt in contract["altitudes"]:
    print("ALT",alt)
    for kind in ["B","D"]:
        for scale in scales:
            group={r['region']:r for r in rows if r['alt']==alt and r['shadow']==kind and r['scale']==scale}
            print(kind,scale,"direct(all/thin/core)",*[round(group[k]['direct'],5) for k in ['all','thin','core']],
                  "single(thin/core)",*[round(group[k]['single'],5) for k in ['thin','core']],
                  "SunT(thin/core)",*[round(group[k]['sunT'],4) for k in ['thin','core']],
                  "core tau",round(group['core']['tau'],3))
print('View parity / finite / Beer checks passed' if args.cache else 'All View parity / Beer / tau scaling checks passed')
