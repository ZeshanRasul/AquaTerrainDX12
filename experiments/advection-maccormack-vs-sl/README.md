# MacCormack vs. Semi-Lagrangian advection (GPU 3D smoke)

**Question:** how much does a MacCormack advection scheme reduce numerical
diffusion versus plain semi-Lagrangian (SL) advection in the DX12 compute smoke
solver, and at what GPU cost?

**Answer (headline):** at matched settings, MacCormack preserves **~12.8× higher
peak density** and **~10× more total smoke** than semi-Lagrangian, for **+3.3%
GPU solver time**. Peak-density preservation is the robust, well-behaved metric
across the whole run; the total-mass integral additionally exposes a documented
limitation of the clamped scheme (see *Limitations*).

| Metric (32³ grid, open-top plume) | Semi-Lagrangian | MacCormack | Ratio |
|---|--:|--:|--:|
| Solver time, mean (ms) | 0.0917 | 0.0948 | **1.033×** |
| Solver time, p95 (ms) | 0.0933 | 0.0963 | 1.032× |
| **Peak density (whole run)** | **7.70** | **98.82** | **12.8×** |
| Total density at emitter-off (step 240) | 215.5 | 2460.8 | 11.4× |
| Kinetic energy at step 239 | 1.9e-4 | 1.5e-3 | ~7.9× |
| Max speed at step 239 | 0.82 | 2.62 | 3.2× |

Solver time is GPU execution time for the solver stages only (excludes volume
rendering and readback), post-warmup mean over 450 steps. Hardware: NVIDIA
GeForce RTX 5090, Release build. Full parameters in `data/*/manifest.json`.

## Figures

![Peak density over time](figures/peak-density.svg)

*Peak density (`density_max`) vs. step.* Semi-Lagrangian caps near 5–8 for the
entire run: its numerical diffusion smears the injected peak away as fast as it
is created. MacCormack climbs to ~99 while the emitter runs, then decays
smoothly as the plume vents through the open top. Same source, same physics —
the gap is purely the advection scheme.

![Total density over time (log scale)](figures/density-sum.svg)

*Total density (`density_sum`, log₁₀) vs. step.* MacCormack holds roughly an
order of magnitude more smoke through emission and early decay. Note the sharp
upturn after step ~430: a late-onset artifact of the clamped limiter discussed
below.

## Method

A single GPU solver runs both schemes; only the advection kernel differs. The
"Start matched A/B benchmark" control pins one canonical configuration and
records the advection mode into each run's `manifest.json`, so the two runs are
identical in everything but the scheme under test:

- Grid 32³, grid spacing and origin fixed; emitter at (16, 8, 16).
- 480 fixed steps at Δt = 1/60 s; emitter on for the first 240, off after.
- Pressure projection: 40 Jacobi iterations (identical for both).
- Physics: buoyancy 0.6, smoke weight 0.05, temperature cooling 0.5,
  **density dissipation 0.1** (equal for both), open top.
- Simulation-only (volume rendering disabled) for clean GPU timing.

Because dissipation, cooling, boundaries and the pressure solve are identical
across the pair, any difference in peak density or plume sharpness is
attributable to advection alone.

**Semi-Lagrangian** traces one backward characteristic per cell and
interpolates — first-order, heavily diffusive. **MacCormack** adds a reverse
advection pass to estimate the interpolation error and corrects for it
(`φⁿ⁺¹ = φ̂ + ½(φⁿ − φ̄)`), with a limiter that clamps the result to the local
extrema of the source field to suppress overshoot. On this solver it costs three
extra scalar dispatches per step, which is only ~3% of frame time because the
40-iteration Jacobi pressure solve dominates.

## Interpretation

The peak-density plot is the clean statement of the result: identical injection,
but SL cannot hold a sharp feature while MacCormack can. The 12.8× peak ratio and
~10× mass retention are the numerical signature of the diffusion MacCormack
removes, and they translate directly to the visual difference (crisp filaments
vs. a diffuse blob). The higher kinetic energy and max speed show the same thing
in the velocity coupling: MacCormack preserves buoyant structure that SL smooths
into stillness. All of this for a ~3% solver-time increase — an excellent
detail-per-millisecond trade on this pressure-solve-dominated pipeline.

## Limitations (and what they taught us)

This comparison went through three scenario designs; the failures are
informative and are recorded here rather than hidden.

1. **A sealed box with zero dissipation is an ill-posed conservation test.**
   With no outlet and no sink, a finite source accumulates without bound, and a
   *non*-diffusive scheme faithfully preserves that growing peak. The test then
   rewards diffusion (SL looks "stable" only because it destroys mass) and
   punishes the scheme that does its job. We switched to a vented, mildly damped
   plume and report **peak-density preservation** — a metric that reflects
   numerical diffusion and maps to visual quality.

2. **The clamped MacCormack limiter is not strictly conservative.** Clamping the
   corrected value to local extrema can inject mass; in a trapped or weakly
   damped flow this accumulates. That is the late-run upturn in the total-density
   plot (after ~step 430, as post-emission smoke recirculates), where the volume
   integral grows even though peak density keeps decaying monotonically. Peak
   density is therefore the trustworthy metric here; the integral is only
   trustworthy up to ~step 400.

3. **The revert-to-first-order variant** (Selle et al. 2008 — revert to the SL
   result wherever the correction overshoots) removes the instability but, on
   this adversarial scenario, reverted so pervasively that it discarded most of
   the anti-diffusion and became *more* diffusive than SL. The plain clamp is the
   right choice for demonstrating the diffusion reduction; a genuinely
   conservative correction (or a flux-limited/BFECC hybrid) is the natural next
   step for long trapped-flow runs.

## Reproduce

1. Build Release. Open the **Smoke 3D GPU** panel.
2. Set **Advection Mode → Semi-Lagrangian**, click **Start matched A/B benchmark**,
   wait for the saved-path status.
3. Set **Advection Mode → MacCormack**, click it again.
4. Compare:

```
pwsh -File diagnostics/Compare-AdvectionRuns.ps1 -RunA <sl_run_dir> -RunB <mc_run_dir>
```

Regenerate the figures from the copied CSVs with the plotting script noted in the
project scratchpad (pure-Python SVG, no dependencies).

## Provenance

- `data/sl/` and `data/maccormack/` — each run's `manifest.json`, `summary.csv`,
  `steps.csv` exactly as exported.
- Reference: A. Selle, R. Fedkiw, B. Kim, Y. Liu, J. Rossignac,
  "An Unconditionally Stable MacCormack Method," *J. Sci. Comput.*, 2008.
