# E0/E1a accounting controls — registered before execution

12 September 2026. Supports [SPEC.md](SPEC.md); no new scientific hypothesis.
The mass-audit code and production shaders remain unchanged. A standalone DX12
driver dispatches the actual production kernel, then a sibling observer reads the
same inputs and records per-cell stage values. The observer never writes density.

Validate on 32³ and 64³, production debug/skip-optimization and optimized O3,
three repeats each. No timings from these instrumented launches. Record all inputs,
constants, hashes, raw outputs, observations, device and D3D12 validation messages.

Ordered nodes are input, sampled/base, solid-cleared, top-rejected, corrected,
limited, damped and floored. Subtract adjacent stored float32 nodes in float64.
Append the actual-output minus observed-final difference explicitly as numerical
closure error; never silently attribute it to interior loss. Require per-cell
closure <=5e-7 times max(1, absolute stage values), and summed absolute closure
<=1e-7 times max(1, sum absolute actual output, sum absolute observed output).
These allow a few float32 rounding units but must be reported, including exact
bitwise equality when achieved. Repeat outputs and traces must match bitwise.

The sampling node for SL/raw is a hardware sample using the production helper.
It is a declared counterfactual at a solid destination, where production exits
before sampling. Solid clearing precedes top rejection; the latter is inactive
on solids. For MC combine, the base node is the supplied forward result, not a
new sample of the original field. Correction/limiting are inactive on rejected
destinations. Final kernels damp before flooring. Raw forward/reverse passes
perform neither top rejection, damping nor flooring. Record those separately.
The total full-MC budget is the final-combine chain from original input to final
output; do not add the forward and reverse pass budgets to it a second time.

Controls and independent expected behavior:

- Identity: constant positive field, zero velocity; SL and complete MC preserve it.
- Periodic half-cell translation: smooth Gaussian, constant velocity; SL conserves
  the discrete sum to float32 tolerance; full MC uses actual raw forward/reverse
  outputs. Periodic departures must not cause rejection.
- Damping: constant one, zero velocity, rate 0.2, dt 1; final value exp(-0.2).
- Positivity: constant -0.25 (deliberately nonphysical fixture), zero velocity;
  SL floors to zero and the signed floor increment is +0.25 per cell.
- Top exit: constant one, downward velocity -0.0625, dt 1; backtracing exits at
  the top in exactly N/16 layers. SL removes those layers. Raw retains them.
- Solid clearing: constant one, stationary centred sphere radius 0.2; compare
  the solid mask and cleared mass against independent CPU cell-centre geometry.
- MC clamp fixtures: density=hat=1 and bar=-3 or +5. Corrected values 3 or -1
  clamp to 1; this deliberately tests combine accounting, not realizable flow.

No wall-clamping mass-loss claim follows from boundary-stencil counts. No result
here establishes original plateau-horizon loss attribution or a coupled-flow G1
effect. A failed observer/control blocks interpretation; fix the measurement first.

Follow-up branch coverage registered after the base suite, before edge-suite runs:
repeat half-cell translation in -y with periodic wrapping to exercise the top seam;
run damping, negativity, top-exit and solid-destination fixtures through MC combine
as well as SL. These fill previously unexercised observer branches, not a new
research sweep. Same thresholds, resolutions, two compiler modes and three repeats.

Historical replay registration, after both control suites pass: use the archived
120-step hardware-SL reference at 8192 projection iterations, N=32 and 64. Load
its exact initial density and projected U/V/W textures, dt=1/60, zero damping,
no solids and nonperiodic addressing; do not rerun or alter the pressure solve.
Record all 120 steps, three reset repeats of each update; chain the actual output.
Require bitwise matches to the archived step-2 and step-120 final fields, and to
the archived step-119/120 input/output traces and departure coordinates. Check
the same closure tolerances at every cell/step. If replay differs, stop attribution.
Report all signed stage budgets and counts; a sum grouped by boundary-stencil
destinations is a spatial partition, not a causal wall-loss counterfactual.
