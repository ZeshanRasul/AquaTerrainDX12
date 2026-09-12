# Final plateau test: error accumulates, but no new stopping rule is established

Completed 11 September 2026 on RTX 5090. The original sharp-density case was
transported for up to 120 steps (two seconds), with velocity projected once and
held fixed. Density evolves; this is not a fully evolving plume simulation.

## Measured sensitivity

Each path is compared to its own 8192-iteration reference. Values below are
normalized L1 expressed as percentages, not image errors.

| Grid | Steps | Hardware | Explicit float |
|---|---:|---:|---:|
| 32³ | 2 | 0% | 0.01273% |
| 32³ | 8 | 0.0000418% | 0.05087% |
| 32³ | 32 | 0.02308% | 0.19947% |
| 32³ | 120 | 0.19303% | 0.70809% |
| 64³ | 2 | 0.00110% | 0.01245% |
| 64³ | 8 | 0.02075% | 0.04926% |
| 64³ | 32 | 0.13537% | 0.19150% |
| 64³ | 120 | 0.43403% | 0.68049% |

The initial exact 32³ hardware plateau is gone by the eight-step checkpoint.
Sampling continues to affect measured sensitivity, but neither path remains
insensitive to the candidate/reference velocity difference at longer horizons.
The measurements do not establish the first step at which equality breaks.

The effect is not solely a shrinking-denominator artifact: at 120 steps, errors
normalized by initial mass are respectively 0.1663%/0.6115% (hardware/float at 32³)
and 0.4104%/0.6433% (64³).

The corresponding high-budget references retain approximately 86.2%/86.4% of
initial mass at 32³, and 94.6%/94.5% at 64³. This is a separate mass-persistence
observation in source-free, undamped transport. It neither proves a specific
cause nor establishes a new conservation correction, but suggests a practical
persistence question that is larger in magnitude than the pressure sensitivity
in this particular setup.

## Validation and limitations

64 launches, three reset trials each: 192 trials. All 32 hardware/float paired
configurations preserve identical projection fields and final midpoint/departure
coordinates. Final-two-step sampler/output, CPU interpolation, source-chain,
finite-value and repeat checks pass. All 16 two-step runs reproduce their prior
eight field buffers and two traces bitwise. Three late-trace corruption checks
and seven existing transport corruption checks reject invalid data. Release build
succeeded; the figure was rendered and inspected.

Only the final two traces are retained at each horizon; the complete earlier
step chain is not independently traced. Full final fields and repeated-trial
hashes are checked. No timings or visual comparisons are used as results.

The 64³ float reference-refinement difference reaches 7.98891e-6 normalized L1
at 120 steps; its independent-velocity-control difference is 5.77113e-6. These
exceed the original two-step 2e-6 density threshold and are reported explicitly,
not silently accepted under the earlier convergence criterion. They remain about
850 and 1180 times smaller than the measured 0.00680492 candidate/reference L1.
Thus reference agreement supports the scale of the measured effect but does not
make the long-horizon reference exact. The 32³ 4096/8192 results are identical;
its float independent-control difference at 120 steps is 6.80838e-7.

This experiment does not compare online stopping criteria, evolving velocity,
obstacles, buoyancy, rendering, or practical monitoring costs. It must not be
presented as the fully evolving pressure-budget study or evidence of a visually
meaningful defect.

## Recommendation

Close the plateau investigation at this point and prioritize coarse-obstacle
robustness. We have a replicated mechanism and an accumulation example, but no
demonstrated decision-making advantage over ordinary residual/divergence checks.
More plateau characterization would not resolve that contribution gap on its own.

For the obstacle direction, start by measuring a concrete failure in the corrected
solver: a plume meeting an axis-aligned wall, a thin/slanted wall, and a narrow
passage at coarse resolution, with source/boundary mass accounting. Establish
whether leakage, sticking or excess disappearance actually occurs before choosing
a remedy. Track persistence alongside leakage to avoid a correction that merely
keeps more smoke by trapping it incorrectly. These are proposed next tests, not
completed results.

Artifacts: [summary.json](summary.json), [validation.json](validation.json),
[figure.svg](figure.svg), [README.md](README.md). Final raw dataset and snapshots:
`diagnostics/runs/plateau-horizon-v2`. The excluded partial v1 dataset is documented
in the README. The runner's unused inherited Pilot parameter and default output
name were cleaned up after the final run; each recorded run retains its exact
runner snapshot and hash.
