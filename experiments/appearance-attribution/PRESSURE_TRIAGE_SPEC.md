# Same-state pressure triage v1

12 September 2026, registered before field capture/analysis. Implements the user's
option-1 decision with option-3 fallback. The previous pressure pilot is still
blocked. This is diagnostic feasibility work, not a new passing pressure protocol.

Question: does a more accurate solution of the identical closed-box discrete
pressure equation plausibly remove the limiting error, or is the limitation in
operator consistency / stored precision / velocity projection?

Bound: initially about one hour of diagnostic work, including missing capture
instrumentation. Report an unresolved outcome rather than expanding the matrix
if that is insufficient. Any reference-integration phase is separately bounded
by one working week, reviewed by 19 September. No iterative solver integration
is promised or started merely because CPU and GPU fields differ.

## Fixed captures

- 32³ scene A, step 1 (positive control).
- 128³ scenes A and B, step 1 (both fail the old tightened criterion).
- 128³ scene A, step 114 (recorded worst ratio in the cap trajectory).
- Each follows the unchanged production trajectory with 65536 iterations.
- Capture pre-divergence and U/V/W before projection; pressure at iterations
  0,256,1024,4096,8192,16384,32768,65536; post-divergence and projected U/V/W.
- Single captured step per launch. Restore resource states after every copy,
  map only after that step's fence; keep production shader code unchanged.
- Validate existing harness checks, finiteness, sizes and hashes. Compare all
  available original density snapshots and non-timing physical metrics against
  the matching v1_3 trajectory prefix (excluding run-configuration identity).
  Inert/off capture control must also reproduce existing raw fields.

Different coupled trajectories are not a Jacobi convergence curve. The new
checkpoints share one actual GPU right-hand side and pre-projection velocity.

## CPU analysis

Reconstruct the exact obstacle-free closed-box stencil. Check pre/post divergence
against face velocities; check pressure residual versus divergence after the
gradient, using the existing full-field identity tolerances. Also report errors
relative to the small RHS, not just absolute pass/fail numbers.

Measure mean RHS compatibility and fix pressure's constant gauge. Do not silently
discard a nonzero mean: report the unavoidable component separately. Solve the
mean-free equation in float64 using the separable cosine basis of this specific
box operator (no claim of obstacle support). Independently verify its residual
with an explicit stencil and manufactured constant/mode/random-field controls.
This CPU solve is diagnostic, not a production algorithm change or new method.

Compare GPU checkpoints with that solution using gauge-free pressure differences,
gradient differences and explicit residuals. Compare float64 projection against
float32-stored pressure / velocity variants. No single CPU/GPU disagreement
establishes precision as the cause, and a plateau alone does not distinguish
roundoff, incompatible RHS or an operator defect.

## Decision

- If the operator is consistent and a more accurate same-state solution materially
  reduces projected divergence, reference feasibility may proceed within its week.
  Identify whether pressure iteration or projection precision must be addressed;
  do not assume PCG alone is sufficient.
- If the limitation lies elsewhere, record it before proposing a remedy. If no
  bounded trustworthy reference route is supported, activate the finite F1 scope.
- If evidence is mixed, report the unresolved discriminator. No new pressure cap,
  gate relaxation, G1 image inspection, or additional scene search.

F1's utility gate remains practical selection guidance. A regime in which a
cost-matched SL baseline wins is one possible contribution, not a requirement
that established theory be contradicted. All future comparators and decision
criteria must be fixed before scoring that branch.
