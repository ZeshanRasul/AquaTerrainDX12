# Coupled pressure controls

Protocol v1.0, 12 September 2026. This document is prospective: no coupled-scene
output was inspected while choosing its run length, source schedule, pressure
budgets or decisions. It is incorporated by protocol v1.2 of [SPEC.md](SPEC.md)
without changing that specification's thresholds or the G1 appearance gate.

## Purpose and boundary

The only question in this control is whether pressure error is small enough that
the later grid-resolution comparison can be interpreted. Pressure-budget selection
is not a proposed technique, performance result or renewed stopping-rule study.
The mass-audit implementation, runner and archived outputs remain frozen.

Use the coupled A/B implementation only after actual GPU source injection has
passed its separate E0 checks. Use the exact registered scene parameters: closed
unit cube, dt=1/60 s, evolving MAC velocity, clamp MacCormack scalar transport,
no obstacle or confinement, zero density dissipation, temperature cooling 0.5/s,
buoyancy 0.6, smoke weight 0.05 and ambient temperature 0. Pressure is cleared
to zero at every step and solved with the production float32 weighted-Jacobi
kernel at weight 2/3.

## Metrics and zero-divergence convention

For every raw step, before any warmup removal or temporal averaging, record:

- RMS and maximum absolute divergence immediately before projection;
- RMS and maximum absolute divergence after pressure-gradient subtraction;
- iteration budget, nonfinite counts and field/configuration hashes; and
- `relative_residual = rms_div_after / rms_div_before` and
  `scaled_max_divergence = dt * max_abs_div_after`.

In this closed obstacle-free discretization,
`div_after = (dt/rho) * (rhs - Laplacian(pressure))`. The divergence ratio is
therefore the relative pressure residual, subject to the full-field identity
checks below. Do not label an unvalidated ratio as a directly measured residual.

Fix `dt * rms_div_before = 1e-10` as the numerical-zero boundary. This is four
orders of magnitude below the tightened absolute-divergence limit and is not a
tunable acceptance tolerance.

- Above or equal to that boundary, evaluate the ratio normally.
- Below it, report the relative residual as `not_applicable`, never as a silently
  substituted zero. Its residual clause passes only when
  `dt * rms_div_after <= 1e-10`; otherwise it fails.
- The maximum-divergence clause and all finite-value checks still apply.

A primary-valid step satisfies relative residual <=1e-4 **and**
`dt * max_abs_div_after <=1e-5`. A tightened-valid step satisfies relative
residual <=1e-5 **and** `dt * max_abs_div_after <=1e-6`. Equality passes. Any
nonfinite value, missing record or invalid zero-divergence case fails both levels.

## Short selection pilot

Run resolutions in the fixed order 32, 64, 128. Each candidate is a fresh
deterministic 180-step trajectory (3.0 s): sources are active on steps 1--120,
corresponding to t=[0,2 s), and inactive on steps 121--180. This includes the
first source/buoyancy response, sustained coupled evolution, the source-off
transition and one second of unforced evolution. It is shorter than the scored
six-second trajectory and does not replace that confirmation.

At every tested resolution and budget, run both registered scenes:

- A: one normalized spherical source at (0.5,0.15,0.5);
- B: two normalized spherical sources at (0.35,0.15,0.5) and
  (0.65,0.15,0.5).

Use this finite geometric candidate ladder, in order:

`256, 1024, 4096, 16384, 65536` iterations per projection.

The factor is four and 65536 is the hard cap. Iterations are fixed for an entire
run. Do not stop Jacobi within a step, warm-start pressure, insert intermediate
budgets or choose different budgets for A and B. These are offline accuracy
controls, so their timings are not evidence of runtime practicality.

For a resolution N, define `I_primary(N)` as the first ladder value for which
every one of the 180 raw steps passes the primary criteria in both A and B.
Define `I_tight(N)` as the first *higher* ladder value for which every raw step
passes the tightened criteria in both scenes. Requiring a higher value ensures
the later opacity check compares two independently refined pressure trajectories;
if `I_primary` already meets the tightened numbers, continue to the next rung.

