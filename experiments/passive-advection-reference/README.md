# Passive-advection reference: increment 1 review

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

Next increment: prescribed translation, with consistent periodic sampling and
limiter neighborhoods, followed by solid-body rotation. Keep those results
separate from these identity checks.
