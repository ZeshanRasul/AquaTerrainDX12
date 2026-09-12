"""Validate shader captures; no simulation claim is tested by this script."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image


def box_lengths(eye, size=512):
    eye=np.asarray(eye,dtype=np.float64)
    forward=.5-eye; forward/=np.linalg.norm(forward)
    right=np.cross(forward,[0,1,0]); right/=np.linalg.norm(right)
    up=np.cross(right,forward)
    y,x=np.mgrid[:size,:size]
    direction=forward+np.tan(np.deg2rad(35)/2)*((2*(x[...,None]+.5)/size-1)*right+(1-2*(y[...,None]+.5)/size)*up)
    direction/=np.linalg.norm(direction,axis=-1,keepdims=True)
    with np.errstate(divide="ignore",invalid="ignore"):
        a=-eye/direction; b=(1-eye)/direction
    enter=np.min(np.maximum(a,b),axis=-1)
    start=np.max(np.minimum(a,b),axis=-1)
    return np.maximum(0,enter-np.maximum(start,0))


def main():
    p=argparse.ArgumentParser(); p.add_argument("root",type=Path); args=p.parse_args()
    manifest=json.loads((args.root/"manifest.json").read_text())
    primary=manifest.get("primary_step",.02)
    if not manifest["complete"] or len(manifest["runs"])!=27:
        raise RuntimeError("Incomplete E0 matrix")
    expected={(f,n,s) for f in ("empty","constant","gaussian") for n in (32,64,128) for s in (.02,.01,.005)}
    if {(r["field"],r["n"],r["step"]) for r in manifest["runs"]}!=expected:
        raise RuntimeError("Missing or duplicated configuration")
    for name,digest in manifest["sources"].items():
        if hashlib.sha256((args.root/"sources"/name).read_bytes()).hexdigest()!=digest:
            raise RuntimeError(f"Source snapshot mismatch: {name}")
    images={}; checks=[]; refinement=[]; spatial=[]; analytic=[]
    for run in manifest["runs"]:
        folder=args.root/run["name"]
        for name,digest in run["files"].items():
            if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=digest:
                raise RuntimeError(f"Hash mismatch: {folder/name}")
        for view in range(3):
            a=np.fromfile(folder/f"view{view}-trial0.rgba32f",dtype="<f4").reshape(512,512,4)
            repeated=all(np.array_equal(a,np.fromfile(folder/f"view{view}-trial{trial}.rgba32f",dtype="<f4").reshape(a.shape)) for trial in (1,2))
            valid=bool(np.isfinite(a).all() and a.min()>=0 and a[...,3].max()<=1 and (a[...,:3]<=a[...,3:]+1e-6).all())
            empty_ok=bool(run["field"]!="empty" or (a==0).all())
            checks.append(dict(run=run["name"],view=view,repeated=repeated,valid=valid,empty_ok=empty_ok))
            images[run["field"],run["n"],run["step"],view]=a[...,3].astype(np.float64)
            if run["n"]==64 and run["step"]==.005:
                rgb=a[...,:3]+.18*(1-a[...,3:])
                srgb=np.where(rgb<=.0031308,12.92*rgb,1.055*np.maximum(rgb,0)**(1/2.4)-.055)
                Image.fromarray(np.uint8(np.clip(srgb,0,1)*255)).save(args.root/f"preview-{run['field']}-view{view}.png")
    for field in ("constant","gaussian"):
        for view in range(3):
            roi=np.logical_or.reduce([images[field,n,s,view]>=.05 for n in (32,64,128) for s in (.02,.01,.005)])
            if roi.sum()<1024: raise RuntimeError("Insufficient control ROI")
            for n in (32,64,128):
                fine=images[field,n,.005,view]
                for step in (.02,.01):
                    error=np.abs(images[field,n,step,view]-fine)
                    refinement.append(dict(field=field,view=view,n=n,step=step,roi_pixels=int(roi.sum()),mae=float(error[roi].mean()),maximum=float(error.max()),passed=bool(error[roi].mean()<.003)))
                if field=="constant":
                    exact=1-np.exp(-box_lengths(manifest["cameras"][view]))
                    analytic.append(dict(view=view,n=n,mae=float(np.abs(fine-exact)[roi].mean()),maximum=float(np.abs(fine-exact).max())))
            for step in (primary,.005):
                for n in (32,64):
                    error=np.abs(images[field,n,step,view]-images[field,128,step,view])
                    spatial.append(dict(field=field,view=view,n=n,step=step,mae=float(error[roi].mean()),passed=bool(error[roi].mean()<.003)))
    valid=all(c["repeated"] and c["valid"] and c["empty_ok"] for c in checks)
    analytic_valid=all(r["mae"]<.003 and r["maximum"]<=.0051 for r in analytic)
    primary_pass=valid and analytic_valid and all(r["passed"] for r in refinement if r["step"]==primary) and all(r["passed"] for r in spatial)
    summary=dict(capture_valid=valid,primary_step=primary,render_controls_pass=primary_pass,analytic_valid=analytic_valid,
                 worst_primary_refinement=max(r["mae"] for r in refinement if r["step"]==primary),
                 worst_legacy_step_002_refinement=max(r["mae"] for r in refinement if r["step"]==.02),
                 worst_step_001_refinement=max(r["mae"] for r in refinement if r["step"]==.01),
                 worst_spatial=max(r["mae"] for r in spatial),checks=checks,
                 refinement=refinement,spatial=spatial,constant_analytic=analytic,
                 scope="E0 rendering controls only; accounting/source/pressure validity and G1 remain untested")
    (args.root/"analysis.json").write_text(json.dumps(summary,indent=2))
    print(json.dumps({k:v for k,v in summary.items() if not isinstance(v,list)},indent=2))


if __name__=="__main__": main()
