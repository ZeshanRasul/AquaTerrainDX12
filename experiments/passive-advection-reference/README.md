# Passive-advection reference harness

A prescribed-velocity transport harness that runs the **production** advection
kernels (semi-Lagrangian and clamped MacCormack) with buoyancy, projection,
confinement and damping all disabled, and scores each step against an analytic
reference. It is the first reference-validated, confound-free measurement of
transport accuracy in this project — no mass-creation bug, no buoyancy feedback,
a known answer.

**Headline (increment 2, moving fields):** against the analytic solution,
**MacCormack roughly halves the transport error of semi-Lagrangian** —
2.4× lower on fractional-cell translation and 2.3× lower on solid-body rotation —
while being slightly *less* mass-conservative. See the table below.

## Increment 1: identity checks (zero velocity)

Independent verification on 11 September 2026, RTX 5090, Release CPU build,
existing unoptimized production compute shaders. This covers zero velocity only.

| Case | Method | Steps | Maximum normalized L1 | Maximum normalized L2 | Nonfinite cells |
|---|---|---:|---:|---:|---:|
| Source only | SL | 300 | 0 | 0 | 0 |
| Source only | MacCormack, clamp | 300 | 0 | 0 | 0 |
| Static Gaussian | SL | 300 | 1.49333e-7 | 1.12486e-7 | 0 |
| Static Gaussian | MacCormack, clamp | 300 | 1.49333e-7 | 1.12486e-7 | 0 |

The Gaussian CPU reference uses double precision, while initialization uses
float shader arithmetic. The definitions match mathematically; they are not
bit-identical implementations. The observed small static error is consistent
with that precision difference. These results establish identity transport and
the finite source schedule, not moving-field accuracy or conservation.

## Review fixes

- Reference dispatches write only density readback. The shared collector now
  skips timestamps and numerical-diagnostic records that reference runs never
  write, avoiding reads of uninitialized or stale data.
- Hidden-window and noninteractive D3D-error handling recognize reference mode.
- The runner clears/restores an inherited mass-audit environment variable,
  checks the process exit code, and invokes numerical validation automatically.
- Validation rejects incomplete/misordered steps, missing artifacts, nonfinite
  metrics and failed L1, L2, or relative mass tolerances. Failures throw instead
  of only warning. The current default tolerance remains 1e-4.
- Analytic reference sums include all reference cells even if a simulated cell
  is nonfinite; nonfinite simulation output still fails validation.
- Plotting accepts an explicit Python executable and labels the log-axis floor,
  so zero error is not presented as a measured 1e-8 error.

`Test-SmokeReferenceValidation.ps1` confirmed rejection of five damaged copies:
truncated CSV, duplicate/misordered step, NaN metric, L2-only failure, and missing
steps CSV. The existing mass-audit suite was also rerun after the collector change:
both 480-step audited runs pass and match their uninstrumented controls.

## Increment 2: moving-field transport accuracy

Cases 3 (constant +x translation, periodic) and 4 (solid-body rotation about z,
one revolution over 300 steps) advect a smooth Gaussian blob under a prescribed,
time-invariant velocity and compare against the exactly-shifted / exactly-rotated
analytic blob (cell-centred samples). 32³ grid, dt = 1/60 s, 300 steps.

| Case | Method | Final normalized L1 | Final rel. mass error | Note |
|---|---|---:|---:|---|
| Translation, whole-cell (1.0/step) | SL & MacCormack | 8.6e-8 | −8e-8 | exact (integer shift) — seam sanity check |
| Translation, fractional (0.5/step) | Semi-Lagrangian | **0.807** | +6e-6 | |
| Translation, fractional (0.5/step) | MacCormack, clamp | **0.338** | +0.057 | **2.4× lower error** |
| Rotation | Semi-Lagrangian | **1.079** | +0.017 | |
| Rotation | MacCormack, clamp | **0.478** | +0.122 | **2.3× lower error** |

All runs finite; identity cases (source-only, static, whole-cell translation) stay
at the ~1e-7 float floor.

**Result.** On both moving-field tests MacCormack roughly halves the
semi-Lagrangian transport error — the anti-diffusion benefit, now measured against
a known answer with the mass-creation confound removed. This is the honest,
reference-validated version of what the earlier coupled-simulation "peak density"
and "total mass" ratios only appeared to show (those were dominated by a
descriptor-binding bug; see `../mass-budget-audit/` and `../advection-maccormack-vs-sl/`).

**Conservation trade-off.** MacCormack preserves *shape* better (lower L1) but is
*less* mass-conservative than SL: on rotation it gains ~12% mass versus SL's ~2%,
and on fractional translation ~6% versus SL's ~0%. This is the expected behaviour
of the clamped limiter (which can inject mass) and is a legitimate, cite-worthy
observation rather than a defect — the two schemes trade sharpness against
conservation.

### Periodic seam fix (methodology note)

The first translation runs lost almost all density (SL: 99.9%) at the periodic
boundary. Cause: coordinates were wrapped with `frac()` but sampled with a *clamp*
sampler, which cannot blend texel N-1 with texel 0 across the seam, while the
MacCormack limiter used wrapped (modulo) neighbours — so the sampling and limiter
neighbourhoods disagreed. Fix: a genuine `WRAP` static sampler (register s1) used
only when `periodicDomain` is set, and no `frac()`. The wrap sampler blends across
the seam natively and its footprint matches the modulo limiter. Whole-cell
translation dropping to the ~1e-7 float floor confirms the seam is now exact.
Non-periodic runs are byte-unchanged (they still use the clamp sampler), so the
mass-audit hashes and all production behaviour are unaffected.

## Reproduce

Reconfigure and build in the x64 Visual Studio developer shell:

```powershell
cmake -S . -B out/build/x64-Release
cmake --build out/build/x64-Release --config Release
./diagnostics/Run-SmokeAdvectionReference.ps1 -OutputRoot ./diagnostics/runs/my-reference
./diagnostics/Test-SmokeReferenceValidation.ps1 -RunRoot ./diagnostics/runs/my-reference
```

For plots when Python is not on PATH:

```powershell
./diagnostics/Analyze-SmokeAdvectionReference.ps1 -RunRoot ./diagnostics/runs/my-reference -PythonExecutable '<absolute path to python.exe>'
```

Local review results: `diagnostics/runs/reference-independent-review/`, including
summary JSON, per-case CSV/manifests/provenance and SVG figures. Regression results:
`diagnostics/runs/mass-audit-reference-regression/`. These output folders are
Git-ignored; reproduce or copy them deliberately when preparing a paper artifact.

The harness is complete: identity checks (increment 1) and moving-field transport
accuracy (increment 2) both validated. It now provides the reference-based
accuracy metric the JCGT investigation needs. Natural follow-ups, not part of this
harness: a resolution sweep (32/64/128³) to show error-vs-cost, and re-asking the
limiter question (clamp vs revert vs a candidate) on these clean transport tests
rather than the buggy coupled runs.
