# Rounding-aware identity validation

12 September 2026. Implements the user-authorized
[prospective amendment](ROUNDING_IDENTITY_AMENDMENT.md). No solver, production
shader, renderer, mass-audit implementation or handoff code changed in this round.

Deterministic controls at 4³/8³/32³ passed, including twelve deliberately corrupted
cases rejected by the same projection/identity checks used by the validator.
Identity-consistent underprojected fields still exceeded both independent primary
divergence limits. These controls establish the distinction between arithmetic
consistency and adequate pressure convergence.

All fourteen historical short runs (168 field steps) pass as regression evidence;
the original outputs were not edited. See `rounding-v1_1-short-regression.json`.

A fresh 32³ scene A pressure32 run completed all 180 steps with 120 emitting steps,
normal-return/provenance sealing and full-field validation. Every projected face
velocity agrees bitwise with the input-derived CPU float32 replay.

| Maximum across 180 steps | Measured | Unchanged limit |
|---|---:|---:|
| Corrected field identity RMS | 5.773645e-17 | <2e-5 |
| Corrected ratio identity difference | 8.480769e-8 | <5e-7 |
| Relative divergence | 2.432350e-6 | <=1e-4 |
| dt-scaled maximum divergence | 3.079573e-8 | <=1e-5 |

The ideal, uncorrected ratio discrepancy reaches 2.110399e-6. Its failure is
retained in the report rather than hidden by increasing a tolerance. Both rounding
corrections are computed from inputs, never fitted to GPU output.

Raw sealed run and machine-readable results are under
`runs/rounding-identity-v1_1/`; validation is `validation.json` and manufactured
controls are `manufactured-controls.json`. This is one fresh numerical control,
not the complete resolution/scene/duration matrix, opacity refinement or G1.

All 180 handoffs succeeded with the existing implementation. This establishes
that the earlier step-43 failure does not happen on every run; it neither repairs
nor diagnoses it. The failed `full-reference-controls-v1` remains failed. Next:
distinguish marker open/read errors from a decoded wrong step, preserve strict
wrong-step rejection, then validate any bounded execution repair in fresh controls
before restarting the full matrix. Do not use this successful run to declare the
historical handoff problem solved.
