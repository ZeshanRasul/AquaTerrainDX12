# Result: the exact density plateau is not robust

The plateau survives some small perturbations, but fails under changes to density
alignment, timestep, and a second advection step. It is not a reliable general
pressure-stopping signal. The failures are small in magnitude; this experiment
does not establish that they are visually significant.

The prescribed matrix completed: **288 launches, 864 reset trials, 72 perturbation
cases**, including matched long-solve and independently prescribed velocity controls.
All field repeatability, initial-condition, wall, pressure/divergence consistency,
and reference-convergence checks passed.

## Direct answers

| Perturbation | Observation at the original plateau budget |
|---|---|
| Fractional density shift | At 32³ and the original dt, a 0.75-cell diagonal shift changes the first-step L1 error from exactly zero to 9.31e-5. At 64³, even a 0.25-cell shift gives 2.00e-5. |
| Double timestep | Exact equality fails in every sharp-SL case tested at double dt, at both resolutions and both step counts. |
| Half timestep | All 32³ sharp-SL cases remain exact. At 64³, some first-step plateaus survive, but none remain exact after two steps. |
| Second advection step | At 64³ with no shift and the original dt, first-step equality becomes L1 = 1.10e-5 after step two (80 changed cells), with exactly the same projected velocity. |

The candidate pressure budget stays fixed at 512 iterations at 32³ and 2048 at
64³. The second step transports the first result with frozen projected velocity;
it is not a second coupled smoke-solver step. Each trajectory is compared against
its own reference at the same phase, timestep and step count.

### Exact-equality counts

| Group | Exact reference density | Tested cases |
|---|---:|---:|
| Sharp SL, 32³ | 14 | 24 |
| Sharp SL, 64³ | 3 | 24 |
| Sharp MacCormack checks | 2 | 16 |
| Smooth SL checks | 0 | 8 |

For the principal sharp-SL matrix, **17/48** cases remain exact. Three matched
one/two-step pairs are exact after one step but not after two. Other two-step
failures already have nonzero first-step error. The largest sharp-SL L1 error is
3.068e-4 (about **0.0307%**): 32³, double timestep, shift 0.25 or 0.5, two steps.
This is a counterexample to exact invariance, not evidence of a large visible error.

All four requested phases produced distinct initial sharp-density arrays at each
resolution. The box is point-sampled, so phase shifts can also change its represented
cell count and mass. Every error is normalized against that case's matched reference;
we do not claim equal discrete mass across phase variants. A cell-average initializer
would be a useful additional control if phase effects become part of the paper.

## Why this is a trustworthy negative result

- The largest density discrepancy between 4096 and 8192 iterations is 1.71e-11.
  The largest discrepancy between 8192 iterations and the independent divergence-free
  velocity control is 4.91e-11, far below the observed plateau failures.
- Pressure and velocity match bit for bit across density phases, shapes, methods,
  and step counts at a fixed resolution, timestep and iteration budget. Thus the
  density-phase tests change the transported field without changing the solve.
- All 24 matching unshifted one-step runs reproduce all eight exported fields of
  the original projection experiment bit for bit.
- All 16 production-versus-optimized pilot comparisons match all eight fields bit
  for bit, including both step counts. This compiler check covers the 32³ sharp-SL
  pilot, not every configuration in the full matrix.
- Seven perturbation-specific negative tests reject wrong phase/timestep/step-count
  metadata, a wrong initializer, a no-op second step, a corrupted reference, and a
  missing convergence control.
- Six existing static and periodic-translation configurations pass regression
  checks. No production advection or projection formula was changed in this follow-up.

## Consequence for the research direction

Do not build a stopping rule around the original exact-density plateau. At a fixed
resolution and timestep, the pressure residual and projected velocity are identical
across phases while transport error changes. A density-sensitive diagnostic is
still worth investigating, but this matrix does not show that density-weighted
divergence is the right predictor.

The next useful probe would record departure-coordinate differences and scalar
sampling differences between candidate and reference velocities, particularly at
the density boundary. That would distinguish changes in characteristic tracing
from changes in the sampled density response. Sampling precision and local field
structure are hypotheses here; this experiment does not isolate either mechanism.

Only after identifying that mechanism should we compare a cheap predictor with
ordinary residual thresholds on held-out fields and longer trajectories. Any
predictor must include its own GPU cost. A common transport reference is still
needed for claims about the best total advection/projection allocation.

## Artifacts

- `figures/sharp-sl-plateau-map.svg`: complete principal matrix, with exact zeros
  explicitly distinguished from small nonzero errors.
- `summary.csv` and `summary.json`: all 72 perturbation cases.
- `run-metrics.json`: per-run pressure, divergence, mass, and other diagnostics.
- `baseline-comparison.json`, `compiler-comparison.json`, and
  `reference-regression-summary.json`: validation evidence.
- `provenance.json`, `analysis-provenance.json`: job matrix and source/build hashes.

Raw fields and snapshotted sources remain in `diagnostics/runs/plateau-v1`;
the compiler pilot is in `diagnostics/runs/plateau-production-pilot`. Trial timings
are retained as diagnostics but are not performance results: this study used only
three reset trials per run, and independent numerical validation ran concurrently
with part of the sweep.
