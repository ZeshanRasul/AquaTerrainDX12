# Appearance attribution: E0 measurement controls

Governing [research plan](../../RESEARCH_PLAN.md) and [registered protocol](SPEC.md).
These are instrument/source controls, not evidence of a new smoke technique.

## Current result

Rendering, static source-table controls, standalone normalized-source injection,
advection accounting, and the 32³ coupled source binding/schedule controls pass on
the RTX 5090. See the [source-injection findings](SOURCE_INJECTION_FINDINGS.md),
[accounting findings](ACCOUNTING_FINDINGS.md), and
[coupled bridge findings](COUPLED_BRIDGE_FINDINGS.md). The full G0 gate is not
closed: coupled pressure selection remains open before G1.

| Check | Worst measured value | Decision |
|---|---:|---|
| Original ray step 0.02 versus 0.005 | 0.00331272 mean absolute alpha | Fails fixed 0.003 bar; retained |
| Revised primary step 0.01 versus 0.005 | 0.00141828 | Pass |
| Same Gaussian/constant field across resolutions | 0.00099512 | Pass |
| Constant-field analytic alpha, step 0.005 | 0.00094157 mean; 0.00486957 maximum | Pass integration checks |
| Stored source integral relative error | 3.32436e-8 | Pass 1e-7 bar |
| Source quadrature 16³ versus 32³ per boundary cell | 0.00019342 mean absolute alpha | Pass 0.003 bar |
| Standalone source injection versus ideal integrated increment | 7.24382e-7 relative | Pass 2e-6 bar; 26 configurations |
| Coupled source audits, scenes A/B at steps 1/120/121 | 0 field mismatches; 0 invalid cells | Pass binding/schedule slice |

Final validation includes 243 basic-control images and 108 static-source images:
117 configurations of field/resolution/step/view, each captured three times.
Repeats are bitwise identical within each configuration. Images are finite,
nonnegative premultiplied RGBA; empty captures are exactly zero. D3D12 validation
was enabled with zero errors. It reports an unoptimized-clear performance warning;
these captures make no timing claim.

The standalone `SmokeAppearanceCapture` executable compiles the current production
pixel shader into a diagnostic wrapper. The wrapper generates box-entry positions
analytically, substitutes only the loop bound for refinement, and reads back a
512×512 RGBA32_FLOAT render target. It also accepts external R32_FLOAT density
files (z,y,x array order). It does not test the application's box-mesh rasterization,
scene occlusion, display compositing, simulation evolution or pressure solver.
The mass audit, production shader files and simulation code were not edited for
this experiment. Only a separate executable target was added to CMake.

## Decisions and repairs

1. `runs/e0-v1`: initial controls. The original step 0.02 fails; the planned
   refinement measurements identify 0.01 as adequate. Protocol v1.1 records an
   accuracy-only amendment before any G1 output exists. The 0.03 G1 threshold,
   cameras, density scale and absorption remain unchanged.
2. `runs/e0-v1_1`: fresh verification with D3D12 validation. A summary field still
   labelled the old 0.02 error as the primary error; the analyzer now selects the
   declared primary step. Per-comparison values and the pass decision were correct.
3. `runs/e0-source-v1`: first source analysis incorrectly used per-resolution ROIs,
   causing four small-source comparisons to fail the 1024-pixel eligibility check.
   The protocol's common ROI across resolutions was restored; no thresholds or
   source settings changed. Keep this run as a superseded analysis, not a null
   scientific result.
4. `runs/e0-source-v1_1`: fresh source run with the common ROI; all 18 comparisons
   pass. Source tables are validated rates, not proof of correct GPU time integration.
5. `runs/e0-final`: final executable, runner and analyzer with captured source
   snapshots. Its 243 images also match the earlier v1.1 images byte for byte.
6. `runs/e0-upload-verified` and `runs/e0-source-upload-verified`: authoritative
   final runs, adding direct readback of every GPU density texture. All uploads
   match the CPU inputs byte for byte, and all 351 rendered images match their
   earlier counterparts. Earlier source manifests' `exact_upload` field compared
   CPU staging data only; use these final runs for the GPU-upload claim.
