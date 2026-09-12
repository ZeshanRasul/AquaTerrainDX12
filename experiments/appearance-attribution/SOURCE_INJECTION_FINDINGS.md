# Standalone normalized-source injection findings

12 September 2026. Supports the [central research plan](../../RESEARCH_PLAN.md)
and follows the registered [source-injection controls](SOURCE_INJECTION_CONTROLS.md).

## Decision

The standalone normalized-source injection control passes. Across all 26
registered configurations, GPU density and temperature match the prescribed
float32 CPU recurrence bitwise, three reset repeats are bitwise identical, and
debug/skip-optimization and O3 outputs are bitwise identical. The worst relative
difference from the ideal real-number integrated increment is
`7.243820120069963e-7`, below the fixed `2e-6` limit.

This closes the standalone source-kernel portion of E0. It does **not** validate
which scalar ping-pong pair a future coupled run binds, whether emission stops at
the registered boundary, or any advection, buoyancy, projection, rendering or G1
result. The coupled harness must still capture density and temperature immediately
before and after injection at steps 1, 120 and 121.

## Registered matrix and checks

The matrix contains one-step and 120-step zero-initialized cases for scenes A and
B at 32³, 64³ and 128³, in both compiler modes, plus the seeded scene-A 32³ case
in both modes: 26 configurations total. Every configuration has three fresh-reset
GPU repeats.

All configurations satisfy the registered checks:

- density and temperature equal the CPU float32 recurrence bitwise;
- repeats and compiler modes agree bitwise;
- all values are finite and cells outside the source support remain unchanged;
- integrated increments satisfy the fixed ideal-value tolerance; and
- D3D12 validation reports zero errors.

Direct artifact verification found zero manifest output-hash mismatches and zero
manifest source-hash mismatches.

## Preserved failed run and repair

`runs/source-injection-v1` is a retained implementation-validity failure. Its
one-step and seeded cases passed, and its accumulated fields remained within the
integrated-rate tolerance, but all six O3 120-step configurations differed from
their debug counterparts. Depending on scene and resolution, 4–212 cells differed,
with maximum absolute differences from `2.38e-7` to `1.38283e-5`.

The single registered repair declared the read/modify/write increment `precise`,
preventing contraction across the rate-times-dt multiplication and the existing
field value. `runs/source-injection-v1_1` reran the unchanged 26-configuration
matrix. Source tables, dt, initial fields, step counts and pass thresholds were
not changed after seeing v1.

## Artifacts and reproduction

The authoritative v1.1 run is retained locally under
`runs/source-injection-v1_1`; the failed v1 run is retained beside it. Both roots
are ignored by Git and should not be deleted. Compact hashes are recorded in
[results.json](results.json).

Reproduce in fresh output roots:

```powershell
cmake --build out/build/x64-Release --config Release --target SourceInjectionValidation
python diagnostics/run_source_injection_validation.py --output experiments/appearance-attribution/runs/new-source-injection
```

The next bounded task is coupled integration validation and the pressure-control
pilot. No calibration or G1 claim is supported by this standalone result.
