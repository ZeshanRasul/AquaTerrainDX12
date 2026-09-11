# Direct GPU smoke mass-budget audit

**Result (11 September 2026): the audit found and fixed a MacCormack descriptor-table ordering bug.**
After the fix, both 480-step audits pass all instrumentation checks, and every
end-of-step density hash matches an uninstrumented control run. The remaining
density excess is real in these simulations, but its numerical mechanism is not
fully isolated. These are correctness runs, not performance or accuracy benchmarks.

## What changed the interpretation

The descriptor heap grouped the MacCormack scratch textures as:

```
density_hat, density_bar, temperature_hat, temperature_bar
```

The scalar pairs (`u0/u1`, `t0/t1`) and combined `t7..t10` table require:

```
density_hat, temperature_hat, density_bar, temperature_bar
```

This mismatch affected both ping-pong sets. A forward temperature write targeted
the density-bar resource. The reverse pass also bound the wrong temperature
input/output resources, with unintended read/write aliasing. The final correction
read temperature data through the density-bar register. This was an implementation
defect, not a valid test of the published MacCormack method.

The heap ordering is now corrected for resources and both SRV/UAV handle arrays.
Runtime adjacency checks guard the required layout. The limiter formulas,
backtrace, pressure solve, physical parameters, and shader optimization flags
were not changed by this audit.

The initial known-field probe failed with the old layout. Its output is retained
in `data/initial-binding-failure/` as diagnostic evidence only: it does **not** pass
the audit and is not a validated transport benchmark. The initial instrumented
run still reproduced the historical emitter-off density sum, 2460.7904.

## Matched results

All runs use 32³ cells in a unit cube, dt = 1/60 s, 480 steps, source enabled for
steps 1–240, open top, no obstacle, no confinement, 40 weighted Jacobi iterations,
buoyancy 0.6, smoke weight 0.05, density dissipation 0.1/s, and cooling 0.5/s.
The source adds 30 density/s and 10 temperature/s to one cell. Thus the nominal
total injected **density sum** is 120. Multiply sums by 1/32768 to obtain volume
integrals. Small deviations in measured injection reflect float arithmetic.

| Density sum | Historical SL | Historical MacCormack | Corrected SL | Corrected MacCormack, clamp |
|---|---:|---:|---:|---:|
| Last emitting step, 240 | 215.5240 | 2460.7904 | 215.5240 | 196.2477 |
| Ratio to 120 injected | 1.7960 | 20.5066 | 1.7960 | 1.6354 |
| Step 480 | — | — | 28.4113 | 12.0829 |

The approximately 20× result is not evidence that a correctly implemented clamp
produces that excess. The corrected MacCormack run also no longer has the
historical catastrophic late density upturn in this 480-step case. This does not
prove stability for longer runs, or physical/visual accuracy.

## Direct budget through step 240

These are cumulative changes measured from actual intermediate GPU fields, not
an assumed injection term subtracted from an end-of-step CSV.

| Measured contribution | SL | MacCormack, clamp |
|---|---:|---:|
| Injection | +120.000002 | +120.000045 |
| Forward transport | +133.215324 | +152.581624 |
| Correction, including final boundary rejection | n/a | −246.670391 |
| Lower clamp changes | 0 | +268.978392 |
| Upper clamp changes | 0 | −52.160650 |
| Damping and positivity floor | −37.691320 | −46.481285 |
| Final density sum | **215.524006** | **196.247734** |

For MacCormack, the limiter adds a net +216.817741 over this interval, while the
correction plus limiter together contribute a net −29.852649 relative to the
forward field. It is therefore misleading to equate the limiter's positive
contribution with the complete method's excess. The forward fields also differ
between SL and MacCormack because temperature and density feed back into velocity.

`forward_delta` includes transport and the boundary behavior of that kernel.
`correction_delta` includes the final kernel's solid/open-boundary rejection.
These are **not** independent measurements of physical boundary flux. The reverse
field is logged as an intermediate, not as another physical timestep.

## Instrumentation and checks

Audit mode copies density immediately before injection, after injection, before
scalar advection, after the forward pass, after the reverse pass (MacCormack),
and after the final scalar kernel. It records corrected and limited values inside
the actual scalar kernel using an audit-only shader compilation define. Signed
lower/upper limiter changes and counts of changed cells are recorded separately.
The SL variant records its raw transport value before damping.

