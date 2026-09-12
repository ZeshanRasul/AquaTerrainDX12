# Offline reference integration v1

Registered before replay, 12 September 2026. This is a new feasibility protocol;
the old Jacobi pilot remains blocked. No appearance scoring or positive-effect
assumption is permitted.

Use the same closed cube, sources, physical constants and production transport.
An explicitly offline file handshake pauses after GPU pre-projection divergence,
waits for its fence, solves on CPU, uploads the response and resumes. All files
are step-specific; publish request/response markers only after complete writes.
Timeouts, stale replies, nonfinite fields or D3D12 errors fail the run. This path
is not timed as a real-time solver and is inert unless explicitly selected.

Primary mode `pressure32`: solve the stored divergence RHS in float64 using the
validated cosine basis; upload float32 pressure and use production GPU gradient.
Refined mode `velocity64`: form divergence from captured face velocities in
float64, solve with a separately constructed one-dimensional symmetric stencil
eigendecomposition, project in float64 and upload float32 face velocities.
Both record mean compatibility, fix the pressure gauge, and validate an explicit
3D stencil residual <=1e-9 relative to nonzero RHS. The refined mode controls
RHS/projection arithmetic and solution implementation, not Jacobi iterations.

First validate upload/application on startup states by comparison to the existing
triage captures for 32³ A and 128³ A/B. Then run the fixed short matrix: 32³/64³/
128³, scenes A/B, both modes, 12 steps with six emitting and six source-off.
Audit actual GPU source injection at steps 1,6,7. Repeat 128³ A in both modes
and require bitwise raw physical outputs. Capture pre-divergence, pre-velocity,
uploaded pressure, post-divergence and post-velocity at every step.

Require exact uploaded-field readback, correct boundaries, finite fields, and
existing divergence reconstruction/reduction/identity tolerances. Primary steps
must pass ratio <=1e-4 and dt*max divergence <=1e-5; refined steps must pass
ratio <=1e-5 and dt*max divergence <=1e-6, retaining the registered numerical-zero
convention. Primary GPU projection must reproduce the CPU float32 replay.
These are controls, not a universal error bound. No threshold changes after runs.
For refined projection, retain float64 pressure and explicitly measure the
velocity-storage rounding term in the discrete identity; float32 pressure is
not the pressure used to form that refined velocity. Compare GPU divergence to
the CPU divergence of the actually uploaded velocities. Do not interpret their
finite storage divergence as the float64 linear-solver residual.

An off-mode control must preserve old trajectory fields. At most one targeted
integration repair is allowed, with invalid runs retained and a fresh matrix.
If validation still fails, report the blocker and use the named F1 scope decision.

Passing this short replay earns full 180/360-step reference validation, not G0/G1
by itself. Before G1, both reference modes must pass complete six-second trajectories
and the existing pressure-refinement alpha discrepancy <0.003 at every eligible
scene/time/view/resolution. Keep the original G1 visibility bar unchanged. A
failed short replay or later control is not a negative appearance result.

Execution note: v1_1 hit the 180-second process watchdog in the 128³ refined
replay. Request/response timestamps show the worker answered in about 0.3 s;
the subsequent upload/application path took roughly 16 s per step. The uploader
read mapped upload memory to validate floats, contrary to D3D12's write-combined
heap guidance. The single integration repair validates each row in ordinary CPU
memory then memcpy-writes the upload heap. No threading, numerical, timeout or
threshold change. Preserve v1_1 as incomplete; rerun all cases in v1_2. A renewed
failure is a blocker under this protocol.
