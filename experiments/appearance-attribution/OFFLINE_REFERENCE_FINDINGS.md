# Offline integration and short coupled replay: validated

12 September 2026. Authoritative matrix: `runs/offline-reference-v1_2`.
[Protocol](OFFLINE_REFERENCE_SPEC.md), [machine-readable results](offline-reference-results.json).
The bounded offline-reference integration deliverable is complete. This is a
positive feasibility result, not evidence for a smoke-appearance technique.

## What is integrated

The explicitly gated appearance path pauses after pre-projection GPU work, waits
for a fence, exports the actual RHS/velocities, obtains the CPU result through
step-specific request/response files, uploads it and resumes the GPU simulation.
Resources are fenced before reuse and upload readback is checked byte-for-byte.
The production shader and default pressure method are unchanged.

- `pressure32`: cosine-basis float64 solve of the stored divergence RHS, float32
  pressure upload, production GPU pressure-gradient subtraction.
- `velocity64`: independently constructed stencil eigensolve using float64
  divergence from the captured velocities, float64 projection, float32 velocity
  upload. The analyzer accounts explicitly for velocity-storage rounding rather
  than equating it with the float64 solver residual.

Both paths retain the closed-box pressure gauge and report RHS compatibility.
Their CPU synchronization, file I/O and diagnostic timings are not real-time
performance evidence, and this is not a new pressure-solver contribution.

## Fixed matrix and results

Resolutions 32³/64³/128³, scenes A/B, both modes: twelve runs. Two additional
128³ A repeats give fourteen runs / 168 full-field steps. Every trajectory is
12 steps (approximately 0.2 seconds), with six emitting and six source-off steps.
Source injection was audited at steps 1,6,7.

| Metric, maximum over all relevant steps including repeats | pressure32 | velocity64 |
|---|---:|---:|
| Relative projected divergence | 2.201384e-6 | 1.015022e-6 |
| dt × maximum absolute divergence | 5.463760e-9 | 2.110998e-9 |
| Float64 linear-system residual, relative | 5.665347e-14 | 3.450406e-14 |
| Discrete identity RMS error | 2.660132e-9 | 4.933256e-18 |
| Residual/rounding-predicted versus compact divergence ratio difference | 4.184881e-7 | 7.085430e-8 |

All primary steps pass the registered 1e-4 relative / 1e-5 absolute criteria.
All refined steps pass 1e-5 relative / 1e-6 absolute. No limit was relaxed.
All full-field identity, divergence-reconstruction, reduction, boundary,
finiteness, configuration, provenance and actual source-schedule checks pass.
There were zero D3D12 errors and zero discarded messages across the matrix.

Pressure upload/readback is exact. Primary GPU projected velocities match the CPU
float32 replay bit-for-bit; refined GPU velocities match the CPU response and
reconstructed refined projection exactly. Startup inputs match the existing
triage states at 32³ A and 128³ A/B. Both 128³ A repeats reproduce every retained
float32 and float64 raw file bit-for-bit. Manufactured cosine and independent
eigen-solver controls pass against the explicit stencil.

The fresh off-mode control preserves the old seven raw fields and 26 non-timing
columns. Frozen mass-audit files and the production simulation/render shaders
remain unchanged relative to the preceding validated work.

## Retained development failure and single repair

`offline-reference-v1` was an initial smoke test before full float64 capture.
The first full matrix, v1_1, stopped when the 180-second watchdog expired during
128³ A refined execution. It is incomplete and is not combined with v1_2 results.
Per-step file timestamps show CPU responses in about 0.3 s; the delay occurred
afterward in the upload/application path, approximately 16 s per step.

The finite-value check read mapped upload memory. The single repair moves that
check into ordinary CPU staging memory, then memcpy-writes the upload heap.
This follows [D3D12 Map guidance](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map).
The earlier CPU-threading hypothesis was rejected: standalone and background
thread solves were fast, and the request timestamps located the actual delay.
No thread setting, solver arithmetic, watchdog duration or numerical threshold
was changed. The measured post-response delay fell below 0.1 s in the repaired
128³ A refined run; this is an execution diagnostic, not a solver speedup claim.

All 147 raw fields of a matched 32³ replay were identical across the repair.
All 151 available float32 exchange files from the interrupted 128³ refined run
also match the repaired replay. The complete matrix was restarted from scratch.

## Decision and limits

Proceed to the registered full-duration reference controls. This short test
establishes integration validity through initial coupled evolution and source
shutoff. It does not establish six-second robustness, pressure-insensitive opacity,
the G1 resolution effect, held-out transfer, or laptop portability.

The old Jacobi selection remains blocked. Full 180/360-step checks and the
predeclared primary/refined alpha discrepancy below 0.003 are still required
before G1. No density images or cross-resolution appearance scores were inspected.
Option 3 remains the fallback if bounded reference feasibility fails later.

## Reproduce

Reconfigure/build the Release AquaTerrainDX12 target, then run into a fresh root:

```powershell
python diagnostics/run_offline_reference_replay.py --root experiments/appearance-attribution/runs/new-offline-replay
```

The finite runner validates each run before continuing, pins source/build state,
stops on failure, and checks the complete matrix and repeats at the end. The
separate off-mode control uses `run_coupled_appearance.py` with `--iterations 65536
--steps 2 --emitter-steps 2 --scene A --resolution 32` and no offline mode.
