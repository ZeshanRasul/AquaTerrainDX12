# Rounding-aware identity v1.1

12 September 2026. User-authorized prospective amendment, registered before new
validation runs. Historical full-reference-controls-v1 remains an interrupted,
failed measurement. Its 42 field captures motivated this definition; they cannot
become a completed control by reanalysis.

For pressure32, let D and L denote the independently evaluated float64 divergence
and closed-box Laplacian, b the stored GPU pre-divergence, and p the uploaded
float32 pressure. Compute v_exact = project64(v_before,p,dt) and
v_replay = project32(v_before,p,dt), using the existing operation-order replay.
The prediction is:

    b - dt L(p) + [D(v_before) - b] + D(v_replay - v_exact)

Both corrections depend only on captured inputs and specified arithmetic.
Neither is fitted to the observed GPU post-velocity or divergence. Report the
ideal identity discrepancy and each correction's RMS alongside the revised
identity. This accounts for rounding in this projection pipeline, not arbitrary
trajectory error or proof of physical accuracy.

Retain bitwise CPU/GPU velocity agreement, pressure upload/hash agreement,
float64 pressure residual <=1e-9, finite values, closed boundary checks, compact
reduction checks, field identity RMS <2e-5 and ratio identity difference <5e-7.
Primary divergence bounds remain 1e-4 relative and 1e-5 dt-scaled maximum;
refined bounds remain 1e-5 and 1e-6. Refined storage accounting is unchanged.
An identity pass alone never establishes adequate incompressibility.

Before fresh coupled controls, run deterministic manufactured controls for the
input-derived correction and rejection controls for corrupt GPU velocity, wrong
pressure used by the prediction, an incorrect Laplacian prediction, and a wrong
compact residual. Also verify that an identity-consistent, underprojected field
still fails the independent divergence limits. Reanalysis of completed short
controls is regression evidence only. A fresh 180-step 32³ A pressure32 run is
the first coupled check; stop if its execution or validation fails. The full
matrix and opacity criteria remain as registered in FULL_DURATION_REFERENCE_SPEC.

The user also authorizes investigation of the separate handoff failure. Do not
attribute its historical cause to rounding. Preserve wrong-step rejection;
distinguish marker open/read errors from a successfully decoded wrong step before
any execution repair. No automatic tolerance, solver, scene or rendering changes.