7. `runs/source-injection-v1`: retained failed implementation run. After 120
   additions, all six O3 accumulated cases differed bitwise from debug, although
   one-step/seeded cases and the integrated-rate tolerance passed. The registered
   single repair marked the accumulation increment `precise`; it did not change
   sources, dt, cases or thresholds.
8. `runs/source-injection-v1_1`: the unchanged 26-configuration matrix passes.
   Density/temperature match the CPU float32 recurrence bitwise, three reset
   repeats and both compiler modes agree bitwise, and D3D12 validation reports
   zero errors. This validates the standalone kernel; coupled integration evidence
   is recorded separately.
9. `runs/coupled-source-schedule-v1_2-*` and `runs/pressure-selection-v1` are
   invalidated implementation runs. Benchmark readback could leave density in
   `COPY_SOURCE` before UAV binding, temperature lacked a required UAV dependency
   barrier, and the runs predated direct InfoQueue capture. Their trajectories and
   apparent pressure outcomes are not evidence.
10. `runs/coupled-source-schedule-v1_3-{A,B}-n32-i256` are fresh controls after
    those repairs. All six step-1/120/121 density and temperature audits match an
    independent reconstruction bitwise; source scheduling, twelve snapshots,
    configuration/hash checks and D3D12 validation pass. This closes the 32³
    coupled binding/schedule check only. Pressure selection remains open.

Raw runs are retained locally under `runs/` and ignored by Git; do not delete them.
The compact [results](results.json) record validation, run locations and manifest
hashes. Each run stores input fields, all float images, compiled shader input,
device/driver identity and hashes. Display PNGs are inspection aids; scoring uses
linear float alpha. No held-out scene C or scored coupled G1 comparison has been
run.

## Reproduce

Build `SmokeAppearanceCapture` using the existing configured Release build and
MSVC environment. Then use a Python installation with NumPy and Pillow:

```powershell
cmake --build out/build/x64-Release --config Release --target SmokeAppearanceCapture
python diagnostics/run_appearance_capture.py --output experiments/appearance-attribution/runs/new-controls
python diagnostics/analyze_appearance_capture.py experiments/appearance-attribution/runs/new-controls
python diagnostics/validate_appearance_sources.py --output experiments/appearance-attribution/runs/new-sources
cmake --build out/build/x64-Release --config Release --target SourceInjectionValidation
python diagnostics/run_source_injection_validation.py --output experiments/appearance-attribution/runs/new-source-injection
cmake --build out/build/x64-Release --config Release --target AquaTerrainDX12
python diagnostics/run_coupled_appearance.py --output experiments/appearance-attribution/runs/new-coupled-A --scene A --resolution 32 --iterations 256 --steps 121 --emitter-steps 120
python diagnostics/run_coupled_appearance.py --output experiments/appearance-attribution/runs/new-coupled-B --scene B --resolution 32 --iterations 256 --steps 121 --emitter-steps 120
```

Output roots must not already exist. Missing/stale builds and changing sources
fail the runner. This is not yet the requested two-device scientific replication;
reserve the laptop run for a stable decisive G1 setup unless a device-specific
measurement issue appears.

## Next bounded work

The sibling accounting observer, historical replay, standalone injector and 32³
coupled source bridge are complete. The fresh pressure-selection-v1_3 pilot
completed all 30 runs but is [blocked at the registered cap](COUPLED_PRESSURE_FINDINGS.md)
at 128³. The user's subsequent option-1/option-3 decision authorized a bounded
[same-state triage](PRESSURE_TRIAGE_FINDINGS.md), now complete and supporting
offline-reference feasibility. [Offline integration and the short replay](OFFLINE_REFERENCE_FINDINGS.md)
now pass. The [full-duration attempt](FULL_DURATION_REFERENCE_FINDINGS.md) is blocked
before opacity by a handoff failure; partial data also expose a primary identity
rounding limitation. See the [central plan](../../RESEARCH_PLAN.md) for the stopped
scope; opacity refinement and G1 remain unmeasured.
