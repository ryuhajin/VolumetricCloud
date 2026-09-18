"""06-B 선형 RGBA16F 경계 진단. 이미지 보정 없이 수치·프로파일만 분석한다."""
import argparse
import json
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("directory", type=Path)
a = p.parse_args()
root = a.directory
contract = json.loads((root / "contract.json").read_text())
x0, y0, x1, y1 = contract["roi"]
out = root / "analysis-results"
out.mkdir(exist_ok=False)

def load(alt, name, pack=1, roi=True):
    v = np.fromfile(root / f"alt{alt}-{name}-pack{pack}.rgba16f", dtype="<f2")
    v = v.reshape(1080, 1920, 4).astype(np.float64)
    assert np.isfinite(v).all()
    return v[y0:y1, x0:x1] if roi else v

def summary(v):
    return dict(mean=float(v.mean()), p50=float(np.median(v)),
                p95=float(np.quantile(v, .95)), max=float(v.max()), min=float(v.min())) if v.size else None

report = {"scope": contract, "altitudes": {}}
for alt in contract["altitudes"]:
    base = load(alt, "N0")
    reference = load(alt, "N3-v12p5-s12p5")
    optical = load(alt, "N0", 3)
    full = load(alt, "N0", roi=False)
    full_optical = load(alt, "N0", 3, roi=False)
    opacity = 1-base[..., 3]
    visible = opacity > .001
    masks = {"all_material": visible, "thin_0p001_0p1": (opacity > .001) & (opacity < .1),
             "middle_0p1_0p5": (opacity >= .1) & (opacity < .5), "thick_over0p5": opacity >= .5}
    results = dict(full_cloud_opacity=summary((1-full[..., 3])[1-full[..., 3] > .001]),
                   full_cloud_tau=summary(full_optical[..., 1][1-full[..., 3] > .001]),
                   regions={}, comparisons={})
    for label, mask in masks.items():
        light = load(alt, "N0", 2)
        results["regions"][label] = dict(pixels=int(mask.sum()),
            opacity=summary(opacity[mask]), sunT=summary(optical[..., 0][mask]),
            direct=summary(base[..., 0][mask]), rim=summary(base[..., 1][mask]),
            single=summary(base[..., 2][mask]), sky=summary(light[..., 0][mask]),
            ground=summary(light[..., 1][mask]), multiple=summary(light[..., 2][mask]))
    pairs = [("N0", "N1-s25"), ("N0", "N2-v25"), ("N0", "N3-v12p5-s12p5"),
             ("N1-s50", "N1-s25"), ("N2-v50", "N2-v25"),
             ("N3-v50-s50", "N3-v25-s25"), ("N3-v25-s25", "N3-v12p5-s12p5")]
    if (root/f"alt{alt}-D-v12p5-s12p5-detail-pack1.rgba16f").exists():
        pairs.append(("N3-v12p5-s12p5", "D-v12p5-s12p5-detail"))
    for first, second in pairs:
        before, after = load(alt, first), load(alt, second)
        channels = {}
        for channel, name in enumerate(["direct", "rim", "single", "viewT"]):
            d = abs(before[..., channel]-after[..., channel])[visible]
            ref = abs(after[..., channel])[visible]
            channels[name] = dict(absolute=summary(d), relative_mae=float(d.mean()/max(ref.mean(), 1e-4)),
                mixed_tolerance_failure_fraction=float(np.mean(d > .0005 + .005*ref)))
        results["comparisons"][first+" -> "+second] = channels
    report["altitudes"][str(alt)] = results
    # 고정된 세로 절단선. 픽셀 중심 좌표와 선형 휘도, 통일 스케일을 사용한다.
    col = (x1-x0)//2
    profile = np.column_stack((np.arange(y0,y1), opacity[:,col], optical[:,col,0],
        base[:,col,0], reference[:,col,0], base[:,col,1], reference[:,col,1],
        base[:,col,2], reference[:,col,2]))
    np.savetxt(out/f"alt{alt}-profile-x{x0+col}.csv",profile,delimiter=",",
        header="y,opacity,sunT,N0direct,N3direct,N0rim,N3rim,N0single,N3single",comments="")

sphere=np.genfromtxt(root/"sphere.csv",delimiter=",",names=True)
report["sphere"]={str(n):summary(sphere["absolute_error"][sphere["steps"]==n]) for n in [8,32,128,4096]}
(out/"results.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
for alt,r in report["altitudes"].items():
    print("ALT",alt,"full opacity",r["full_cloud_opacity"],"full tau",r["full_cloud_tau"])
    for k,v in r["regions"].items():
        print(k,"pixels",v["pixels"],"opacity",v["opacity"],"SunT",v["sunT"])
    for k,v in r["comparisons"].items():print(k,"direct",v["direct"])
print("sphere",report["sphere"])
