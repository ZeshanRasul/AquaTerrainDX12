# RTX 3070 Laptop GPU: sampler response reproduces bitwise

Validated 11 September 2026 from `results-20260911-221451.zip` supplied by the user.
All nine output buffers (three sizes, three repetitions) match the corresponding
RTX 5090 baseline buffer byte for byte, including coordinate diagnostics and all
16 texture outputs.

| Texture size | Queries per repetition | Repetitions | Changed texture samples vs 5090 | Maximum absolute difference |
|---|---:|---:|---:|---:|
| 32³ | 23,503 | 3 | 0 | 0 |
| 33³ | 23,503 | 3 | 0 | 0 |
| 64³ | 23,503 | 3 | 0 | 0 |

This covers 70,509 query positions across the sizes, or 211,527 query evaluations
with repeats, totaling 3,384,432 texture samples. Repetitions are dispatches in the
same process at each size, not separate driver/device initializations.

## Validation and provenance

- Reported device: NVIDIA GeForce RTX 3070 Laptop GPU; raw driver version
  9007199255730126. Baseline: RTX 5090, raw driver version 9007199255733668.
- All 21 returned payload files pass their recorded SHA-256 checks. The returned
  input-hash manifest matches the original package and its checked files.
- Returned query definitions, normalized coordinates, corner values and runner
  match the supplied package/baseline. The runner checks executable and shader
  hashes before dispatch; those binaries are not themselves in the returned ZIP.
- Independent laptop analysis passes repeatability, finite-value, exact-corner,
  constant-preservation, and CPU/GPU coordinate-reconstruction checks.
- Basis weights lie exactly on the 1/256 lattice and sum exactly to one. Maximum
  failure of separability is 0.0052318572998046875, as on the 5090.
- The previous sentinel again yields 0.171875 rather than the independently
  rounded-fraction product 0.1737213134765625. Generic [0,1] pattern reconstruction
  from measured basis weights agrees within 2.9802322387695312e-8.
- The same N=33 threshold/model limitation also reproduces. Its selected `float`
  label is a tied minimax result among inadequate models, not full-precision
  sampler behavior.

Raw extraction: `diagnostics/runs/sampler-rtx3070-v1/results-20260911-221451`.
Baseline: `diagnostics/runs/sampler-v2`. Original package:
`diagnostics/runs/rtx3070-sampler-package`.

[comparison.json](comparison.json) records checks and per-size hashes;
[summary.json](summary.json) contains the independent laptop analysis.
`provenance.json` hashes the supplied archive, analyzers and reports.

## Implication for the paper and next experiment

We can now state that this measured sampler response was reproduced bitwise on
two tested NVIDIA GPU/driver configurations. This extends the evidence beyond
the original device, without establishing behavior for all NVIDIA hardware,
other vendors, formats, filters, or drivers. Device and driver differ together;
this experiment does not separate their effects.

The laptop has not yet reproduced the pressure/density plateau: this package
contains only the isolated sampler test. The next useful validation is the actual
density-sampling ablation at 32³ and 64³ on the laptop: baseline sharp density,
zero shift, dt=1/60, two steps, hardware/float sampling, candidate budgets 512/2048,
4096/8192 references and the independent velocity control, three trials each.
That is 16 launches. Validate each device against its own references and check
projection/departure invariance across sampling paths within each device. Treat
cross-device numerical agreement as a measured outcome, never a validity gate.

The practical question remains whether hardware output equality hides pressure
sensitivity on the second device, and whether explicit interpolation exposes it.
Sampler replication strengthens that hypothesis but does not replace the solver
experiment or establish a publishable stopping policy.
