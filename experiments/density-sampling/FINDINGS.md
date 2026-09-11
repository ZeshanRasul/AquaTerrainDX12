# Density-sampling ablation: the exact plateau disappears

Completed 11 September 2026 on the RTX 5090. Replacing only final density sampling
with explicit float interpolation removes every exact candidate/reference density
plateau in this test set. The projection fields and midpoint/departure coordinates
remain bitwise identical between the two sampling paths. This is a controlled
intervention supporting sampler dependence of the observed plateau.

## Original sharp-density configuration

Normalized L1 compares each path with its own 8192-iteration reference. Candidate
budgets are 512 iterations at 32³ and 2048 at 64³; dt is 1/60 and phase shift zero.

| Grid | Step | Hardware changed cells | Hardware L1 | Float changed cells | Float L1 |
|---|---:|---:|---:|---:|---:|
| 32³ | 1 | 0 | 0 | 374 | 6.36151e-5 |
| 32³ | 2 | 0 | 0 | 836 | 1.27299e-4 |
| 64³ | 1 | 0 | 0 | 1280 | 6.24274e-5 |
| 64³ | 2 | 80 | 1.10311e-5 | 2696 | 1.24519e-4 |

This confirms that the manual probe's first-step response survives as an actual
density update and propagates into the second step. It is no longer just a
counterfactual interpolation of the unchanged hardware source field.

## Perturbations and scale of the effect

Across ten cases and two steps, hardware gives five exact-zero differences;
float interpolation gives none. Cases cover both grids, a sharp phase shift,
doubled dt, half dt with a half-cell shift, and smooth density. All five hardware
plateaus disappear with float sampling. This is the selected ten-case transport
subset, not a rerun of every earlier plateau-sweep configuration.

The float path's normalized L1 spans 2.96904e-5 to 2.51703e-4 (approximately
0.003%–0.025%). Differences are small. Hardware sensitivity is not uniformly
smaller than float sensitivity: some phase/time-step cases reverse that relation.
Nothing here establishes that the replacement is more accurate against an exact
transport solution or should be used in production.

The largest 4096-versus-8192 reference L1 is 4.72572e-7; the largest difference
from the independently constructed divergence-free velocity control is 2.48818e-7.
Thus references are close at the measured scale but not identical, particularly
under explicit interpolation. The smallest candidate float difference is more
than 60 times the largest reference-refinement difference across the set.

Candidate density extrema stay within [0,1]. Relative mass change spans roughly
-0.392% to -0.0176% across all recorded candidate paths/steps. These are measured
mass changes, not a conservation result or proof of physical accuracy.

## Verification and artifacts

- 80 full-run launches, three trials each: 240 trials with two scalar steps.
  The separate eight-launch pilot preceded the full run.
- All 40 hardware/float pairs preserve initial density, pressure, divergence
  before/after projection, and all three velocity components bitwise.
- All midpoint/departure coordinates agree across sampling paths at both steps.
  Frozen departures also agree between steps.
- Selected probe samples match actual updates bitwise. The second-step input is
  the actual first-step output. Float interpolation and stencil bounds agree
  with independent CPU checks. Repeated field/trace hashes and finite checks pass.
- All 40 hardware runs reproduce the prior transport-probe experiment's eight
  fields and both traces bitwise.
- Six ablation corruption tests and seven existing transport-probe corruption
  tests reject their invalid inputs. Release build and whitespace checks pass.
- The standalone figure was rendered and visually inspected. No timing claim
  is made; probes and readbacks are inside the measured intervals.

[summary.json](summary.json), [summary.csv](summary.csv), [validation.json](validation.json),
and [figure](figures/density-sampling.svg) contain the measurements. Raw data and
input snapshots are in `diagnostics/runs/density-sampling-v1`; provenance is copied
alongside this report.

## Interpretation and next validation

Exact output equality at a pressure budget can be a property of the density
sampling path rather than convergence of the projected velocity. Combined with
the isolated sampler measurements, this supports rejecting exact density equality
as a stand-alone convergence certificate. It does not yet define a better stopping
rule or establish a new numerical method.

The RTX 3070 laptop is useful **now**. Start with the packaged sampler test in
[README.md](README.md): it uses identical executable/shader/query inputs and
records adapter/driver identity. Validate its own invariants first, then compare
effective weights with the 5090. After those results, replicate the baseline
32³/64³ density-sampling intervention on the laptop. Two devices can test whether
this mechanism and its practical consequence recur, without assuming exact
cross-device equality or universal behavior.

The paper direction is a reproducible diagnostic for sampling-dependent apparent
convergence. Establish cross-device behavior before expanding this into a practical
budget-selection recommendation; any recommendation will need tolerances and
validation beyond two frozen-velocity steps.
