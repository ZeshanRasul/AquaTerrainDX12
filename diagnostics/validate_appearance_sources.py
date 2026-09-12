"""E0 source quadrature/integral and rendered refinement controls (no flow)."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import numpy as np
from appearance_source import sphere_rate


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(); p.add_argument("--output",type=Path,required=True); args=p.parse_args()
    repo=Path(__file__).resolve().parents[1]; out=args.output.resolve()
    exe=repo/"out/build/x64-Release/SmokeAppearanceCapture.exe"
    sources=[repo/"diagnostics/appearance_source.py",Path(__file__),repo/"diagnostics/SmokeAppearanceCapture.cpp",
             repo/"diagnostics/appearance_capture.hlsl",repo/"src/Shaders/pixel_smoke.hlsl",
             repo/"experiments/appearance-attribution/SPEC.md"]
    if not exe.exists() or any(f.stat().st_mtime>exe.stat().st_mtime for f in sources[2:5]):
        raise RuntimeError("Missing/stale executable")
    out.mkdir(parents=True,exist_ok=False)
    hashes={str(f.relative_to(repo)):sha(f) for f in sources}
    for f in sources:
        dest=out/"sources"/f.relative_to(repo); dest.parent.mkdir(parents=True,exist_ok=True); dest.write_bytes(f.read_bytes())
    manifest=dict(executable_sha256=sha(exe),source_sha256=hashes,runs=[],complete=False,
                  shape_order="zyx",capture_shape=[512,512,4],dtype="little-endian float32",
                  simulated_seconds=0,static_rate_multiplier_seconds=1,step=.01,
                  quadratures=[16,32],selected_quadrature=16,
                  scope="Static source tables only; GPU time integration remains unvalidated")
    scenes={"A":[(.5,.15,.5)],"B":[(.35,.15,.5),(.65,.15,.5)]}
    comparisons=[]; all_valid=True; images={}
    for n in (32,64,128):
        for scene,centres in scenes.items():
            for q in (16,32):
                records=[]; field=np.zeros((n,n,n),dtype="<f4")
                for centre in centres:
                    rate,record=sphere_rate(n,centre,q); field+=rate; records.append(record)
                name=f"source-{scene}-n{n}-q{q}"; density=out/f"{name}.f32"; field.tofile(density)
                integral=float(field.sum(dtype=np.float64)/n**3); target=.002*len(centres)
                relative_error=abs(integral-target)/target
                command=[str(exe),str(n),str(density),"0.01",str(repo/"src/Shaders"),str(out/name),str(repo/"diagnostics/appearance_capture.hlsl")]
                result=subprocess.run(command,capture_output=True,text=True,timeout=180)
                (out/f"{name}.log").write_text(result.stdout+result.stderr)
                if result.returncode: raise RuntimeError(result.stdout+result.stderr)
                repeated=True; values_valid=True
                for view in range(3):
                    a=np.fromfile(out/name/f"view{view}-trial0.rgba32f",dtype="<f4").reshape(512,512,4)
                    values_valid &= bool(np.isfinite(a).all() and a.min()>=0 and a[...,3].max()<=1 and (a[...,:3]<=a[...,3:]+1e-6).all())
                    for trial in (1,2): repeated &= (out/name/f"view{view}-trial{trial}.rgba32f").read_bytes()==(out/name/f"view{view}-trial0.rgba32f").read_bytes()
                    images[scene,n,q,view]=a[...,3].astype(np.float64)
                exact_upload=(out/name/"density-upload-readback.f32").read_bytes()==density.read_bytes()
                valid=bool(repeated and values_valid and exact_upload and relative_error<=1e-7)
                all_valid &= valid
                manifest["runs"].append(dict(name=name,n=n,scene=scene,q=q,sources=records,
                                             integral=integral,target=target,relative_error=relative_error,
                                             repeated=repeated,values_valid=values_valid,exact_upload=exact_upload,passed=valid,
                                             input_sha256=sha(density),files={f.name:sha(f) for f in (out/name).iterdir() if f.is_file()}))
                (out/"manifest.json").write_text(json.dumps(manifest,indent=2))
            print(f"Source {scene} N={n} complete",flush=True)
    for scene in scenes:
        for view in range(3):
            # The protocol uses ONE mask shared across resolutions, not one
            # thresholded mask per N (which falsely rejects a small fine source).
            roi=np.logical_or.reduce([images[scene,n,q,view]>=.05 for n in (32,64,128) for q in (16,32)])
            for n in (32,64,128):
                count=int(roi.sum()); mae=float(np.abs(images[scene,n,16,view]-images[scene,n,32,view])[roi].mean()) if count else None
                passed=count>=1024 and mae<.003
                comparisons.append(dict(scene=scene,n=n,view=view,roi_pixels=count,mae=mae,passed=passed))
                all_valid &= passed
    if hashes!={str(f.relative_to(repo)):sha(f) for f in sources} or sha(exe)!=manifest["executable_sha256"]:
        raise RuntimeError("Sources changed during run")
    manifest["complete"]=True; (out/"manifest.json").write_text(json.dumps(manifest,indent=2))
    summary=dict(passed=bool(all_valid),worst_integral_relative_error=max(r["relative_error"] for r in manifest["runs"]),
                 worst_rendered_refinement=max(r["mae"] for r in comparisons if r["mae"] is not None),
                 comparisons=comparisons,scope=manifest["scope"])
    (out/"analysis.json").write_text(json.dumps(summary,indent=2)); print(json.dumps(summary,indent=2))


if __name__=="__main__": main()
