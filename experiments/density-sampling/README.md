# Density-sampling ablation

This experiment replaces only the SL density sample with eight clamped texture
loads and float interpolation. It tests actual one- and two-step density updates
with projected velocity held fixed. See [FINDINGS.md](FINDINGS.md) for results.

## Protocol

- Ten cases: N=32/64, sharp baseline, sharp phase shift (0.75/0.25 cells
  respectively), doubled dt, half dt with half-cell shift, and smooth baseline.
- Per case: hardware/float sampling, candidate pressure budget (512 at N=32,
  2048 at N=64), 4096 and 8192 references, plus independently constructed
  divergence-free velocity control (-1). Eight launches per case.
- Three repeat trials per launch, each reset to the same initial condition,
  with two scalar steps after projecting velocity once. Total: 80 launches,
  240 trials. No rendering or timing comparison is used as an outcome.
- Each sampling method is compared with its own 8192-iteration density result.
  This isolates pressure-budget sensitivity, not total transport accuracy.
- Both paths retain hardware velocity sampling and the same BackTrace, boundary
  guards, attenuation, and temperature formula. The alternate entry is only
  selected within the projection experiment and requires transport probes.
  These sealed-domain runs do not establish periodic-boundary behavior.

The analyzer verifies seven unchanged fields across paths (initial density,
divergence before/after, pressure, u/v/w), midpoint/departure coordinates at both
steps, the step-to-step input chain, and bitwise equality between the selected
probe sample and the actual update. CPU interpolation and stencil bounds are
checked independently. It also checks 4096/8192 and independent-control agreement
for both sampling methods, and compares every hardware run with the prior
transport-probe experiment's eight fields and two traces.

Raw fields, manifests, traces, source snapshots and provenance:
`diagnostics/runs/density-sampling-v1`. The smaller pilot is under
`diagnostics/runs/density-sampling-pilot`.

## Reproduce on the main machine

Build `AquaTerrainDX12` Release from an MSVC developer shell, then:

```powershell
./diagnostics/Run-SmokeDensitySampling.ps1 -OutputRoot diagnostics/runs/density-sampling-new
python diagnostics/analyze_density_sampling.py diagnostics/runs/density-sampling-new --out experiments/density-sampling-new --history diagnostics/runs/transport-probe-v1
```

Use a fresh output directory. Python requires NumPy. `--history` is optional
on another device: cross-device equality must not be used as a validity gate.
The current figure and negative-validation scripts target the recorded run/pilot.

## RTX 3070 validation: ready now

Transfer [rtx3070-sampler-validation.zip](rtx3070-sampler-validation.zip) to the
laptop and extract it. This small first cross-device test uses the original
sampler executable, shader, and queries, not the whole renderer. It needs Windows
DX12 and the Microsoft Visual C++ x64 runtime; no Python or project build.

Run from PowerShell in the extracted directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Run.ps1
```

Confirm the printed GPU name identifies the RTX 3070 rather than an integrated
adapter. Windows per-app graphics settings can select the NVIDIA device if
needed. Return the generated `results-TIMESTAMP.zip`. The package README has
the same instructions. Local package execution reproduced the earlier RTX 5090
sample buffers bitwise; that checks packaging, not cross-device portability.

First analyze the laptop sampler's own constants, corners, repeatability, basis
weights and general-value predictions. Then compare devices. Next replicate the
original 32/64 density-sampling cases on the laptop (the runner's `-Pilot` covers
32 only; the full runner includes both sizes and all perturbations). Differences
are experimental observations, not automatic failures.
