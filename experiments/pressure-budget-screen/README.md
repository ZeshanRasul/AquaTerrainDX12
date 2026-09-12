# Pressure-budget selection: initial decision screen

This is an offline reanalysis of the validated `projection-v1` sweep, not a new
GPU experiment or an implemented stopping controller. It establishes what the
simple criteria already achieve before investing in a transport-aware rule.

Four criteria were compared: relative pressure residual, relative RMS divergence,
dt times maximum divergence, and dt times density-weighted RMS divergence.
Thresholds were calibrated on both density shapes and advection methods at 32³,
then applied to 64³. Calibration uses a fixed logarithmic threshold grid, choosing
the least summed iteration count that satisfies all calibration cases; ties favor
the looser threshold. This is exploratory resolution transfer within the same
manufactured velocity family, not independent-scene validation.

The available candidate budgets are 0,8,32,128,512,2048. Error is one-step normalized
L1 to the same method at 8192 iterations, with the prior reference checks retained.
Tolerance values 1%,0.1%,0.01% are exploratory numerical tolerances, not established
visual acceptability thresholds.

| Tolerance | Selected 64³ budget, every criterion | Transfer cases passing | Smallest tested budget meeting tolerance in hindsight |
|---|---:|---:|---:|
| 1% | 512 | 4/4 | 512 |
| 0.1% | 2048 | 4/4 | 2048 |
| 0.01% | 2048 | 4/4 | 2048 |

All criteria make identical budget choices in this screen. Relative pressure
residual and relative RMS divergence differ by at most 5.07e-8, consistent with
the already verified discrete pressure/divergence identity. Density weighting
does not improve budget selection on these cases. No actual online monitoring
cost, GPU time savings, stronger solver comparison, or longer trajectory benefit
has been measured. Sparse budgets also conceal any benefit between checkpoints.

## Decision for the next round

There is currently no evidence that a new transport-aware stopping criterion
beats the ordinary baselines. The manufactured plateau experiments should not
be expanded solely to seek such evidence.

The next bounded GPU round should introduce evolving velocity and practical
operating budgets, using a rising plume and interacting plumes first; coarse
obstacle interaction is the bridge to the alternative research direction.
Compare fixed-budget trajectories and candidate checkpoint metrics against
refined-reference trajectories. Use short-horizon comparisons as well as longer
ones, since trajectory divergence alone is not evidence of worse visual quality.
Report density, velocity, mass, rendered effects, and uninstrumented costs; any
eventual adaptive controller must include its monitoring overhead. Ordinary
residual/divergence baselines are required, and a stronger pressure solver is
required before a practical performance claim.

Continue pressure selection only if a cheap additional measurement makes a
useful, reproducible difference to budget decisions or missed failures on cases
outside its calibration set. Otherwise preserve the sampler findings as supporting
material and move to coarse-obstacle robustness or resolution/time-step-consistent
smoke persistence. Do not claim this offline screen settles those evolving-flow
questions. No new evolving-flow harness was implemented in this screen.

Reproduce: `python diagnostics/screen_pressure_budgets.py`.
`screen.json` includes every selected threshold, case result and input/script hashes.
