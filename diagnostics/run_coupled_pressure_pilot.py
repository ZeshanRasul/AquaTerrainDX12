"""Run the preregistered coupled-pressure selection pilot.

This orchestration implements COUPLED_PRESSURE_CONTROLS.md verbatim.  It exposes
only source/snapshot validity and pressure decisions; it neither renders density
nor calculates the cross-resolution G1 quantity.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


RESOLUTIONS = (32, 64, 128)
SCENES = ("A", "B")
ITERATION_LADDER = (256, 1024, 4096, 16384, 65536)
TOTAL_STEPS = 180
EMITTER_STEPS = 120
DT_FLOAT32 = 0.01666666753590107
RUNNER_SOURCE_PATHS = (
    "diagnostics/run_coupled_appearance.py",
    "diagnostics/analyze_coupled_appearance.py",
    "diagnostics/appearance_source.py",
    "src/Renderer/Renderer.cpp",
    "src/Renderer/Renderer.h",
    "src/Renderer/SmokeGpuDiagnostics.cpp",
    "src/Renderer/SmokeAppearanceExperiment.cpp",
    "src/Shaders/3d_smoke_compute.hlsl",
    "src/Shaders/appearance_source_injection.hlsl",
    "src/Window.cpp",
    "src/WinMain.cpp",
    "experiments/appearance-attribution/SPEC.md",
    "experiments/appearance-attribution/SOURCE_INJECTION_CONTROLS.md",
    "experiments/appearance-attribution/COUPLED_PRESSURE_CONTROLS.md",
    "RESEARCH_PLAN.md",
    "CMakeLists.txt",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def atomic_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def environment_hashes(repo: Path, executable: Path) -> dict:
    sources = {
        str(Path(name)): sha256(repo / name) for name in RUNNER_SOURCE_PATHS
    }
    runtime = {
        "appearance_source_injection.hlsl": sha256(
            executable.parent / "Shaders/appearance_source_injection.hlsl"
        ),
        "3d_smoke_compute.hlsl": sha256(
            executable.parent / "Shaders/3d_smoke_compute.hlsl"
        ),
    }
    return {
        "git_head": subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip(),
        "pilot_script_sha256": sha256(Path(__file__).resolve()),
        "runner_source_sha256": sources,
        "executable_path": str(executable),
        "executable_sha256": sha256(executable),
        "runtime_shader_sha256": runtime,
    }


def verify_artifact_manifest(run: Path, provenance: dict) -> None:
    artifacts = provenance.get("artifacts")
    if not isinstance(artifacts, dict) or "analysis.json" not in artifacts:
        raise RuntimeError(f"Run has no complete artifact manifest: {run}")
    run_resolved = run.resolve()
    for relative, record in artifacts.items():
        path = (run / relative).resolve()
        if path.parent != run_resolved and run_resolved not in path.parents:
            raise RuntimeError(f"Artifact path escapes its run root: {relative}")
        if not path.is_file():
            raise RuntimeError(f"Manifest artifact is missing: {path}")
        if path.stat().st_size != int(record["bytes"]) or sha256(path) != record["sha256"]:
            raise RuntimeError(f"Manifest artifact changed: {path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--timeout", type=int, default=7200)
    return parser.parse_args()


def validate_existing(
    run: Path,
    scene: str,
    resolution: int,
    iterations: int,
    baseline: dict,
) -> dict:
    required = ("analysis.json", "config.json", "provenance.json", "complete.txt")
    missing = [name for name in required if not (run / name).is_file()]
    if missing:
        raise RuntimeError(
            f"Existing run is incomplete and will not be overwritten: {run}; "
            f"missing {missing}"
        )
    config = json.loads((run / "config.json").read_text(encoding="utf-8"))
    expected = {
        "scene": scene,
        "resolution": resolution,
        "pressure_iterations": iterations,
        "total_steps": TOTAL_STEPS,
        "emitter_steps": EMITTER_STEPS,
    }
    observed = {name: config.get(name) for name in expected}
    if config.get("device_trace_enabled") is not True:
        raise RuntimeError(f"Pilot requires device tracing: {run}")
    if observed != expected:
        raise RuntimeError(
            f"Existing run configuration mismatch at {run}: "
            f"expected {expected}, observed {observed}"
        )
    analysis = json.loads((run / "analysis.json").read_text(encoding="utf-8"))
    if not analysis.get("harness_valid", False):
        raise RuntimeError(f"Existing run failed harness validation: {run}")
    provenance = json.loads((run / "provenance.json").read_text(encoding="utf-8"))
    expected_provenance_config = {
        "scene": scene,
        "resolution": resolution,
        "iterations": iterations,
        "steps": TOTAL_STEPS,
        "emitter_steps": EMITTER_STEPS,
        "dt_float32": DT_FLOAT32,
        "application_config_matches_request": True,
    }
    if provenance.get("complete") is not True:
        raise RuntimeError(f"Run provenance is not complete: {run}")
    if provenance.get("configuration") != expected_provenance_config:
        raise RuntimeError(f"Run provenance configuration mismatch: {run}")
    if provenance.get("git_head") != baseline["git_head"]:
        raise RuntimeError(f"Run git revision differs from the pilot baseline: {run}")
    if provenance.get("executable_sha256") != baseline["executable_sha256"]:
        raise RuntimeError(f"Run executable differs from the pilot baseline: {run}")
    if provenance.get("runtime_shader_sha256") != baseline["runtime_shader_sha256"]:
        raise RuntimeError(f"Run runtime shaders differ from the pilot baseline: {run}")
    if provenance.get("source_sha256") != baseline["runner_source_sha256"]:
        raise RuntimeError(f"Run source hashes differ from the pilot baseline: {run}")
    verify_artifact_manifest(run, provenance)
    return analysis


def compact_result(run: Path, analysis: dict) -> dict:
    return {
        "path": str(run),
        "analysis_sha256": sha256(run / "analysis.json"),
        "provenance_sha256": sha256(run / "provenance.json"),
        "harness_valid": bool(analysis["harness_valid"]),
        "primary_pass": bool(analysis["primary_pass"]),
        "tight_pass": bool(analysis["tight_pass"]),
        "failed_primary_step_count": len(analysis["failed_primary_steps"]),
        "first_failed_primary_step": (
            analysis["failed_primary_steps"][0]
            if analysis["failed_primary_steps"] else None
        ),
        "failed_tight_step_count": len(analysis["failed_tight_steps"]),
        "first_failed_tight_step": (
            analysis["failed_tight_steps"][0]
            if analysis["failed_tight_steps"] else None
        ),
        "worst_relative_residual": analysis["worst_relative_residual"],
        "worst_scaled_max_divergence": analysis["worst_scaled_max_divergence"],
    }


def main() -> None:
    args = parse_args()
    if os.environ.get("AQUA_SMOKE_APPEARANCE_DEVICE_TRACE", "1") == "0":
        raise RuntimeError("Device tracing must remain enabled for the pilot")
    if args.timeout < 1:
        raise ValueError("--timeout must be positive")

    repo = Path(__file__).resolve().parents[1]
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    runner = repo / "diagnostics/run_coupled_appearance.py"
    protocol = repo / "experiments/appearance-attribution/COUPLED_PRESSURE_CONTROLS.md"
    progress_path = root / "pilot-progress.json"
    executable = (
        args.executable
        or repo / "out/build/x64-Release/bin/Release/AquaTerrainDX12.exe"
    ).resolve()
    required_environment_paths = [
        *(repo / name for name in RUNNER_SOURCE_PATHS),
        executable,
        executable.parent / "Shaders/appearance_source_injection.hlsl",
        executable.parent / "Shaders/3d_smoke_compute.hlsl",
    ]
    missing = [str(path) for path in required_environment_paths if not path.is_file()]
    if missing:
        raise RuntimeError("Missing pilot input files:\n" + "\n".join(missing))
    baseline = environment_hashes(repo, executable)

    progress = {
        "protocol": "coupled pressure controls v1.0; short selection pilot",
        "protocol_path": str(protocol.relative_to(repo)),
        "status": "running",
        "scope": "Pressure selection only; no rendered or cross-resolution G1 output",
        "resolutions": list(RESOLUTIONS),
        "scenes": list(SCENES),
        "iteration_ladder": list(ITERATION_LADDER),
        "total_steps": TOTAL_STEPS,
        "emitter_steps": EMITTER_STEPS,
        "environment_sha256": baseline,
        "runs": {},
        "selections": {},
        "started_or_resumed_utc": datetime.now(timezone.utc).isoformat(),
    }
    if progress_path.exists():
        previous = json.loads(progress_path.read_text(encoding="utf-8"))
        for fixed in (
            "protocol", "resolutions", "scenes", "iteration_ladder",
            "total_steps", "emitter_steps", "environment_sha256",
        ):
            if previous.get(fixed) != progress[fixed]:
                raise RuntimeError(f"Existing pilot progress disagrees on {fixed}")
        progress["started_utc"] = previous.get(
            "started_utc", previous.get("started_or_resumed_utc")
        )
        progress["runs"] = previous.get("runs", {})
        progress["selections"] = previous.get("selections", {})
    else:
        progress["started_utc"] = progress["started_or_resumed_utc"]
    atomic_json(progress_path, progress)

    try:
        for resolution in RESOLUTIONS:
            primary = None
            tight = None
            for iterations in ITERATION_LADDER:
                scene_analyses = {}
                for scene in SCENES:
                    if environment_hashes(repo, executable) != baseline:
                        raise RuntimeError("Pilot executable or monitored source changed")
                    label = f"n{resolution:03d}-i{iterations:05d}-{scene}"
                    run = root / label
                    if run.exists():
                        print(f"resume {label}", flush=True)
                        analysis = validate_existing(
                            run, scene, resolution, iterations, baseline
                        )
                    else:
                        command = [
                            sys.executable,
                            str(runner),
                            "--output", str(run),
                            "--scene", scene,
                            "--resolution", str(resolution),
                            "--iterations", str(iterations),
                            "--steps", str(TOTAL_STEPS),
                            "--emitter-steps", str(EMITTER_STEPS),
                            "--timeout", str(args.timeout),
                        ]
                        command.extend(("--executable", str(executable)))
                        print(f"run {label}", flush=True)
                        run_environment = os.environ.copy()
                        run_environment["AQUA_SMOKE_APPEARANCE_DEVICE_TRACE"] = "1"
                        subprocess.run(command, cwd=repo, env=run_environment, check=True)
                        if environment_hashes(repo, executable) != baseline:
                            raise RuntimeError(
                                "Pilot executable or monitored source changed during a run"
                            )
                        analysis = validate_existing(
                            run, scene, resolution, iterations, baseline
                        )
                    scene_analyses[scene] = analysis
                    progress["runs"][label] = compact_result(run, analysis)
                    atomic_json(progress_path, progress)

                both_primary = all(scene_analyses[s]["primary_pass"] for s in SCENES)
                both_tight = all(scene_analyses[s]["tight_pass"] for s in SCENES)
                print(
                    f"decision n={resolution} i={iterations}: "
                    f"primary={both_primary} tight={both_tight}",
                    flush=True,
                )
                if primary is None and both_primary:
                    primary = iterations
                elif primary is not None and iterations > primary and both_tight:
                    tight = iterations
                    break

            progress["selections"][str(resolution)] = {
                "I_primary": primary,
                "I_tight": tight,
            }
            if primary is None or tight is None:
                progress["status"] = "blocked_at_iteration_cap"
                progress["blocked_resolution"] = resolution
                progress["completed_utc"] = datetime.now(timezone.utc).isoformat()
                atomic_json(progress_path, progress)
                print(json.dumps(progress["selections"], indent=2), flush=True)
                return
            atomic_json(progress_path, progress)

        progress["status"] = "selection_complete"
        progress["completed_utc"] = datetime.now(timezone.utc).isoformat()
        atomic_json(progress_path, progress)
        print(json.dumps(progress["selections"], indent=2), flush=True)
    except Exception as error:
        progress["status"] = "measurement_failure"
        progress["error"] = str(error)
        progress["failed_utc"] = datetime.now(timezone.utc).isoformat()
        atomic_json(progress_path, progress)
        raise


if __name__ == "__main__":
    main()
