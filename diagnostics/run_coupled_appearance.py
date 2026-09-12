"""Run one deterministic coupled appearance/pressure-control trajectory.

This runner generates the registered normalized source, launches the gated
production solver path, and seals the raw output with source/build hashes.  It
does not render or calculate the cross-resolution G1 result.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

import numpy as np

from appearance_source import sphere_rate


SCENES = {
    "A": [(0.5, 0.15, 0.5)],
    "B": [(0.35, 0.15, 0.5), (0.65, 0.15, 0.5)],
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_head(repo: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scene", required=True, choices=tuple(SCENES))
    parser.add_argument("--resolution", required=True, type=int, choices=(32, 64, 128))
    parser.add_argument("--iterations", required=True, type=int)
    parser.add_argument("--steps", type=int, default=360)
    parser.add_argument("--emitter-steps", type=int, default=120)
    parser.add_argument("--timeout", type=int, default=7200, help="Process timeout in seconds")
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--pressure-triage-step", type=int, default=0)
    parser.add_argument("--offline-pressure", choices=('off','pressure32','velocity64'), default='off')
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if (args.offline_pressure == 'off' and args.iterations < 1) or (args.offline_pressure != 'off' and args.iterations != 0):
        raise ValueError("Offline reference requires zero iterations; production requires positive iterations")
    if args.steps < 1 or not 0 <= args.emitter_steps <= args.steps:
        raise ValueError("Require steps >= 1 and 0 <= emitter-steps <= steps")
    if not 0 <= args.pressure_triage_step <= args.steps:
        raise ValueError("Pressure triage step must be within the run")
    if args.pressure_triage_step and args.iterations != 65536:
        raise ValueError("Pressure triage requires 65536 iterations")

    repo = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    executable = (args.executable or
                  repo / "out/build/x64-Release/bin/Release/AquaTerrainDX12.exe").resolve()
    runtime_shader = executable.parent / "Shaders/appearance_source_injection.hlsl"
    runtime_solver_shader = executable.parent / "Shaders/3d_smoke_compute.hlsl"
    source_shader = repo / "src/Shaders/appearance_source_injection.hlsl"
    source_solver_shader = repo / "src/Shaders/3d_smoke_compute.hlsl"
    analyzer = repo / "diagnostics/analyze_coupled_appearance.py"

    monitored = [
        Path(__file__).resolve(),
        analyzer,
        repo / "diagnostics/appearance_source.py",
        repo / "src/Renderer/Renderer.cpp",
        repo / "src/Renderer/Renderer.h",
        repo / "src/Renderer/SmokeGpuDiagnostics.cpp",
        repo / "src/Renderer/SmokeAppearanceExperiment.cpp",
        repo / "src/Renderer/SmokeOfflineReference.cpp",
        repo / "src/Renderer/OfflineResponseMarker.h",
        repo / "experiments/appearance-attribution/MARKER_ACCESS_SPEC.md",
        repo / "diagnostics/offline_reference_worker.py",
        repo / "diagnostics/analyze_pressure_triage.py",
        repo / "diagnostics/analyze_offline_reference.py",
        repo / "experiments/appearance-attribution/ROUNDING_IDENTITY_AMENDMENT.md",
        repo / "experiments/appearance-attribution/FULL_DURATION_REFERENCE_SPEC.md",
        repo / "experiments/appearance-attribution/OFFLINE_REFERENCE_SPEC.md",
        source_solver_shader,
        source_shader,
        repo / "src/Window.cpp",
        repo / "src/WinMain.cpp",
        repo / "experiments/appearance-attribution/SPEC.md",
        repo / "experiments/appearance-attribution/SOURCE_INJECTION_CONTROLS.md",
        repo / "experiments/appearance-attribution/COUPLED_PRESSURE_CONTROLS.md",
        repo / "experiments/appearance-attribution/PRESSURE_TRIAGE_SPEC.md",
        repo / "RESEARCH_PLAN.md",
        repo / "CMakeLists.txt",
    ]
    missing = [
        str(path)
        for path in [executable, runtime_shader, runtime_solver_shader, *monitored]
        if not path.exists()
    ]
    if missing:
        raise RuntimeError("Missing required build/source files:\n" + "\n".join(missing))
    compiled_sources = [p for p in monitored if p.suffix.lower() in (".cpp", ".h")]
    if any(path.stat().st_mtime > executable.stat().st_mtime for path in compiled_sources):
        raise RuntimeError("Main executable is stale; reconfigure/build Release before running")
    if sha256(runtime_shader) != sha256(source_shader):
        raise RuntimeError("Runtime appearance source shader is stale; rebuild Release")
    if sha256(runtime_solver_shader) != sha256(source_solver_shader):
        raise RuntimeError("Runtime coupled solver shader is stale; rebuild Release")
    if output.exists():
        raise FileExistsError(f"Output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)

    start_hashes = {str(path.relative_to(repo)): sha256(path) for path in monitored}
    executable_hash = sha256(executable)
    dt = np.float32(1.0 / 60.0)
    n = args.resolution
    rate = np.zeros((n, n, n), dtype="<f4")
    source_records = []
    for centre in SCENES[args.scene]:
        source, record = sphere_rate(n, centre, quadrature=16)
        rate = (rate + source).astype("<f4")
        source_records.append(record)
    if not np.isfinite(rate).all() or np.any(rate < 0):
        raise RuntimeError("Generated source table is not finite and nonnegative")
    stored_integral = float(rate.sum(dtype=np.float64) / n**3)
    target_integral = 0.002 * len(SCENES[args.scene])
    relative_error = abs(stored_integral - target_integral) / target_integral
    if relative_error > 1e-7:
        raise RuntimeError(f"Stored source integral failed: relative error {relative_error:.17g}")

    with tempfile.TemporaryDirectory(prefix="aqua-appearance-source-") as temp_name:
        source_path = Path(temp_name) / "source-rate.f32"
        rate.tofile(source_path)
        source_hash = sha256(source_path)

        environment = os.environ.copy()
        for name in (
            "AQUA_SMOKE_AUDIT",
            "AQUA_SMOKE_REFERENCE",
            "AQUA_SMOKE_REFERENCE_ADVECTION",
            "AQUA_SMOKE_REFERENCE_OUTPUT",
        ):
            environment.pop(name, None)
        environment.update(
            AQUA_SMOKE_APPEARANCE="1",
            AQUA_SMOKE_APPEARANCE_SCENE=args.scene,
            AQUA_SMOKE_APPEARANCE_OUTPUT=str(output),
            AQUA_SMOKE_APPEARANCE_SOURCE=str(source_path),
            AQUA_SMOKE_APPEARANCE_ITERATIONS=str(args.iterations),
            AQUA_SMOKE_APPEARANCE_STEPS=str(args.steps),
            AQUA_SMOKE_APPEARANCE_EMITTER_STEPS=str(args.emitter_steps),
            AQUA_SMOKE_RESOLUTION=str(n),
            AQUA_SMOKE_PRESSURE_TRIAGE_STEP=str(args.pressure_triage_step),
            AQUA_SMOKE_OFFLINE_PRESSURE=str(('off','pressure32','velocity64').index(args.offline_pressure)),
        )
        diagnostic_logs = (
            executable.parent / "smoke-automation-error.txt",
            executable.parent / "smoke-audit-error.txt",
            executable.parent / "smoke-automation-exit.txt",
        )
        for path in diagnostic_logs:
            path.unlink(missing_ok=True)
        command = [str(executable)]
        stop_worker = threading.Event()
        worker = None
        if args.offline_pressure != 'off':
            from offline_reference_worker import serve
            worker = threading.Thread(target=serve, args=(output,stop_worker), daemon=True)
            worker.start()
        try:
            result = subprocess.run(
                command,
                cwd=executable.parent,
                env=environment,
                capture_output=True,
                text=True,
                timeout=args.timeout,
            )
        except subprocess.TimeoutExpired as error:
            failure_log = output.with_name(output.name + ".timeout.log")
            failure_log.write_text((error.stdout or "") + (error.stderr or ""), encoding="utf-8")
            raise RuntimeError(f"Coupled run timed out; output saved at {failure_log}") from error
        finally:
            stop_worker.set()
            if worker is not None: worker.join(timeout=10)
        process_text = result.stdout + result.stderr
        if result.returncode != 0:
            failure_log = output.with_name(output.name + ".failed.log")
            failure_log.write_text(process_text, encoding="utf-8")
            details = ""
            for path in diagnostic_logs:
                if path.exists():
                    details += f"\n{path.name}: {path.read_text(errors='replace')}"
            raise RuntimeError(
                f"Coupled run exited {result.returncode}; output saved at {failure_log}{details}"
            )

        exit_log = executable.parent / "smoke-automation-exit.txt"
        exit_text = exit_log.read_text(encoding="utf-8") if exit_log.exists() else ""
        if exit_text.strip() != "normal_return=0":
            raise RuntimeError(
                "Coupled process did not record a clean normal return: "
                f"{exit_text.strip() or 'missing exit record'}"
            )
        (output / "host-exit.txt").write_text(exit_text, encoding="utf-8")

        required = [
            output / "config.json",
            output / "steps.csv",
            output / "source-audit.csv",
            output / "complete.txt",
            output / "source-rate.f32",
            output / "snapshots/density-step-000.f32",
            output / "debug-messages.txt",
            output / "debug-status.txt",
            output / "phase.txt",
            output / "host-exit.txt",
        ]
        absent = [str(path) for path in required if not path.exists()]
        if absent:
            raise RuntimeError("Run returned success but is incomplete:\n" + "\n".join(absent))
        if (output / "source-rate.f32").stat().st_size != rate.nbytes:
            raise RuntimeError("Copied source table has the wrong size")
        if sha256(output / "source-rate.f32") != source_hash:
            raise RuntimeError("Copied source table differs from generated input")
        app_config = json.loads((output / "config.json").read_text(encoding="utf-8"))
        requested_config = {
            "scene": args.scene,
            "resolution": n,
            "pressure_iterations": args.iterations,
            "total_steps": args.steps,
            "emitter_steps": args.emitter_steps,
            "offline_pressure_mode": ('off','pressure32','velocity64').index(args.offline_pressure),
        }
        observed_config = {
            name: app_config.get(name) for name in requested_config
        }
        if observed_config != requested_config:
            raise RuntimeError(
                "Application configuration differs from runner request: "
                f"expected {requested_config}, observed {observed_config}"
            )
        (output / "process.log").write_text(process_text, encoding="utf-8")

    source_generation = {
        "scene": args.scene,
        "resolution": n,
        "array_order": "z,y,x; x contiguous",
        "dtype": "little-endian float32",
        "quadrature": 16,
        "centres": [list(c) for c in SCENES[args.scene]],
        "source_records": source_records,
        "stored_integral_rate": stored_integral,
        "target_integral_rate": target_integral,
        "relative_integral_error": relative_error,
        "source_rate_sha256": source_hash,
    }
    (output / "source-generation.json").write_text(
        json.dumps(source_generation, indent=2) + "\n", encoding="utf-8"
    )

    analyzer_command = [sys.executable, str(analyzer), str(output)]
    analyzer_result = subprocess.run(
        analyzer_command,
        cwd=repo,
        capture_output=True,
        text=True,
    )
    analyzer_text = analyzer_result.stdout + analyzer_result.stderr
    (output / "analyzer.log").write_text(analyzer_text, encoding="utf-8")
    if analyzer_result.returncode != 0:
        raise RuntimeError(
            f"Coupled analyzer exited {analyzer_result.returncode}; see {output / 'analyzer.log'}"
        )
    analysis = json.loads((output / "analysis.json").read_text(encoding="utf-8"))
    if not analysis.get("harness_valid", False):
        raise RuntimeError(f"Coupled run failed harness validation; see {output / 'analysis.json'}")

    end_hashes = {str(path.relative_to(repo)): sha256(path) for path in monitored}
    if end_hashes != start_hashes or sha256(executable) != executable_hash:
        raise RuntimeError("A monitored source or executable changed during the run")

    artifacts = {}
    for path in sorted(p for p in output.rglob("*") if p.is_file()):
        if path.name == "provenance.json":
            continue
        artifacts[str(path.relative_to(output))] = {
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
        }
    provenance = {
        "protocol": "coupled appearance bridge / pressure controls v1.0",
        "complete": True,
        "command": command,
        "analyzer_command": analyzer_command,
        "configuration": {
            "scene": args.scene,
            "resolution": n,
            "iterations": args.iterations,
            "steps": args.steps,
            "emitter_steps": args.emitter_steps,
            "dt_float32": float(dt),
            "application_config_matches_request": True,
        },
        "git_head": git_head(repo),
        "executable_sha256": executable_hash,
        "runtime_shader_sha256": {
            "appearance_source_injection.hlsl": sha256(runtime_shader),
            "3d_smoke_compute.hlsl": sha256(runtime_solver_shader),
        },
        "source_sha256": start_hashes,
        "artifacts": artifacts,
        "scope": "One coupled raw trajectory; no rendering or cross-resolution G1 calculation",
    }
    (output / "provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps({
        "output": str(output),
        "scene": args.scene,
        "resolution": n,
        "iterations": args.iterations,
        "steps": args.steps,
        "source_relative_integral_error": relative_error,
        "artifact_count": len(artifacts),
    }, indent=2))


if __name__ == "__main__":
    main()