Full texture snapshots are summed on the CPU in double precision. A per-cell
float4 buffer records limiter stages; it is read only after the corresponding
frame-resource fence completes. Snapshots restore the resource's original state.
The ordinary shader variants contain no audit writes or extra dispatches.

Both audited runs passed all 480 steps:

- Known constant field, varied by step, matches **every texel**.
- Known 7.25 impulse, moved in x/y/z each step, matches **every texel**.
- These checks exercise both scalar ping-pong sets, padded rows/slices, the same
  nonzero copy offset as ordinary readback, and step association.
- Source injection changes only the selected cell. Its expected value allows
  float multiply/add versus fused multiply-add rounding: 2e-7 × max(1, expected
  source value). Every other cell must match exactly.
- Post-injection and pre-advection density match exactly at every cell.
- An independent final texture copy matches the ordinary readback sum and
  FNV-1a hash of all float bytes, ignoring row padding.
- Damping and limiter reconstruction residuals pass a tolerance of
  2e-6 × max(1, absolute final/limited/corrected sums). This is an arithmetic
  consistency threshold, **not** an acceptable conservation error.
- No nonfinite recorded values.
- Sequential pre-source sums match the previous step's final sum exactly.
- For each method, all 480 final density hashes, kinetic energies, and RMS
  divergence values match the corresponding run with probes disabled.

Maximum absolute damping reconstruction residual was 4.91e-6 across both audited
runs. Maximum limiter residual was 4.97e-11. The cumulative budget closes to
within 1.81e-10 through step 480. Those tiny residuals validate the accounting;
they do not make the transport conservative.

## What is established, and what remains open

| Suspect | Status |
|---|---|
| MacCormack scratch resource bindings | Defect found, fixed, and guarded by layout checks plus runtime known-field tests |
| Injection or source schedule | No unexplained contribution in these runs; measured directly |
| CPU summation/readback | Known-field and independent final-copy checks pass |
| Intermediate velocity stages directly writing density | Excluded in these runs by exact before/after field comparison |
| Instrumentation changing the trajectory | No observed change: audited/control hashes and recorded velocity diagnostics match |
| Clamp as the sole cause of historical 20× excess | Unsupported; historical run used incorrect resource bindings |
| Residual non-conservation | Confirmed for both corrected paths during emission |
| Boundary flux, projection error, interpolation, feedback contributions | Still need controlled transport tests and targeted ablations |

The next task remains the prescribed-velocity passive-advection harness. It
should use this corrected implementation and separate transport error from
buoyancy feedback and projection. A new limiter is not justified by the old
coupled-run headline.

The earlier advection and confinement experiment interpretations must be
revisited. Both used the affected MacCormack path; their timings describe the
historical implementation, not the cost of a validated corrected method.

## Reproduce

Build in an x64 Visual Studio developer shell; reconfigure to discover the new
`SmokeMassAudit.cpp` source in the project's CMake glob:

```powershell
cmake -S . -B out/build/x64-Release
cmake --build out/build/x64-Release --config Release
./diagnostics/Run-SmokeMassAudit.ps1 -OutputRoot ./diagnostics/runs/my-mass-audit
./diagnostics/Analyze-SmokeMassAudit.ps1 -RunRoot ./diagnostics/runs/my-mass-audit
```

The runner requires a fresh output directory per run, launches hidden application
instances, and exits on incomplete runs, failed checks, or control mismatches.
It saves source/executable/runtime-shader SHA256 hashes and the Git base revision.
Current uncommitted source changes are identified by hashes; the Git revision
alone is insufficient to reproduce this workspace state. Build before running.

The GPU panel also has **Start mass-budget audit (480 steps)**, using the selected
advection/limiter and the canonical scenario. Command-line automation uses
`AQUA_SMOKE_AUDIT=sl|maccormack`, `AQUA_SMOKE_AUDIT_OUTPUT=<directory>` and
`AQUA_SMOKE_AUDIT_PROBES=0` for the uninstrumented control. Automation uses clamp.

Verified artifacts are under `data/verified/`: `budget.csv` (audit only),
`end-fields.csv`, `manifest.json`, `provenance.json`, and aggregate `summary.json`.
Control runs intentionally have a header-only budget CSV. No solver timing
columns are exported. Hardware: NVIDIA GeForce RTX 5090. CPU build: Release.
Shaders retain the existing STRICTNESS | DEBUG | SKIP_OPTIMIZATION flags.
