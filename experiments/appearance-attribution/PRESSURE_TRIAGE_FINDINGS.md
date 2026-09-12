# Same-state triage: reference feasibility is supported

12 September 2026. Four prospective captures completed under
[triage v1](PRESSURE_TRIAGE_SPEC.md), plus a capture-disabled control. Raw root:
`runs/pressure-triage-v1`. Machine-readable report:
[pressure-triage-results.json](pressure-triage-results.json).

## Decision

Proceed with option 1's bounded offline-reference feasibility phase, reviewed by
19 September, with option 3/F1 as the named fallback. The tested 128³ states
support incomplete Jacobi convergence as the dominant error at the registered
cap. A more accurate solution of the same closed-box operator improves their
projection sufficiently even with float32 pressure and velocity storage.

The old pressure-selection-v1_3 result remains blocked. These counterfactual
single-state CPU projections do not constitute a new coupled trajectory or a G0
pass, and no appearance/G1 output was scored. This is not a paper contribution.

## Measurements

The pressure checkpoints within each row share the exact captured RHS and
pre-projection velocity; this is not a comparison of evolving trajectories.
Residuals are recomputed from stored fields using an explicit float64 stencil.

| Grid / scene / step | Residual ratio at 32768 | Residual ratio at 65536 | CPU reference, pressure and projection in float32: divergence ratio |
|---|---:|---:|---:|
| 32³ / A / 1 | 5.715e-7 | 5.715e-7 | 2.418e-7 |
| 128³ / A / 1 | 1.960e-4 | 2.132e-5 | 1.776e-6 |
| 128³ / B / 1 | 2.771e-4 | 2.985e-5 | 1.915e-6 |
| 128³ / A / 114 | 2.828e-4 | 3.041e-5 | 2.842e-6 |

The 32³ control has flattened below the tightened 1e-5 limit. The three 128³
captures still improve about ninefold over the final interval. Thus a plateau
at 32³ must not be extrapolated to the failing 128³ states.

All four reference/float32 projections also satisfy the tightened absolute limit:
the largest `dt * max(abs(divergence))` is 1.3983e-7, below 1e-6. At the evolved
state, rounding the otherwise float64 projected velocity to float32 raises the
ratio from 7.19e-8 to 2.21e-6: storage precision matters, but it does not explain
the observed 3.05e-5 failure there. No claim is made about all later reference
states or a universal precision floor.

## Controls and qualifications

- Release build and all five existing run validations passed. Source schedules,
  raw fields and provenance were checked; no D3D12 errors/discarded messages.
- All captured runs reproduce the original v1_3 physical/non-timing trajectory
  prefixes exactly (2,2,2,114 rows), excluding run-configuration identity. Every
  available matching registered snapshot is byte-identical (13 comparisons).
  The disabled/enabled 32³ pair has seven identical raw float files.
- The float32 CPU gradient/subtraction replay reproduces all captured GPU U/V/W
  faces bit-for-bit in all four states, with exactly closed normal boundaries.
- Pre/post divergence reconstruction, compact reduction and pressure/divergence
  identity checks pass the existing full-field tolerances. The largest difference
  between direct residual ratio and compact divergence ratio is 7.751e-8, below
  the registered 5e-7 identity tolerance.
- The direct cosine-basis solve is specific to this obstacle-free closed box.
  Nine manufactured constant/mode/random controls at sizes 4,8,32 pass an
  independent explicit-stencil residual and pressure-reconstruction check.
  The solve reports the incompatible mean separately and fixes the pressure
  gauge. Its residual is below 1e-9 relative to the mean-free RHS for all captures.
- Both ideal and float32-rounded shader RHS residuals are reported. A CPU/GPU
  disagreement alone was not used to assign causality. Instrumented timing is
  excluded from performance claims.

## Next bounded deliverable

Integrate one offline reference path for the same discrete closed-box projection,
using the validated direct solve rather than assuming PCG is necessary. First
verify pressure upload and GPU application against these captured-state controls;
then validate a short coupled replay under a new prospective reference protocol.
The protocol must specify a distinct refinement/independent accuracy control,
fresh trajectories and retained pressure/opacity-sensitivity criteria before any
full G1 scoring. The old iteration-based selection cannot simply be relabelled
as passed by a direct solve.

No production pressure algorithm, threshold or iteration cap was changed. A
reference implementation is an offline accuracy tool; practical-runtime transfer
and the appearance effect remain untested. If reference feasibility fails within
the week, proceed to the finite F1 planning gate rather than another solver branch.

Reproduce the report after completing the four specified captures and off control:

```powershell
python diagnostics/analyze_pressure_triage.py --root experiments/appearance-attribution/runs/pressure-triage-v1 --original experiments/appearance-attribution/runs/pressure-selection-v1_3 --output experiments/appearance-attribution/pressure-triage-results.json
```
