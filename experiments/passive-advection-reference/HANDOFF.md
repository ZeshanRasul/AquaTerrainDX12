# Passive-advection reference suite — handoff / freeze

**Status: frozen as a trustworthy accuracy baseline for the paper (11 Sep 2026).**
This document states exactly what the suite validates, what it does not, and how to
reproduce it. No new limiters or adaptive methods are included (deferred by
decision). The pressure solver and mass-audit infrastructure are untouched.

## What the suite is

A prescribed-velocity transport harness that runs the **production** advection
kernels (semi-Lagrangian and clamped MacCormack) with buoyancy, projection,
confinement and damping disabled, and scores each step against an **analytic**
reference (cell-centred Gaussian, exactly shifted or rotated). It is the first
reference-validated, confound-free accuracy measurement in this project.

Four cases: source-only and static (identity checks), periodic translation
(whole- and fractional-cell), solid-body rotation. Plus a resolution sweep
({32,64,128}³) for error-vs-cost and empirical refinement rate.

## What is VALIDATED

- **Harness plumbing.** Source-only and static cases give ~0 error (float floor
  ~1e-7), confirming injection, identity transport, readback, and the metric math.
- **Periodic sampling ↔ limiter consistency.** Periodic density sampling uses a
  `WRAP` static sampler (register s1); the MacCormack limiter uses modulo neighbours;
  the two footprints agree. Proof: whole-cell (integer-shift) periodic translation
  returns to the ~1e-7 float floor for both schemes — an exact identity that only
  holds if sampling and limiter wrap identically. (An earlier `frac()`+clamp version
  failed this and lost 99.9% of mass at the seam; that is fixed.)
- **Transport accuracy (fractional translation, rotation).** Against the analytic
  answer, clamped MacCormack roughly halves semi-Lagrangian's normalized L1
  (~2.3–2.4× lower) — the anti-diffusion benefit with no mass-creation or buoyancy
  confound.
- **Empirical refinement rates.** |d log L1 / d log N| ≈ 1.85–1.86 (MacCormack) and
  ≈ 0.78–1.29 (semi-Lagrangian), consistent with second- and first-order behaviour
  respectively for this smooth test.
- **Correctness/performance separation.** Accuracy is measured with the production
  (`SKIP_OPTIMIZATION`) shaders — identical to every other run and the mass audit.
  Timing uses optimized (`OPTIMIZATION_LEVEL3`) shaders with warmup and repeated
  runs, and the analyzer **verifies the optimized shaders reproduce the production
  accuracy** (optimization invariance) before trusting any timing number.
- **Reproducibility/provenance.** Every run writes `provenance.json` (git HEAD,
  executable + runtime-shader SHA-256, resolution, optimization flag) and a
  self-describing `manifest.json`; the runtime shader hash is checked against source
  before each run so stale builds are rejected.

## What remains UNCERTAIN / caveats

- **Refinement rates are empirical, not formal convergence proofs.** They are
  error-decay slopes over three resolutions for one smooth Gaussian; no
  grid-independence, asymptotic-regime, or formal order claim is made. Report them
  as observed rates.
- **Timing needs the frozen optimized rerun.** The first resolution sweep timed the
  *unoptimized* shaders; its cost numbers are indicative only. Rerun with the
  current suite (which uses optimized-timing runs + invariance check) for the cost
  figures of record. Also prefer a second GPU / vendor before publishing timing.
- **Conservation trade-off.** Clamped MacCormack is less mass-conservative than SL
  (it gains a few % on coarse grids); this shrinks under refinement (~0.12 at 32³ →
  ~−0.0003 at 128³) but is real and is reported as `final_mass_error_rel`.
- **Single feature / scenario.** One blob, two motions, cubic grids, no obstacles or
  discontinuities (no Zalesak-style step). Adequate as a smooth-transport baseline;
  not a full advection test battery.
- **Clamp limiter only.** Revert and any adaptive limiter are deliberately out of
  scope here.

## Reproduce

Build Release (developer shell; reconfigure if new sources were added), then:

```powershell
cmake --build out/build/x64-Release --config Release
# Correctness suite (identity + moving-field cases, production shaders):
./diagnostics/Run-SmokeAdvectionReference.ps1 -OutputRoot ./diagnostics/runs/ref
./diagnostics/Analyze-SmokeAdvectionReference.ps1 -RunRoot ./diagnostics/runs/ref
# Resolution sweep (correctness + optimized timing + invariance check):
./diagnostics/Run-SmokeResolutionSweep.ps1 -OutputRoot ./diagnostics/runs/sweep
```

Output folders are Git-ignored; copy a chosen run's `manifest.json`, `steps.csv`,
`provenance.json`, `summary.json` and figures into an experiment `data/` folder
deliberately when preparing a paper artifact.

## Scope boundary (for the next experiment)

The next experiment — how pressure-projection accuracy interacts with advection
quality under a fixed GPU budget — is handled separately (Codex). It must **not**
change the pressure solver or the mass-audit infrastructure, and it can reuse this
harness's reference metric and provenance pattern unchanged.
