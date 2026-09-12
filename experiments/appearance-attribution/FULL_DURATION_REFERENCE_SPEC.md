# Full-duration numerical and opacity refinement controls v1.1

12 September 2026, fixed before full-duration output. Extends the validated
offline reference protocol without changing either solver mode or any threshold.

Run fresh 180-step trajectories, then fresh 360-step trajectories, in the fixed
order 32³/64³/128³, scenes A/B, pressure32/velocity64. Each emits on steps 1--120.
Audit injection at 1/120/121. Check every captured numerical step with the existing
offline full-field validator and its unchanged identity/reduction/ratio tolerances.
The primary prediction now uses the prospectively registered input-derived
rounding terms in [the v1.1 amendment](ROUNDING_IDENTITY_AMENDMENT.md).
Retain all raw exchange fields and provenance. Primary runs must pass 1e-4 relative
and 1e-5 dt-scaled maximum divergence; refined runs 1e-5 and 1e-6, including the
existing numerical-zero convention. Stop on any failed validity or numerical
criterion; identify the case, step and measured value. Do not render a failed run.

For each six-second run require its first 180 physical steps and all matching
snapshots to reproduce the independently restarted 180-step run bit-for-bit.
Repeat 128³ A at 360 steps in both modes, requiring bitwise physical field and
snapshot agreement. This matrix is finite: 12 pilot runs, 12 full runs, 2 repeats.
The process watchdog is 7200 seconds for these longer offline runs; this is an
execution bound, not a numerical iteration cap or a performance claim.

Only after the entire numerical matrix passes, render the registered density
snapshots t=0,0.2,...,6 s using the frozen E0 capture (512², three existing cameras,
absorption 4, ray step 0.01, density scale 1). At each scene/time/view, construct
the common ROI as the union of alpha>=0.05 in the three primary-resolution images.
For each resolution require mean absolute primary/refined alpha difference <0.003
on every eligible ROI (at least 1024 pixels). Record ineligible ROIs explicitly
and full projected-domain alpha error for every image. Never average failures
away. No camera, absorption, ROI or tolerance tuning; no G1 cross-resolution error
calculation or image inspection as part of this task.

If successful, report numerical and opacity-control passes separately. If blocked,
preserve completed data and stop before opacity/G1 as applicable. The old Jacobi
pilot remains blocked. The original v1 extension authorized no new repair round.
The subsequent explicit user authorization permits the rounding amendment and
separate handoff investigation described above; it does not relax numerical gates.
