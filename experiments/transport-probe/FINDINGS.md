# The plateau occurs in the scalar-sampling response

The probe establishes that the original density plateau does not mean the
departure coordinates have converged. Candidate and reference velocities trace
different departure points, but those differences can disappear in the hardware
sampling of the current density field. Changing the density field can expose them
without changing either set of departure coordinates.

## Direct measurements

Original sharp density, no shift, dt = 1/60:

| Grid / step | Cells with different departures | Cells with different hardware outputs | Hardware normalized L1 | Manual float-weight normalized L1 |
|---|---:|---:|---:|---:|
| 32³ / 1 | 32,768 / 32,768 | 0 | 0 | 6.362e-5 |
| 32³ / 2 | 32,768 / 32,768 | 0 | 0 | 6.371e-5 |
| 64³ / 1 | 262,096 / 262,144 | 0 | 0 | 6.243e-5 |
| 64³ / 2 | 262,096 / 262,144 | 80 | 1.103e-5 | 6.229e-5 |

The maximum departure-coordinate discrepancy is about 1.78e-4 cells at 32³ and
3.51e-4 cells at 64³. Candidate pressure budgets remain 512 and 2048 respectively;
the reference uses 8192 iterations.

The manual interpolation column compares diagnostic samples at the **same recorded
departures and source fields**, using float weights instead of a hardware texture
sample. It does not measure continuum error and does not represent a separately
evolved manual-interpolation simulation.

### Why unchanged output is possible

In the first 32³ step, 32,352 cells with changed departures have the same constant
source stencil in both runs. Their unchanged output is expected from the local
field structure. More revealingly, manual float interpolation responds in **374
cells** where the hardware output remains identical. At 64³ the analogous count
is **1,280 cells**. Thus constant source regions do not explain the entire plateau;
the hardware sampling response also suppresses changes visible to float-weight
interpolation.

For the unshifted 64³ case, both runs produce identical first-step density. Their
second-step input fields therefore match each other exactly. Within each run,
midpoints and departure coordinates are also identical between steps because
velocity is frozen. Nevertheless, the evolved input field exposes a difference
at **80 cells** on step two. This isolates a dependence on the current source
field, without needing a change in velocity, tracing, or projection budget.

## What the rounded-fraction model does and does not explain

Rounding interpolation fractions to multiples of 1/256 predicts zero candidate/
reference sample difference in the original first-step plateau cases. At 64³ on
step two, its common-source normalized difference is 1.100e-5, close to the observed
hardware difference of 1.103e-5.

However, that model does **not** reproduce individual hardware sample values:
the largest absolute mismatch across the tested cases is about **0.00391**.
It must not be presented as the device's exact interpolation rule. The probe
supports a sampling-precision explanation, but it does not isolate the internal
rounding, interpolation ordering, or effective weight precision responsible.

The shifted, double-timestep and smooth-field checks reproduce their previously
measured density errors. Their full tracing and sampling statistics are included
in `summary.csv` and `summary.json`.

## Validation

- **80 launches / 240 reset trials**, comprising 40 probe-enabled/disabled pairs.
- All eight simulation fields match between each pair, bit for bit.
- All trace records repeat identically across the three trials.
- Every recorded hardware sample matches its actual production SL output exactly,
  at both steps. Probe input chaining and the independent final readback also match.
- CPU reconstruction verifies both manual interpolation models and the stencil
  bounds. Coordinate arithmetic explicitly reproduces float32 subtraction.
- All 40 probe-enabled runs reproduce the eight fields from the previous plateau
  experiment; their captured first-step outputs also match that experiment.
- Departures and midpoints are unchanged between steps in every run.
- Reference density differences are at most 8.60e-12 between 4096 and 8192
  iterations, and 3.27e-11 against the independently initialized velocity control.
- Seven damaged-trace tests correctly reject inconsistent departures, hardware
  outputs, manual models, bounds, input chaining, and a missing second trace.

No production advection formula was changed. The probe is a separate shader using
the production tracing and sampling functions. Timings include probe dispatches
and copies and are explicitly excluded from performance conclusions.

## Consequence for the next experiment

The evidence is now strong enough to stop treating exact density equality as a
projection-convergence test. It also explains why a useful transport diagnostic
must account for the current source field, rather than only a velocity or residual norm.

Before designing a predictor, characterize the sampler directly with known corner
values and fractional-coordinate sweeps. A basis-stencil microbenchmark can test
which effective weights and rounding behavior reproduce the observed samples.
That would replace the incomplete 1/256 model with measured behavior. Cross-device
validation would be necessary before turning a finding on this RTX 5090 into a
general graphics recommendation.

This is a measured explanation of the observed plateau, not yet a new stopping
policy, faster solver, or publication claim.

## Artifacts

`figures/sampling-response.svg` presents the principal results. `summary.csv` and
`summary.json` contain all 20 candidate/reference step comparisons; `validation.json`
and `history-validation.json` hold the invariance checks. Provenance and source
snapshots accompany the raw fields under `diagnostics/runs/transport-probe-v1`.
