"""Same-state closed-box pressure diagnostics; no G1 or performance scoring."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path

import numpy as np


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rms(x):
    return float(np.sqrt(np.mean(np.square(x, dtype=np.float64))))


def laplace(p):
    n = p.shape[0]
    out = np.zeros_like(p, dtype=np.float64)
    for axis in range(3):
        lo, hi = [slice(None)] * 3, [slice(None)] * 3
        lo[axis], hi[axis] = slice(None, -1), slice(1, None)
        lo, hi = tuple(lo), tuple(hi)
        d = (p[hi] - p[lo]) * n**2
        out[lo] += d
        out[hi] -= d
    return out


def solve_box(rhs):
    """Orthogonal cosine basis diagonalizes the cell-centred Neumann stencil.

    Explicit matrix transforms avoid an additional SciPy dependency. This is an
    offline diagnostic, with no claim of performance or arbitrary-geometry support.
    The constant RHS component is returned, never silently discarded.
    """
    n = rhs.shape[0]
    k = np.arange(n, dtype=np.float64)
    q = np.sqrt(2.0 / n) * np.cos(np.pi * k[:, None] * (k[None, :] + .5) / n)
    q[0] /= np.sqrt(2.0)
    mean = float(np.mean(rhs))
    transformed = rhs - mean
    for axis in range(3):
        transformed = np.moveaxis(np.moveaxis(transformed, axis, -1) @ q.T, -1, axis)
    eigen = -4 * n**2 * np.sin(np.pi * k / (2 * n))**2
    denom = eigen[:, None, None] + eigen[None, :, None] + eigen[None, None, :]
    denom[0, 0, 0] = 1
    transformed /= denom
    transformed[0, 0, 0] = 0
    for axis in range(3):
        transformed = np.moveaxis(np.moveaxis(transformed, axis, -1) @ q, -1, axis)
    return transformed, mean


def divergence(velocity):
    n = velocity[0].shape[0]
    return n * sum(np.diff(v.astype(np.float64), axis=axis)
                   for v, axis in zip(velocity, (2, 1, 0)))


def project(velocity, pressure, dt, arithmetic32=False, store32=False):
    n = pressure.shape[0]
    dtype = np.float32 if arithmetic32 else np.float64
    p = pressure.astype(dtype)
    result = []
    for v, axis in zip(velocity, (2, 1, 0)):
        out = v.astype(dtype).copy()
        interior = [slice(None)] * 3
        interior[axis] = slice(1, -1)
        correction = (dtype(dt) * np.diff(p, axis=axis)) / dtype(1.0 / n)
        out[tuple(interior)] -= correction
        result.append(out.astype(np.float32) if store32 else out)
    return result


def controls():
    rng = np.random.default_rng(128)
    results = []
    for n in (4, 8, 32):
        x = (np.arange(n) + .5) / n
        for kind, p in [('constant', np.ones((n, n, n))),
                        ('mode', np.broadcast_to(np.cos(np.pi*x), (n, n, n)).copy()),
                        ('random', rng.normal(size=(n, n, n)))]:
            b = laplace(p)
            solved, mean = solve_box(b)
            error = rms(solved - (p - p.mean()))
            residual = rms(laplace(solved) - b) / max(rms(b), 1)
            assert error < 1e-10 and residual < 1e-10
            results.append(dict(n=n, kind=kind, pressure_rms_error=error,
                                relative_residual=residual, rhs_mean=mean))
    return results


def analyze(run, original):
    config = json.loads((run / 'config.json').read_text())
    valid = json.loads((run / 'analysis.json').read_text())
    assert valid['harness_valid']
    manifest = json.loads((run / 'provenance.json').read_text())
    # Verify every sealed run artifact, including the newly captured fields.
    for name, record in manifest['artifacts'].items():
        assert sha(run / name) == record['sha256'], name
    cap = run / 'pressure-triage'
    meta = json.loads((cap / 'capture.json').read_text())
    n, step = valid['resolution'], meta['step']
    dt = float(np.float32(config['dt']))
    assert meta['iterations'] == 65536 and meta['field_count'] == 16
    fields = {}
    for file in cap.glob('*.f32'):
        key = file.stem
        shape = ((n, n, n+1) if key.startswith('u_') else
                 (n, n+1, n) if key.startswith('v_') else
                 (n+1, n, n) if key.startswith('w_') else (n, n, n))
        a = np.fromfile(file, dtype='<f4')
        assert a.size == np.prod(shape) and np.isfinite(a).all(), key
        fields[key] = a.reshape(shape).astype(np.float64)
    assert len(fields) == 16 and np.count_nonzero(fields['p00000']) == 0
    before = [fields[k+'_before'] for k in 'uvw']
    after = [fields[k+'_after'] for k in 'uvw']
    for velocities in (before, after):
        for v, axis in zip(velocities, (2, 1, 0)):
            assert np.all(np.take(v, (0, n), axis=axis) == 0)
    db, da = fields['div_before'], fields['div_after']
    exact_db, exact_da = divergence(before), divergence(after)
    rhs = db / dt
    rhs_gpu = (np.float32(1) / np.float32(dt) * db.astype(np.float32)).astype(np.float64)
    norm = rms(rhs)
    checkpoints = []
    for k in (0, 256, 1024, 4096, 8192, 16384, 32768, 65536):
        p = fields[f'p{k:05d}']
        checkpoints.append(dict(iterations=k,
            relative_residual=rms(rhs-laplace(p))/norm,
            relative_residual_rounded_rhs=rms(rhs_gpu-laplace(p))/rms(rhs_gpu)))
    p64, mean = solve_box(rhs)
    solver_res = rms(rhs - mean - laplace(p64)) / norm
    assert solver_res < 1e-9, solver_res
    emulated = project(before, fields['p65536'], dt, arithmetic32=True)
    projection32_matches = all(np.array_equal(a, b) for a, b in zip(emulated, after))
    projection32_error = max(float(np.max(np.abs(a-b))) for a, b in zip(emulated, after))
    assert projection32_matches, 'Float32 projection replay does not match captured GPU'
    variants = {}
    for name, vel in [
        ('captured_gpu', after),
        ('captured_pressure_project64', project(before, fields['p65536'], dt)),
        ('reference64_project64', project(before, p64, dt)),
        ('reference64_project64_store_velocity32', project(before, p64, dt, store32=True)),
        ('reference_pressure32_project64', project(before, p64.astype(np.float32), dt)),
        ('reference_pressure32_project32', project(before, p64, dt, arithmetic32=True)),
        ('captured_pressure_project32', project(before, fields['p65536'], dt, arithmetic32=True)),
    ]:
        div = divergence(vel)
        variants[name] = dict(relative_divergence=rms(div)/rms(db),
                              scaled_max_divergence=float(np.max(np.abs(div)))*dt)
    identity = exact_da - (db - dt * laplace(fields['p65536']))
    assert rms(exact_db-db) < 1e-5 and rms(exact_da-da) < 1e-5
    assert rms(identity) < 2e-5
    rows = list(csv.DictReader((run/'steps.csv').open()))
    old_rows = list(csv.DictReader((original/'steps.csv').open()))
    keys = [k for k in rows[0] if not k.endswith('_ms') and k != 'configuration_fnv1a64']
    assert len(rows) <= len(old_rows)
    assert all(r[k] == old[k] for r, old in zip(rows, old_rows) for k in keys), 'trajectory changed'
    matched = []
    for file in (run/'snapshots').glob('*.f32'):
        old = original/'snapshots'/file.name
        if old.exists():
            assert sha(file) == sha(old), file.name
            matched.append(file.name)
    row = rows[step-1]
    assert abs(rms(da)-float(row['rms_divergence_after'])) <= max(1e-10/dt, 2e-4*rms(da))
    assert float(np.max(np.abs(da))) == float(row['max_abs_divergence_after'])
    ratio_difference = abs(checkpoints[-1]['relative_residual']-float(row['relative_residual']))
    assert ratio_difference < 5e-7, 'Residual/divergence ratio disagreement'
    p_error = fields['p65536'] - p64
    p_error -= p_error.mean()
    return dict(run=str(run), scene=valid['scene'], resolution=n, step=step,
        harness_valid=True, provenance_valid=True, prefix_rows_identical=len(rows),
        matching_snapshots=matched, rhs_rms=norm, rhs_mean=mean,
        incompatible_rhs_ratio=abs(mean)/norm, rounded_rhs_difference_ratio=rms(rhs-rhs_gpu)/norm,
        solver64_relative_residual=solver_res, checkpoints=checkpoints,
        pre_divergence_reconstruction_rms=rms(exact_db-db),
        post_divergence_reconstruction_rms=rms(exact_da-da),
        identity_rms=rms(identity), identity_relative_to_pre_divergence=rms(identity)/rms(db),
        captured_compact_ratio=float(row['relative_residual']),
        direct_residual_vs_compact_ratio_difference=ratio_difference,
        float32_projection_replay_bitwise=projection32_matches,
        float32_projection_replay_max_error=projection32_error,
        gauge_free_pressure_error_rms=rms(p_error), variants=variants,
        capture_sha256={p.name:sha(p) for p in sorted(cap.iterdir()) if p.is_file()})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    tests = controls()
    runs = []
    for run in sorted(args.root.iterdir()):
        if not (run/'pressure-triage/capture.json').exists():
            continue
        v = json.loads((run/'analysis.json').read_text())
        old = args.original/f"n{v['resolution']:03d}-i65536-{v['scene']}"
        runs.append(analyze(run, old))
    assert {(r['resolution'], r['scene'], r['step']) for r in runs} == {
        (32, 'A', 1), (128, 'A', 1), (128, 'B', 1), (128, 'A', 114)
    } and len(runs) == 4, 'Registered capture matrix incomplete or expanded'
    off, on = args.root/'n032-A-s001-off', args.root/'n032-A-s001'
    matches = 0
    for file in off.rglob('*.f32'):
        assert sha(file) == sha(on/file.relative_to(off)), file.name
        matches += 1
    output = dict(protocol='same-state pressure triage v1',
                  analyzer_sha256=sha(Path(__file__)), numpy_version=np.__version__,
                  controls=tests, off_on_identical_raw_fields=matches, runs=runs,
                  scope='Diagnostic only; old gate remains blocked; no G1 scoring')
    args.output.write_text(json.dumps(output, indent=2)+'\n')
    for r in runs:
        print(r['resolution'],r['scene'],r['step'],
              [(x['iterations'],round(x['relative_residual'],10)) for x in r['checkpoints']])
        print(json.dumps(r['variants'],indent=2))


if __name__ == '__main__':
    main()
