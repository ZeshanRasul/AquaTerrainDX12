# Normalized GPU source injection — registered controls

Protocol v1.0, 12 September 2026. This closes only the source-injection portion
of E0. It does not exercise advection, buoyancy, projection, rendering or G1.

Use the selected 16³ midpoint quadrature source tables from `appearance_source.py`
for scenes A and B at 32³, 64³ and 128³. The experiment-only GPU shader reads the
stored R32_FLOAT rate through a byte-address buffer and adds `rate * dt` in place
to both R32_FLOAT density and temperature. The coupled harness must use this exact
shader and rate-table construction. It must skip its dispatch after source shutoff.

Run these cases in both debug/skip-optimization and O3, with three reset repeats:

- one step from zero at float32 dt=1/60;
- 120 steps from zero, with a UAV barrier between steps;
- one step from density 0.125 and temperature 0.25 for scene A at 32³.

CPU expected fields perform the float32 multiply once, followed by float32 addition
for every step. Require GPU density and temperature to match these fields bitwise,
all repeated outputs to match bitwise, and debug/O3 outputs to match bitwise. The
integrated density and temperature increments, summed in float64 and scaled by cell
volume, must each agree with the stored-table target after applying the same CPU
recurrence. Also report their relative difference from the ideal real-number value
`source_count * 0.002 * dt * steps`; require this to be <=2e-6 in zero-initial cases.
Outside nonzero-rate cells, seeded values must remain bitwise unchanged. All inputs
and outputs must be finite and D3D12 validation must report zero errors.

This validates the source implementation and schedule-sized accumulation. It does
not prove that a future coupled dispatch binds the correct ping-pong texture or
shuts off at the correct step; the coupled harness must capture density and
temperature immediately before/after injection at steps 1, 120 and 121.

## Control-informed implementation repair

The v1.0 run was executed before this paragraph. All debug outputs matched the CPU
recurrence, and both compiler modes stayed within 7.27e-7 of the ideal integrated
rate. However, O3 differed from debug after 120 additions: 4–212 affected cells,
with maximum absolute difference from 2.38e-7 to 1.38283e-5 depending on grid/scene.
One-step and seeded controls matched. This fails the preregistered compiler-mode
bitwise requirement and is an implementation validity failure, not a physics result.

The single permitted repair declares the read/modify/write accumulation value
`precise`, preventing multiply-add contraction across the source multiply and
existing value. Rerun the unchanged matrix in a fresh v1.1 root. No source values,
dt, case, output threshold or scientific gate changed. If v1.1 still fails any
fixed condition, report source injection blocked rather than iterating further.