Complete both A and B at a rung before deciding it fails. Once both selections
exist, stop testing higher rungs at that resolution. If either selection does
not exist under the cap, stop the pilot: G1 is blocked under protocol v1.1 of
SPEC.md. Do not extend the cap merely to obtain a passing result.

During selection, inspect only configuration/provenance, finite checks and the
registered pressure metrics. Do not render density, calculate cross-resolution
density or opacity differences, or inspect candidate frames visually.

## Selected full-field checks

After selecting both budgets at all three resolutions, replay the selected A and
B pilots at `I_primary` and `I_tight`. Capture, after steps 1, 120 and 180:

- pre-projection divergence;
- final pressure;
- post-projection divergence; and
- final U, V and W face velocities.

This fixed matrix covers startup, the final emitting step and the post-source
trajectory. Analyze captured float32 values in float64 on the CPU. Require finite
fields, exact zero normal velocity at the closed boundary, and the following:

1. Recompute divergence from U/V/W and require RMS disagreement with the captured
   post-projection field below 1e-5.
2. Reconstruct the closed-domain pressure Laplacian and residual. Require RMS of
   `div_after - (dt/rho) * residual` below 2e-5.
3. Recompute RMS and maximum directly from each captured divergence field.
   The compact GPU maximum must equal the captured-field maximum. Compact RMS
   must agree with the float64 result within
   `max(1e-10/dt, 2e-4 * cpu_rms)`.
4. Where the pre-divergence is not numerically zero, direct CPU residual ratio
   and captured divergence ratio must differ by no more than 5e-7. Both direct
   and compact classifications must agree at the registered threshold.

The replay must also reproduce the original pilot's 180-step pressure decisions
and registered snapshot hashes on the same GPU. A failed identity, reduction or
repeat check invalidates the measurement; it cannot be converted into a pressure
pass by relaxing a threshold.

## Six-second confirmation and opacity sensitivity

Before calculating G1, run fresh 360-step A and B trajectories at both selected
budgets for every resolution. Retain the same 120 emitting steps, followed by
240 source-off steps. Every raw step must still pass the criterion associated
with its run. Save density at t=0 and every 12 steps, giving the registered
t=0,0.2,...,6 s sequence.

If a selected budget passes the short pilot but fails later in this confirmation,
promote it to the next unused value in the same ladder and rerun both scenes from
reset. `I_tight` must remain higher than `I_primary`. Repeat only within the
registered ladder and stop as soon as both six-second configurations pass. A
failure at the cap blocks G1.

Render the pressure-valid density snapshots with the frozen E0 capture. At each
scene, time and view, form the common ROI from the three primary-budget,
uncorrected resolutions exactly as SPEC.md defines it. For every eligible ROI
(at least 1024 pixels), require
`mean(abs(alpha_primary - alpha_tight)) < 0.003` separately at 32, 64 and 128.
Report full projected-domain alpha error for every image, including samples whose
ROI is too small, but do not use an ineligible ROI to pass or fail this control.
Do not average a failure away across views, times, scenes or resolutions.

Only after all pressure and opacity-sensitivity checks pass may the analyzer
calculate or reveal the 32-versus-128 G1 result. If the alpha control fails, the
pressure confound remains unresolved and G1 is blocked under the current spec.

## Fixed outcomes and no-expansion rule

This protocol has three outcomes:

1. **Pass:** both fixed configurations pass every pressure step, all selected
   field checks pass, and every eligible pressure-refinement image is below 0.003.
   Proceed to G1.
2. **Numerical feasibility blocker:** a primary or distinct tightened
   configuration cannot be obtained by 65536 iterations, including during the
   six-second confirmation. Stop before G1 and report the failed resolution,
   scene, step and criterion.
3. **Measurement or sensitivity blocker:** the full-field validation fails, or
   pressure refinement changes an eligible alpha image by at least 0.003. Stop
   before G1 and report the failure.

Do not add budgets, extend duration, change sources, weaken thresholds, switch
pressure algorithms or select favorable frames after a failure. A necessary
instrumentation repair may be made as E0 validity work, followed by a complete
fresh-root rerun with the numerical decisions unchanged. Any scientific change
requires a new prospective version that preserves this protocol and its outcome;
it cannot retroactively turn these runs into a pass.
