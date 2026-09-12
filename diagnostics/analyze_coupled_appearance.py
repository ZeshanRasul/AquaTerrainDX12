"""Validate one coupled trajectory without revealing the G1 comparison."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

import numpy as np


PRIMARY_RATIO = 1e-4
PRIMARY_SCALED_MAX = 1e-5
TIGHT_RATIO = 1e-5
TIGHT_SCALED_MAX = 1e-6
SCALED_ZERO_RMS = 1e-10
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
UINT64_MASK = (1 << 64) - 1


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def key(row: dict[str, str], *names: str) -> str:
    for name in names:
        if name in row:
            return row[name]
    raise KeyError(f"None of {names!r} occur in columns {tuple(row)}")


def integer(row: dict[str, str], *names: str) -> int:
    return int(key(row, *names))


def number(row: dict[str, str], *names: str) -> float:
    return float(key(row, *names))


def optional_integer(row: dict[str, str], names: tuple[str, ...], default: int = 0) -> int:
    for name in names:
        if name in row and row[name] != "":
            return int(row[name])
    return default


def binary(row: dict[str, str], *names: str) -> bool:
    value = key(row, *names)
    if value not in ("0", "1"):
        raise ValueError(f"Expected a binary value in {names!r}, got {value!r}")
    return value == "1"


def fnv1a64(data: bytes | memoryview) -> int:
    value = FNV_OFFSET
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & UINT64_MASK
    return value


def close_stat(observed: float, expected: float) -> bool:
    return math.isclose(observed, expected, rel_tol=2e-12, abs_tol=2e-14)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    root = args.run.resolve()
    config = json.loads((root / "config.json").read_text(encoding="utf-8"))
    rows = read_csv(root / "steps.csv")

    total_steps = int(config.get("total_steps", config.get("steps")))
    resolution = int(config["resolution"])
    dt = float(config.get("dt", config.get("time_step", 1.0 / 60.0)))
    configured_iterations = int(config.get("pressure_iterations", config.get("iterations")))
    scene = str(config["scene"])
    expected_source_count = {"A": 1, "B": 2}.get(scene)
    target_source_rate = 0.002 * expected_source_count if expected_source_count else None
    configuration_identity = str(config.get("configuration_identity", ""))
    configuration_hash = str(config.get("configuration_fnv1a64", ""))
    registered_config_checks = {
        "offline_pressure_mode": config.get('offline_pressure_mode',0) in (0,1,2),
        "schema_version": config.get("schema_version") == 2,
        "scene": expected_source_count is not None,
        "resolution": resolution in (32, 64, 128),
        "grid_spacing": config.get("grid_spacing") == [1.0 / resolution] * 3,
        "origin": config.get("origin") == [-0.5, -0.5, -0.5],
        "dt": dt == float(np.float32(1.0 / 60.0)),
        "step_bounds": 1 <= total_steps <= 360,
        "emitter_bounds": 0 <= int(config["emitter_steps"]) <= total_steps,
        "snapshot_stride": config.get("snapshot_stride") == 12,
        "pressure_iterations": (configured_iterations == 0 if config.get('offline_pressure_mode',0) in (1,2) else 1 <= configured_iterations <= 65536),
        "advection": config.get("advection") == "maccormack",
        "limiter": config.get("limiter") == "clamp",
        "closed_domain": config.get("closed_domain") is True,
        "obstacle": config.get("obstacle") is False,
        "vorticity_confinement": config.get("vorticity_confinement") == 0,
        "density_dissipation": config.get("density_dissipation_per_s") == 0,
        "temperature_cooling": config.get("temperature_cooling_per_s") == 0.5,
        "ambient_temperature": config.get("ambient_temperature") == 0,
        "temperature_buoyancy": config.get("temperature_buoyancy") == 0.6,
        "smoke_weight": config.get("smoke_weight") == 0.05,
        "source_count": config.get("source_count") == expected_source_count,
        "target_source_rate": target_source_rate is not None
            and close_stat(float(config["target_integrated_source_rate"]), target_source_rate),
        "stored_source_rate": target_source_rate is not None
            and abs(float(config["stored_integrated_source_rate"]) - target_source_rate)
                / target_source_rate <= 1e-7,
        "source_shader_flags": config.get("source_shader_flags")
            == "strict|debug|skip_optimization",
        "solver_shader_flags": config.get("production_solver_shader_flags")
            == "strict|debug|skip_optimization",
        "instrumented_timing": config.get("instrumented_timing") is True,
        "debug_layer": config.get("d3d12_debug_layer_enabled") is True,
        "debug_errors": config.get("d3d12_error_count") == 0,
        "debug_discarded": config.get("d3d12_discarded_message_count") == 0,
        "configuration_identity": bool(configuration_identity),
        "configuration_hash": configuration_hash.isdecimal()
            and int(configuration_hash) == fnv1a64(configuration_identity.encode("utf-8")),
    }
    registered_config_valid = all(registered_config_checks.values())
    actual_steps = [integer(row, "step", "step_index") for row in rows]
    contiguous = actual_steps == list(range(1, total_steps + 1))

    decisions = []
    for row in rows:
        step = integer(row, "step", "step_index")
        emitted = binary(row, "emit", "emitter_enabled")
        iterations = integer(row, "pressure_iterations", "iterations")
        before_rms = number(row, "rms_divergence_before", "divergence_before_rms")
        before_max = number(row, "max_abs_divergence_before", "divergence_before_max")
        after_rms = number(row, "rms_divergence_after", "divergence_after_rms")
        after_max = number(row, "max_abs_divergence_after", "divergence_after_max")
        values = (before_rms, before_max, after_rms, after_max)
        nonfinite_counts = {
            "divergence_before": integer(
                row, "nonfinite_divergence_before", "divergence_before_nonfinite"
            ),
            "divergence_after": integer(
                row, "nonfinite_divergence_after", "divergence_after_nonfinite"
            ),
            "velocity": integer(row, "nonfinite_velocity", "velocity_nonfinite"),
            "density": integer(
                row, "nonfinite_density_cells", "density_nonfinite", "nonfinite"
            ),
        }
        density_min = number(row, "density_min")
        density_max = number(row, "density_max")
        density_sum = number(row, "density_sum")
        density_integral = number(row, "density_integral")
        motion_values = (
            number(row, "kinetic_energy"),
            number(row, "velocity_rms"),
            number(row, "velocity_max"),
        )
        centre_values = (
            number(row, "density_centre_x"),
            number(row, "density_centre_y"),
            number(row, "density_centre_z"),
        )
        counters_valid = all(value >= 0 for value in nonfinite_counts.values())
        finite = all(math.isfinite(value) and value >= 0 for value in values)
        finite = finite and counters_valid and all(
            value == 0 for value in nonfinite_counts.values()
        )
        finite = finite and all(
            math.isfinite(value) and value >= 0
            for value in (density_min, density_max, density_sum, density_integral, *motion_values)
        )
        finite = finite and density_max >= density_min
        finite = finite and all(math.isfinite(value) for value in centre_values)
        schedule_matches = emitted == (step <= int(config["emitter_steps"]))
        density_hash = key(row, "density_fnv1a64")
        row_configuration_hash = key(row, "configuration_fnv1a64")
        hashes_valid = (
            density_hash.isdecimal()
            and 0 <= int(density_hash) <= UINT64_MASK
            and row_configuration_hash == configuration_hash
        )
        scaled_before_rms = dt * before_rms
        scaled_after_rms = dt * after_rms
        scaled_after_max = dt * after_max
        zero_case = scaled_before_rms < SCALED_ZERO_RMS
        ratio = None if zero_case else after_rms / before_rms
        primary_residual = (
            scaled_after_rms <= SCALED_ZERO_RMS if zero_case else ratio <= PRIMARY_RATIO
        )
        tight_residual = (
            scaled_after_rms <= SCALED_ZERO_RMS if zero_case else ratio <= TIGHT_RATIO
        )
        iteration_match = iterations == configured_iterations
        primary = bool(
            finite and iteration_match and schedule_matches and hashes_valid
            and primary_residual and scaled_after_max <= PRIMARY_SCALED_MAX
        )
        tight = bool(
            finite and iteration_match and schedule_matches and hashes_valid
            and tight_residual and scaled_after_max <= TIGHT_SCALED_MAX
        )
        decisions.append(
            {
                "step": step,
                "emit": emitted,
                "iterations": iterations,
                "relative_residual": ratio,
                "relative_residual_applicable": not zero_case,
                "scaled_before_rms": scaled_before_rms,
                "scaled_after_rms": scaled_after_rms,
                "scaled_after_max": scaled_after_max,
                "finite": finite,
                "counters_valid": counters_valid,
                "iteration_match": iteration_match,
                "schedule_matches": schedule_matches,
                "hashes_valid": hashes_valid,
                "density_fnv1a64": density_hash,
                "primary_pass": primary,
                "tight_pass": tight,
                "nonfinite_counts": nonfinite_counts,
            }
        )

    source_rows = read_csv(root / "source-audit.csv")
    audit_candidates = ((1, int(config['emitter_steps']), int(config['emitter_steps'])+1)
                        if config.get('offline_pressure_mode',0) else (1,120,121))
    expected_audit_steps = sorted(set(step for step in audit_candidates if 1 <= step <= total_steps))
    observed_audit_steps = [integer(row, "step", "step_index") for row in source_rows]
    source_audit_pass = observed_audit_steps == expected_audit_steps
    source_audit_details = []
    source_rate = np.fromfile(root / "source-rate.f32", dtype="<f4")
    if source_rate.size != resolution**3:
        raise RuntimeError("source-rate.f32 has the wrong size")
    source_rate_hash_valid = (
        str(config.get("source_rate_fnv1a64", "")).isdecimal()
        and int(config["source_rate_fnv1a64"])
            == fnv1a64(source_rate.tobytes(order="C"))
    )
    source_rate = source_rate.reshape((resolution,) * 3)
    source_increment = (source_rate * np.float32(dt)).astype("<f4")
    emitter_steps = int(config["emitter_steps"])
    for row in source_rows:
        audit_step = integer(row, "step", "step_index")
        density_mismatch = integer(
            row, "density_mismatch_cells", "density_bitwise_mismatch_cells"
        )
        temperature_mismatch = integer(
            row, "temperature_mismatch_cells", "temperature_bitwise_mismatch_cells"
        )
        nonfinite = integer(row, "nonfinite", "nonfinite_cells")
        density_negative = integer(row, "density_negative_cells")
        temperature_negative = integer(row, "temperature_negative_cells")
        schedule_matches = binary(row, "schedule_matches")
        emitted = binary(row, "emit", "emitter_enabled")
        raw = {}
        raw_valid = True
        for field in ("density", "temperature"):
            for phase in ("before", "after"):
                path = root / "source-audit" / f"step-{audit_step:03d}-{field}-{phase}.f32"
                values = np.fromfile(path, dtype="<f4") if path.exists() else np.empty(0, dtype="<f4")
                valid = values.size == resolution**3
                raw_valid = raw_valid and valid
                raw[(field, phase)] = values.reshape((resolution,) * 3) if valid else values
        expected_density = (
            raw[("density", "before")] + source_increment
        ).astype("<f4") if emitted and raw_valid else raw[("density", "before")].copy()
        expected_temperature = (
            raw[("temperature", "before")] + source_increment
        ).astype("<f4") if emitted and raw_valid else raw[("temperature", "before")].copy()
        python_density_mismatch = int(np.count_nonzero(
            raw[("density", "after")].view("<u4") != expected_density.view("<u4")
        )) if raw_valid else -1
        python_temperature_mismatch = int(np.count_nonzero(
            raw[("temperature", "after")].view("<u4")
            != expected_temperature.view("<u4")
        )) if raw_valid else -1
        python_density_bitwise = bool(
            raw_valid and python_density_mismatch == 0
        )
        python_temperature_bitwise = bool(
            raw_valid and python_temperature_mismatch == 0
        )
        raw_finite_nonnegative = bool(
            raw_valid
            and all(np.isfinite(values).all() and (values >= 0).all() for values in raw.values())
        )
        passed_column = row.get("passed")
        if passed_column is None:
            raise KeyError("source-audit.csv is missing the required passed column")
        if passed_column not in ("0", "1"):
            raise ValueError("source-audit.csv passed must be binary")
        python_density_nonfinite = sum(
            int(np.count_nonzero(~np.isfinite(raw[("density", phase)])))
            for phase in ("before", "after")
        ) if raw_valid else -1
        python_temperature_nonfinite = sum(
            int(np.count_nonzero(~np.isfinite(raw[("temperature", phase)])))
            for phase in ("before", "after")
        ) if raw_valid else -1
        python_density_negative = sum(
            int(np.count_nonzero(raw[("density", phase)] < 0))
            for phase in ("before", "after")
        ) if raw_valid else -1
        python_temperature_negative = sum(
            int(np.count_nonzero(raw[("temperature", phase)] < 0))
            for phase in ("before", "after")
        ) if raw_valid else -1
        audit_hash_arrays = {
            "density_pre_fnv1a64": raw[("density", "before")],
            "density_post_fnv1a64": raw[("density", "after")],
            "density_expected_fnv1a64": expected_density,
            "temperature_pre_fnv1a64": raw[("temperature", "before")],
            "temperature_post_fnv1a64": raw[("temperature", "after")],
            "temperature_expected_fnv1a64": expected_temperature,
        }
        audit_hashes_valid = raw_valid and all(
            key(row, name).isdecimal()
            and int(key(row, name)) == fnv1a64(values.tobytes(order="C"))
            for name, values in audit_hash_arrays.items()
        )
        counts_match_raw = bool(
            raw_valid
            and density_mismatch == python_density_mismatch
            and temperature_mismatch == python_temperature_mismatch
            and integer(row, "density_nonfinite_cells") == python_density_nonfinite
            and integer(row, "temperature_nonfinite_cells") == python_temperature_nonfinite
            and density_negative == python_density_negative
            and temperature_negative == python_temperature_negative
            and nonfinite == python_density_nonfinite + python_temperature_nonfinite
            and integer(row, "density_pre_zero_cells")
                == int(np.count_nonzero(raw[("density", "before")] == 0))
            and integer(row, "temperature_pre_zero_cells")
                == int(np.count_nonzero(raw[("temperature", "before")] == 0))
        )
        passed = bool(
            density_mismatch == 0
            and temperature_mismatch == 0
            and nonfinite == 0
            and density_negative == 0
            and temperature_negative == 0
            and schedule_matches
            and emitted == (audit_step <= emitter_steps)
            and python_density_bitwise
            and python_temperature_bitwise
            and raw_finite_nonnegative
            and audit_hashes_valid
            and counts_match_raw
            and passed_column == "1"
        )
        source_audit_pass = source_audit_pass and passed
        source_audit_details.append(
            {
                "step": audit_step,
                "emit": emitted,
                "density_mismatch_cells": density_mismatch,
                "temperature_mismatch_cells": temperature_mismatch,
                "nonfinite": nonfinite,
                "density_negative_cells": density_negative,
                "temperature_negative_cells": temperature_negative,
                "schedule_matches": schedule_matches,
                "python_density_bitwise": python_density_bitwise,
                "python_temperature_bitwise": python_temperature_bitwise,
                "python_density_mismatch_cells": python_density_mismatch,
                "python_temperature_mismatch_cells": python_temperature_mismatch,
                "raw_finite_nonnegative": raw_finite_nonnegative,
                "audit_hashes_valid": audit_hashes_valid,
                "counts_match_raw": counts_match_raw,
                "passed": passed,
            }
        )

    expected_snapshot_steps = {0, total_steps}
    expected_snapshot_steps.update(range(12, total_steps + 1, 12))
    snapshots = []
    snapshots_valid = True
    snapshot_integrity_valid = True
    expected_bytes = resolution**3 * np.dtype("<f4").itemsize
    snapshot_rows = read_csv(root / "snapshots.csv")
    observed_snapshot_steps = [
        integer(row, "step", "step_index") for row in snapshot_rows
    ]
    expected_snapshot_sequence = sorted(expected_snapshot_steps)
    snapshot_row_by_step = {
        integer(row, "step", "step_index"): row for row in snapshot_rows
    }
    exact_snapshot_sequence = observed_snapshot_steps == expected_snapshot_sequence
    snapshots_valid = exact_snapshot_sequence
    snapshot_integrity_valid = exact_snapshot_sequence
    step_row_by_step = {
        integer(row, "step", "step_index"): row for row in rows
    }
    for step in expected_snapshot_sequence:
        path = root / "snapshots" / f"density-step-{step:03d}.f32"
        metadata = snapshot_row_by_step.get(step)
        exists = path.exists()
        correct_size = exists and path.stat().st_size == expected_bytes
        finite = False
        nonnegative = False
        metadata_hash_valid = False
        metadata_stats_valid = False
        step_hash_valid = step == 0
        if correct_size:
            field = np.fromfile(path, dtype="<f4")
            finite = bool(np.isfinite(field).all())
            nonnegative = bool((field >= 0).all())
            field_hash = fnv1a64(field.tobytes(order="C"))
            finite_values = field[np.isfinite(field)].astype(np.float64)
            field_sum = float(finite_values.sum(dtype=np.float64))
            field_min = float(finite_values.min()) if finite_values.size else 0.0
            field_max = float(finite_values.max()) if finite_values.size else 0.0
            field_nonfinite = int(np.count_nonzero(~np.isfinite(field)))
            field_negative = int(np.count_nonzero(field < 0))
            metadata_hash_valid = bool(
                metadata is not None
                and key(metadata, "fnv1a64").isdecimal()
                and int(key(metadata, "fnv1a64")) == field_hash
            )
            metadata_stats_valid = bool(
                metadata is not None
                and close_stat(number(metadata, "simulation_time_s"), step * dt)
                and close_stat(number(metadata, "density_sum"), field_sum)
                and close_stat(
                    number(metadata, "density_integral"),
                    field_sum / resolution**3,
                )
                and close_stat(number(metadata, "density_min"), field_min)
                and close_stat(number(metadata, "density_max"), field_max)
                and integer(metadata, "nonfinite") == field_nonfinite
                and integer(metadata, "negative") == field_negative
            )
            if step > 0 and step in step_row_by_step:
                step_hash_valid = (
                    key(step_row_by_step[step], "density_fnv1a64").isdecimal()
                    and int(key(step_row_by_step[step], "density_fnv1a64")) == field_hash
                )
        metadata_path_valid = bool(
            metadata is not None
            and Path(key(metadata, "file")).as_posix()
                == f"snapshots/density-step-{step:03d}.f32"
        )
        integrity_valid = bool(
            exists and correct_size and metadata_path_valid
            and metadata_hash_valid and metadata_stats_valid and step_hash_valid
        )
        valid = bool(integrity_valid and finite and nonnegative)
        snapshot_integrity_valid = snapshot_integrity_valid and integrity_valid
        snapshots_valid = snapshots_valid and valid
        snapshots.append(
            {
                "step": step,
                "path": str(path.relative_to(root)),
                "exists": exists,
                "correct_size": correct_size,
                "finite": finite,
                "nonnegative": nonnegative,
                "metadata_path_valid": metadata_path_valid,
                "metadata_hash_valid": metadata_hash_valid,
                "metadata_stats_valid": metadata_stats_valid,
                "step_hash_valid": step_hash_valid,
                "integrity_valid": integrity_valid,
                "sha256": sha256(path) if exists else None,
                "valid": valid,
            }
        )

    failed_primary = [item["step"] for item in decisions if not item["primary_pass"]]
    failed_tight = [item["step"] for item in decisions if not item["tight_pass"]]
    applicable_ratios = [
        item["relative_residual"]
        for item in decisions
        if item["relative_residual_applicable"] and item["relative_residual"] is not None
    ]
    step_rows_valid = bool(
        contiguous
        and len(rows) == total_steps
        and all(
            item["iteration_match"]
            and item["schedule_matches"]
            and item["hashes_valid"]
            and item["counters_valid"]
            for item in decisions
        )
    )
    debug_messages_path = root / "debug-messages.txt"
    debug_status_path = root / "debug-status.txt"
    phase_path = root / "phase.txt"
    host_exit_path = root / "host-exit.txt"
    lifecycle_valid = bool(
        phase_path.is_file()
        and phase_path.read_text(encoding="utf-8").splitlines()
            == [
                "configured",
                "step-1-source-command-recorded",
                "step-1-readback-complete",
            ]
        and host_exit_path.is_file()
        and host_exit_path.read_text(encoding="utf-8").strip() == "normal_return=0"
    )
    debug_validation_valid = bool(
        debug_messages_path.is_file()
        and debug_status_path.is_file()
        and config.get("d3d12_debug_layer_enabled") is True
        and config.get("d3d12_error_count") == 0
        and config.get("d3d12_discarded_message_count") == 0
        and "enabled" in debug_status_path.read_text(encoding="utf-8")
    )
    harness_valid = bool(
        registered_config_valid
        and step_rows_valid
        and int(config.get("recorded_steps", -1)) == total_steps
        and int(config.get("validation_failures", -1)) == 0
        and (root / "complete.txt").read_text(encoding="utf-8").splitlines()[0] == "PASS"
        and source_rate_hash_valid
        and source_audit_pass
        and snapshot_integrity_valid
        and debug_validation_valid
        and lifecycle_valid
    )
    analysis = {
        "protocol": ("offline reference v1; shared source and scalar pressure validation" if config.get('offline_pressure_mode',0) else "coupled pressure controls v1.0; single-run validation"),
        "run": str(root),
        "scene": scene,
        "resolution": resolution,
        "pressure_iterations": configured_iterations,
        "total_steps": total_steps,
        "recorded_steps": len(rows),
        "steps_contiguous": contiguous,
        "step_rows_valid": step_rows_valid,
        "registered_config_valid": registered_config_valid,
        "registered_config_checks": registered_config_checks,
        "configuration_fnv1a64": configuration_hash,
        "source_rate_hash_valid": source_rate_hash_valid,
        "debug_validation_valid": debug_validation_valid,
        "lifecycle_valid": lifecycle_valid,
        "d3d12_message_count": config.get("d3d12_message_count"),
        "d3d12_error_count": config.get("d3d12_error_count"),
        "d3d12_discarded_message_count": config.get(
            "d3d12_discarded_message_count"
        ),
        "source_audit_pass": bool(source_audit_pass),
        "source_audit_steps_expected": expected_audit_steps,
        "source_audit_steps_observed": observed_audit_steps,
        "source_audit": source_audit_details,
        "snapshots_valid": bool(snapshots_valid),
        "snapshot_integrity_valid": bool(snapshot_integrity_valid),
        "snapshot_steps_expected": expected_snapshot_sequence,
        "snapshot_steps_observed": observed_snapshot_steps,
        "snapshots": snapshots,
        "harness_valid": harness_valid,
        "primary_pass": bool(harness_valid and not failed_primary),
        "tight_pass": bool(harness_valid and not failed_tight),
        "failed_primary_steps": failed_primary,
        "failed_tight_steps": failed_tight,
        "worst_relative_residual": max(applicable_ratios, default=None),
        "worst_scaled_max_divergence": max(
            (item["scaled_after_max"] for item in decisions), default=None
        ),
        "numerical_zero_steps": sum(
            not item["relative_residual_applicable"] for item in decisions
        ),
        "thresholds": {
            "scaled_zero_rms": SCALED_ZERO_RMS,
            "primary_relative_residual": PRIMARY_RATIO,
            "primary_scaled_max_divergence": PRIMARY_SCALED_MAX,
            "tight_relative_residual": TIGHT_RATIO,
            "tight_scaled_max_divergence": TIGHT_SCALED_MAX,
        },
        "decisions": decisions,
        "scope": "Pressure/source/snapshot validity for one run; no G1 comparison",
    }
    (root / "analysis.json").write_text(json.dumps(analysis, indent=2) + "\n", encoding="utf-8")
    printed = {
        key: value
        for key, value in analysis.items()
        if key not in (
            "decisions", "snapshots", "source_audit",
            "failed_primary_steps", "failed_tight_steps",
            "registered_config_checks", "snapshot_steps_expected",
            "snapshot_steps_observed",
        )
    }
    printed["failed_primary_step_count"] = len(failed_primary)
    printed["first_failed_primary_step"] = failed_primary[0] if failed_primary else None
    printed["failed_tight_step_count"] = len(failed_tight)
    printed["first_failed_tight_step"] = failed_tight[0] if failed_tight else None
    print(json.dumps(printed, indent=2))


if __name__ == "__main__":
    main()
