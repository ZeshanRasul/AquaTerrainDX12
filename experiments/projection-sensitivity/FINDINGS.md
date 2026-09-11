# First deliverable: measured projection sensitivity versus GPU cost

The full experiment completed on an NVIDIA GeForce RTX 5090. There are 216
optimized launches (72 configurations, three launches each), comprising 5,184
independent reset trials. Every recorded field repeated bit for bit within and
across launches. All wall, nonfinite, pressure/divergence identity, and convergence
checks passed. GPU timing variation across launches was small: the largest IQR
of launch medians was 3.36% of the corresponding median cost.

## What the results show

Increasing pressure iterations reduces the density discrepancy caused by incomplete
projection, but weighted Jacobi becomes expensive well before its numerical floor.
At 64³, representative smooth-density results are:

| Jacobi iterations | SL combined cost (ms) | SL projection-induced L1 | MacCormack combined cost (ms) | MacCormack projection-induced L1 |
|---:|---:|---:|---:|---:|
| 0 | 0.01685 | 0.033606 | 0.02574 | 0.032549 |
| 128 | 0.49200 | 0.022163 | 0.50094 | 0.021470 |
| 512 | 1.91170 | 0.006393 | 1.92154 | 0.006204 |
| 2048 | 7.61494 | 0.00003973 | 7.62550 | 0.00004803 |

These errors use each method's own 8192-iteration result as reference. The table
therefore measures sensitivity to projection error, **not the total accuracy of
the two advection methods against a common transport reference**. It does not
establish a winning solver allocation or a general SL/MacCormack crossover.

The most useful follow-up observation is a density plateau before pressure
convergence. For the sharp feature with SL:

| Resolution | First sampled budget with exactly matching reference density | Relative pressure residual | Relative velocity L2 error to independent control |
|---:|---:|---:|---:|
| 32³ | 512 | 0.001024 | 0.0006244 |
| 64³ | 2048 | 0.001153 | 0.0006792 |

The density arrays match exactly, while the velocity demonstrably does not.
MacCormack still has small density discrepancies at those same budgets
(3.27e-6 and 8.00e-6 normalized L1 respectively). This is a property of these
sampled one-step tests. It is not evidence that residuals below 1e-3 are generally
irrelevant or that the simulation can safely stop solving at those budgets.

## Reference and compiler checks

The 8192-iteration pressure residual reaches approximately 3.49e-6 at 32³ and
1.25e-5 at 64³. The largest normalized density L1 discrepancy between 4096 and
8192 iterations is 1.18e-10. The largest discrepancy between 8192 iterations and
the independently initialized divergence-free velocity control is 4.66e-10.
The reference is a verified discrete control, not an exact continuum solution.

Twenty-four additional production-build comparisons covered both resolutions,
both density shapes, both methods, and budgets 0, 128, and 8192. All eight exported
fields matched the optimized build bit for bit in every comparison. Thus the
optimized pressure and advection costs correspond to the same measured numerics.

Eight damaged-data tests were rejected as intended: missing/reordered trials,
nonfinite timing or density, nonidentical repeats, closed-wall leakage, inconsistent
pressure, and a missing field.

All ten existing passive-reference configurations passed regression checks. The
SL and MacCormack mass audits each passed 480 steps and matched their uninstrumented
controls on every density hash and velocity diagnostic; the measured budgets
closed within the existing 1e-4 tolerance. Their summaries are archived here as
`reference-regression-summary.json` and `mass-regression-summary.json`.

## Next experiment

Probe the plateau before building a stopping policy:

1. Densely sample budgets around 512 at 32³ and 2048 at 64³. Shift the density by
   fractions of a cell, vary time step, and change the perturbation amplitude.
   This will test whether the plateau depends on grid alignment and sampling.
2. Move an otherwise identical density feature between regions of strong and weak
   residual divergence while preserving the same velocity and pressure residual.
   Compare ordinary residuals with density-weighted divergence against actual
   transport changes. One velocity family cannot establish predictor superiority.
3. Check two-step and short evolving trajectories. A velocity discrepancy that
   leaves one density update unchanged may matter later.

A useful predictor must generalize across these tests and beat ordinary residual
thresholds after its diagnostic overhead is included. A common transport reference
is additionally required before claiming an optimal advection/projection allocation.

## Artifacts

- `figures/smooth-r32-error-vs-cost.svg`
- `figures/smooth-r64-error-vs-cost.svg`
- `figures/sharp-r32-error-vs-cost.svg`
- `figures/sharp-r64-error-vs-cost.svg`
- `aggregate.json`: medians across launches, field errors, residuals, mass changes.
- `run-summary.csv`: one row per launch.
- `provenance.json` and `analysis-provenance.json`: build/source and analysis hashes.

Full binary fields, source snapshot, trials, and manifests are retained in the
gitignored `diagnostics/runs/projection-v1` directory. Compiler comparisons are
in `diagnostics/runs/projection-production-check`.

The sealed-top pressure-gradient correction means earlier *coupled closed-domain*
results need regeneration before comparison with this experiment. Earlier passive
transport results do not execute that kernel.
