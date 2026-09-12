"""Run the prospectively specified E0 matrix with source/output provenance."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    exe = repo / "out/build/x64-Release/SmokeAppearanceCapture.exe"
    sources = [Path(__file__), repo / "diagnostics/SmokeAppearanceCapture.cpp",
               repo / "diagnostics/appearance_capture.hlsl",
               repo / "src/Shaders/pixel_smoke.hlsl", repo / "CMakeLists.txt",
               repo / "experiments/appearance-attribution/SPEC.md", repo / "RESEARCH_PLAN.md",
               repo / "diagnostics/analyze_appearance_capture.py"]
    if not exe.exists() or any(p.stat().st_mtime > exe.stat().st_mtime for p in sources[1:5]):
        raise RuntimeError("Missing or stale capture executable; rebuild target")
    args.output.mkdir(parents=True, exist_ok=False)
    snapshot = args.output / "sources"
    snapshot.mkdir()
    hashes = {str(p.relative_to(repo)): sha(p) for p in sources}
    for p in sources:
        target = snapshot / p.relative_to(repo)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(p.read_bytes())
    manifest = dict(protocol="E0 v1.1", primary_step=.01, executable_sha256=sha(exe), sources=hashes,
                    compiler="MSVC Release; HLSL 5.1 strict O3", image_shape=[512,512,4],
                    dtype="little-endian float32", capture_format="RGBA32_FLOAT",
                    density_format="R32_FLOAT", filter="linear clamp", density_scale=1,
                    absorption=4, colour=[.7,.7,.7], light_direction=[1,-1,1],
                    cameras=[[2.5,1.5,2.5],[-1.5,1.5,2.5],[.5,1.5,-2]],
                    look_at=[.5,.5,.5], up=[0,1,0], fov_degrees=35,
                    fields={"empty":0,"constant":.25,"gaussian":{"peak":1,"sigma":.15,"centre":[.5]*3}},
                    simulation=False, timing_claim=False, runs=[], complete=False)
    path = args.output / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2))
    for n in (32,64,128):
        for field in ("empty","constant","gaussian"):
            for step in (.02,.01,.005):
                name = f"{field}-n{n}-s{step}"
                command = [str(exe),str(n),field,str(step),str(repo/"src/Shaders"),
                           str(args.output.resolve()/name),str(repo/"diagnostics/appearance_capture.hlsl")]
                result = subprocess.run(command, capture_output=True, text=True, timeout=180)
                (args.output / f"{name}.log").write_text(result.stdout+result.stderr)
                if result.returncode:
                    raise RuntimeError(f"Capture failed: {name}: {result.stdout} {result.stderr}")
                files = {p.name:sha(p) for p in (args.output/name).iterdir() if p.is_file()}
                manifest["runs"].append(dict(name=name,n=n,field=field,step=step,files=files,command=command))
                path.write_text(json.dumps(manifest,indent=2))
                print(name, flush=True)
    if hashes != {str(p.relative_to(repo)):sha(p) for p in sources} or sha(exe)!=manifest["executable_sha256"]:
        raise RuntimeError("Sources/executable changed during run")
    manifest["complete"] = True
    path.write_text(json.dumps(manifest,indent=2))


if __name__ == "__main__":
    main()
