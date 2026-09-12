# Full-duration controls: blocked before opacity

12 September 2026. Root: `runs/full-reference-controls-v1`.
The first requested run, 180 steps / 32³ / scene A / pressure32, terminated at
the step-43 handoff. No complete 180-step or 360-step run was obtained. The
matrix stopped under the [registered extension](FULL_DURATION_REFERENCE_SPEC.md).
No opacity rendering, refinement scoring or G1 scoring was performed.

## Execution blocker

The process returned code 3 with a caught standard exception, `Offline response
step mismatch`. Lifecycle records show 42 submitted steps and the pre-projection
fence for step 43 completed with device status S_OK. The retained request JSON
contains step 43 and `response.ready` contains exactly the bytes `43`.

This does not establish a wrong-step CPU answer. The current C++ reader initializes
the reply to zero and uses the same mismatch error for an unsuccessful open/read
or a different decoded value. Those cases cannot be distinguished from the saved
message. A transient file access failure is possible but unproven. No further
runtime repair or rerun was performed; the previously allowed integration repair
had already been used. Pinned source/executable hashes were verified unchanged
after this failure, before updating the central plan.

## Separate evidence in the completed fields

A read-only diagnostic inspected the 42 complete pre/post field sets. These lack
a completed normal-return/provenance seal and the final compact-metric CSV, so
they cannot pass the registered validator. Their input hashes and reproducible
analysis are retained in `interrupted-diagnostic.json` under the run root.

All 42 CPU float32 projection replays match the captured GPU velocities bit-for-bit.
The largest reconstructed relative divergence is 1.301690e-6; the largest
dt-scaled maximum divergence is 7.823111e-9. These values are below both numerical
acceptance levels in this partial trajectory.

However, the difference between the ideal pressure-residual ratio and the ratio
of captured divergence first exceeds the fixed 5e-7 identity tolerance at step 22:

| Quantity | Step 22 | Step 37 (largest ratio difference) |
|---|---:|---:|
| Captured divergence ratio, CPU recomputation | 7.404339e-7 | 1.301690e-6 |
| Ideal-identity ratio difference | 5.243218e-7 | 1.061155e-6 |
| Ideal-identity RMS discrepancy | 2.088916e-9 | 6.071798e-9 |
| Identity RMS after explicitly accounting for captured arithmetic rounding | 3.632124e-19 | 7.610000e-18 |

The last row is diagnostic attribution, not a revised passing criterion. It uses
the actual pre-velocity divergence and separately measures the difference between
float64 gradient subtraction and the bitwise-verified float32 subtraction. Thus
the observed discrepancy is explained by pre-divergence/projection rounding in
these states. It is not evidence that the pressure equation is poorly solved.

This limitation was already possible in the ideal primary-mode identity: it
compares `div_before - dt * Laplacian(p32)` with the stored projected velocity's
divergence. The refined mode explicitly accounts for storage rounding, whereas
the existing primary check does not account for all its arithmetic rounding.
That check passed the short replay but is not scale-independent as the flow
evolves. Its tolerance was not changed or reinterpreted to pass this run.

## Decision

Full-duration numerical validity remains blocked; opacity refinement and G1 are
unmeasured. The previous short-replay pass remains limited to twelve steps.
The appearance hypothesis has not been falsified or confirmed.

Any continuation requires an explicit prospective decision covering both the
file-handoff contract and the primary identity's rounding accounting, retaining
this failed attempt. Otherwise take the named finite F1 path. No additional
pressure budgets, solver family, favorable frames or acceptance-threshold sweep
are justified by these data.

Reproduce the partial diagnostic (it never marks a gate passed):

```powershell
python diagnostics/diagnose_interrupted_reference.py experiments/appearance-attribution/runs/full-reference-controls-v1/s180-n032-A-pressure32 --output experiments/appearance-attribution/runs/full-reference-controls-v1/interrupted-diagnostic.json
```
