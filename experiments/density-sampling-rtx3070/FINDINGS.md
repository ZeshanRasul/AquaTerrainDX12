# RTX 3070 Laptop GPU: density-sampling ablation replicates

The full laptop run reproduces the practical result: all five exact hardware
density plateaus disappear when final density interpolation alone uses explicit
float arithmetic. This extends the result from an isolated sampler measurement
to actual one- and two-step density updates on a second GPU/driver configuration.

## Original sharp configuration on the laptop

Zero phase shift, dt=1/60; candidate budgets 512 at 32³ and 2048 at 64³.
Each sampling path is compared with its own laptop 8192-iteration reference.

| Grid | Step | Hardware changed cells | Hardware normalized L1 | Float changed cells | Float normalized L1 |
|---|---:|---:|---:|---:|---:|
| 32³ | 1 | 0 | 0 | 374 | 6.36193e-5 |
| 32³ | 2 | 0 | 0 | 836 | 1.27308e-4 |
| 64³ | 1 | 0 | 0 | 1280 | 6.24281e-5 |
| 64³ | 2 | 80 | 1.10311e-5 | 2696 | 1.24518e-4 |

Across ten configurations and two steps, the hardware path has the same five
exact-zero comparisons as the RTX 5090; the float path has none. Float normalized
L1 ranges from 2.97037e-5 to 2.51712e-4, approximately 0.003%–0.025%. This remains
a small sensitivity effect; no visual significance or improvement in total
advection accuracy has been established.

## Independent validation

All 80 launches, 240 trials and 40 hardware/float pairs pass the existing analyzer
without using RTX 5090 output equality as a validity requirement. The laptop's
initial density, pressure, divergence and velocity fields remain bitwise identical
between sampling paths. Midpoints/departures remain bitwise identical between
paths and frozen between steps. Selected probe samples match actual outputs;
second-step inputs match first-step outputs. Finite-value, repeatability,
closed-wall, CPU interpolation/stencil, pressure/divergence identity and reference
checks pass.

Maximum laptop normalized L1 between the 4096 and 8192 reference solves is
4.26419e-7; maximum difference from the independently constructed divergence-free
velocity control is 2.62898e-7. Both are below the candidate differences exposed
by float sampling.

## Cross-device comparison and provenance limits

All manifests identify NVIDIA GeForce RTX 3070 Laptop GPU, raw driver version
9007199255730126. RTX 5090 baseline driver version is 9007199255733668.
All 14 supplied source snapshots verify against their laptop provenance hashes.
They match the baseline source text after normalizing newlines and optional UTF-8
BOM; 13 differ at the byte/hash level. The recorded executable hashes also differ.
This is therefore a replication with equivalent recorded source text, not a
controlled same-binary device-only comparison. The archive does not include the
executable itself, and the source snapshot is not a complete build-environment
manifest. Hardware, driver and build effects cannot be separated here.

Unlike the isolated sampler outputs, the complete solver outputs are not bitwise
identical across devices:

- Initial density matches in all 80 launches.
- Pressure and each velocity component match in 20/80 launches. Maximum pressure
  difference is 3.81470e-6; maximum componentwise velocity difference is 1.14366e-6.
- Final density matches in 47/80 launches; maximum absolute density difference
  across all corresponding outputs is 2.57842e-4.
- The largest absolute difference between corresponding normalized-L1 sensitivity
  metrics is 1.14241e-7. That is a difference between scalar metrics, not a norm
  of the cross-device density-field difference.

The qualitative conclusion reproduces despite these small numerical differences.
The prior isolated sampler equality should not be extrapolated to whole-solver
bitwise reproducibility.

## Research consequence

We now have a controlled sampling intervention replicated on two tested GPU/driver
configurations: exact density equality can hide remaining projection sensitivity,
and explicit interpolation exposes it without altering the projected velocity or
departure coordinates within each run. This supports the proposed practical
diagnostic. It does not establish universal sampler behavior, physical convergence,
a visual-error threshold, or a new pressure stopping policy.

The next scientific question is whether this distinction matters over longer,
evolving flows at a meaningful error tolerance. A useful next experiment would
compare pressure budgets against a high-budget trajectory over many steps, with
both sampling paths and controlled sources/forces, recording density/velocity
error, mass and rendered differences. That work should test whether the effect
accumulates, dissipates, or remains practically negligible before proposing a
stopping rule.

## Reproduce the analysis

```powershell
python diagnostics/analyze_density_sampling.py diagnostics/runs/density-sampling-rtx3070/density-sampling-v1 --out experiments/density-sampling-rtx3070
python diagnostics/compare_density_devices.py
```

The second script targets the recorded laptop and desktop directories.
`summary.json` / `summary.csv` contain laptop metrics; `validation.json` records
within-device checks; `comparison.json` records source checks, field differences
and metric comparisons. Original archive:
`experiments/density-sampling/rtx3070-density-sampling.zip`. Raw extracted data:
`diagnostics/runs/density-sampling-rtx3070/density-sampling-v1`.
