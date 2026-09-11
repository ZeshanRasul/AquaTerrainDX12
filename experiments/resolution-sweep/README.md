# Advection resolution sweep (error vs. cost)

This experiment builds directly on the
[passive-advection reference harness](../passive-advection-reference/) — it runs the
moving-field cases at several grid resolutions and both schemes, and reports
**accuracy against isolated GPU cost**. It is the error-vs-cost figure a JCGT
reviewer will want, and the convergence-order check that distinguishes a
first-order scheme (semi-Lagrangian) from a higher-order one (MacCormack).

## Result (RTX 5090, Release, {32,64,128}³, 300 steps)

Accuracy (below) is validated and stable. The cost/error-vs-cost figures should be
regenerated with the **frozen optimized-timing suite** (see *Run*); the numbers
quoted below came from an initial run whose timing used the unoptimized production
shaders and are indicative only.

| Case | Scheme | L1 @ 32³ | L1 @ 64³ | L1 @ 128³ | **Refinement rate** |
|---|---|--:|--:|--:|:--:|
| Translation | Semi-Lagrangian | 0.792 | 0.322 | 0.133 | **1.29** |
| Translation | MacCormack, clamp | 0.307 | 0.055 | 0.023 | **1.86** |
| Rotation | Semi-Lagrangian | 1.079 | 0.711 | 0.366 | **0.78** |
| Rotation | MacCormack, clamp | 0.478 | 0.150 | 0.037 | **1.85** |

Refinement rate = |d log L1 / d log N|: the **empirical error-decay rate for this
smooth test**, *not* a proof of formal convergence order (no grid-independence or
asymptotic-regime claim is made). MacCormack's rate (~1.85–1.86) is consistent with
second-order behaviour and semi-Lagrangian's (~0.78–1.29) with first-order, measured
against analytic solutions on the corrected solver with no mass-creation or buoyancy
confound.

**Error-vs-cost (indicative).** MacCormack sits below/left of SL on the log-log
error-vs-cost plot: to reach SL's 128³ translation accuracy it needs only ~32–64³,
i.e. lower error at substantially lower advection cost. The exact cost ratio awaits
the optimized-timing rerun; see `figures/*-error-vs-cost.svg`.

**Conservation improves with resolution.** MacCormack's relative mass error falls
from ~0.12 (32³) to ~−0.0003 (128³) — the clamp's non-conservation is a coarse-grid
effect that vanishes under refinement, while SL stays conservative throughout. So
at usable resolutions MacCormack is both more accurate *and* nearly conservative.

**Two observations worth a sentence in the paper**, beyond the rates: MacCormack's
rate falls short of 2.0 (the clamp limiter reduces accuracy near the blob's
extrema), and SL's rotation rate (0.78) is below one — rotational phase error decays
more slowly than translation. Both are measured, not assumed.

## Question (answered above)

For a fixed physical transport problem, how fast does each scheme's error fall as
the grid is refined, and what does that accuracy cost in isolated advection time?

## Method

Each run is a hidden, env-var-driven instance launched at a chosen cubic
resolution (`AQUA_SMOKE_RESOLUTION`, read before any smoke resource is sized). The
**physical problem is held fixed** across resolutions, so refinement resolves the
*same* feature with more cells:

- Domain: unit cube (grid spacing 1/r).
- Blob: physical size fixed (sigma = 0.12·r cells = 0.12 of the domain).
- Translation: physical velocity fixed — `speed_cells = 0.375 · r/32`
  (0.375 / 0.75 / 1.5 cells per step at 32 / 64 / 128³; all fractional, no
  whole-cell exactness).
- Rotation: angular velocity resolution-independent (one revolution / 300 steps).
- Transport only: buoyancy, projection, confinement and damping all off.

Grid: **{32, 64, 128}³**, cases **{translation (periodic), rotation}**, schemes
**{SL, clamped MacCormack}**.

**Correctness vs. performance are separated.** Per config the runner does one
*correctness* run with the production (`SKIP_OPTIMIZATION`) shaders — the accuracy
of record, matching every other run and the mass audit — and several *timing* runs
with optimized (`OPTIMIZATION_LEVEL3`) shaders, warmup + repeats, for the isolated
advection cost. The analyzer takes error from the correctness run, cost as the
median over the timing runs, and **verifies optimization invariance**: the
optimized runs' final L1 must match the correctness L1, so the timing shaders are
confirmed not to change the numerics. Isolated advection time is GPU-timestamped
around the scalar-advection kernels only (no pressure/buoyancy), per step in
`steps.csv` as `advection_ms`.

## Run

Build Release first, then:

```powershell
cmake --build out/build/x64-Release --config Release
./diagnostics/Run-SmokeResolutionSweep.ps1 -OutputRoot ./diagnostics/runs/sweep-v1
```

The runner validates each run and calls `Analyze-SmokeResolutionSweep.ps1`, which
writes `summary.json`, prints the error/cost table, the empirical refinement rate
per (case, scheme), the optimization-invariance check, and (if Python is present)
error-vs-cost SVGs under `<run_root>/figures/`.

## What to expect

- **Whole picture:** MacCormack's error should fall faster with resolution than
  SL's, so on the log-log error-vs-cost plot the MacCormack curve sits **below and
  to the left** of SL — lower error at a given cost, or a target accuracy reached
  on a coarser (cheaper) grid.
- **Convergence order:** SL ≈ 1 (first order); MacCormack noticeably higher
  (≈ 1.5–2, limited below 2 by the clamp near extrema). The analyzer prints these.
- **Cost:** MacCormack's isolated advection time is a small multiple of SL's
  (three scalar dispatches vs. one), roughly constant in ratio across resolution.
- **Caveat carried from the reference harness:** MacCormack preserves shape better
  but is less mass-conservative than SL; `final_mass_error_rel` is reported so that
  trade-off stays visible.

## Provenance

Output folders are Git-ignored; copy the chosen run into `data/` deliberately when
preparing a paper artifact, together with `summary.json` and the figures.
Reference: builds on `../passive-advection-reference/` and the corrected solver
from `../mass-budget-audit/`.
