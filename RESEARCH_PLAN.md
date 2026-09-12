# Real-time smoke research plan

Version 1.9 — 12 September 2026. Status: E0 rendering/source-table/standalone
injection/accounting controls and the 32³ coupled source bridge are validated;
the coupled pressure gate is blocked at the registered 65536-iteration cap at
128³. See [pressure findings](experiments/appearance-attribution/COUPLED_PRESSURE_FINDINGS.md).
The user has selected option 1 with option 3 as fallback. A bounded
[same-state pressure triage](experiments/appearance-attribution/PRESSURE_TRIAGE_SPEC.md)
supports a more accurate offline reference in the four tested states; see
[triage findings](experiments/appearance-attribution/PRESSURE_TRIAGE_FINDINGS.md).
The [offline integration and short replay](experiments/appearance-attribution/OFFLINE_REFERENCE_FINDINGS.md)
now pass: fourteen runs / 168 full-field steps, including bitwise repeats. The
[full-duration attempt is blocked](experiments/appearance-attribution/FULL_DURATION_REFERENCE_FINDINGS.md)
at the step-43 handoff of its first run. Partial fields also expose a primary
ideal-identity/rounding limitation. Opacity refinement and G1 remain unmeasured;
the old Jacobi blocker remains unchanged. The user has now explicitly authorized
the [rounding-aware identity amendment](experiments/appearance-attribution/ROUNDING_IDENTITY_AMENDMENT.md)
and separate handoff investigation. Numerical limits remain unchanged; fresh
controls are required, and the interrupted run retains its failed status.
The [amendment now passes](experiments/appearance-attribution/ROUNDING_IDENTITY_FINDINGS.md)
manufactured rejection controls, 168 historical regression steps and one fresh
180-step 32³ A primary run. The [marker transport control](experiments/appearance-attribution/MARKER_ACCESS_FINDINGS.md)
now also passes seven synthetic cases and fresh 180-step runs in both reference
modes (360 steps, zero D3D12 errors). A controlled lock reproduces the old reader's
misleading mismatch; the historical OS cause is still unknown. The revised reader
logs native errors and retries only missing publication/sharing/locking conflicts.
The full numerical matrix is ready for manual execution; opacity/G1 remain open.
This is the central decision document
for Codex, Claude and the user. No G1 scientific result has been collected.

## Objective and scope

Deliver a reproducible, developer-usable graphics result, suitable for a focused
JCGT submission. Publication is a target, not a promised outcome. Current priority:
**resolution-consistent rendered smoke opacity**, with persistence as a secondary
measurement. Pressure stopping
and obstacle handling remain useful supporting infrastructure, but are not active
parallel paper proposals.

Target claim to test, not assert:

> At fixed physical setup, time step and advection scheme, a lightweight
> render-side calibration reduces the change in rendered smoke opacity
> caused by changing grid resolution, and transfers to previously untuned flows.

Primary scheme: corrected clamp MacCormack. Resolutions: 32³,64³,128³. Physical
domain, source integrals, duration, solver equations and rendering settings stay
fixed. The 128³ result is a consistency target, not ground truth. Semi-Lagrangian
is a supporting baseline. Dynamic grid switching, time-step LOD, solver switching,
arbitrary quality presets, moving obstacles and general liquid simulation are
outside the primary claim.

Primary remedy family: render-side extinction/density-scale calibration only.
Calibrated values never feed into simulated density, buoyancy, temperature, pressure
or velocity. Motion-field invariance is therefore a plumbing requirement, not a
research benefit. In contrast, gains applied to simulated density could change
motion through buoyancy feedback and are explicitly outside this proposal.

## Decisions already made

1. No further plateau, sampler reverse-engineering or pressure-threshold sweeps
   unless needed to invalidate a specific measurement in this plan.
2. Establish a rendered effect in **coupled smoke** before developing a correction.
   Prescribed-flow mass loss alone cannot pass that gate.
3. Separate mass, diffusion/shape, opacity and persistence. Constant periodic
   translation is a control, not a stand-in for spatially varying transport.
4. Preserve the mass-audit implementation and frozen reference outputs. New
   attribution/rendering work lives in sibling diagnostic files and new run roots.
5. If the primary visibility gate fails, take the finite benchmark/limiter branch
   below. Do not tune cameras, thresholds or scenes until the claim appears to pass.
6. A negative result is a completed decision. A benchmark paper is also conditional
   on practical utility; publication is not guaranteed by collecting more cases.

## Prospectively fixed gates

Detailed definitions: [appearance/attribution specification](experiments/appearance-attribution/SPEC.md).
Thresholds below are engineering decision bars, **not measured human visibility
thresholds**. They must not be described as perceptual validation without a study.

| Gate | Pass condition | Failure action |
|---|---|---|
| G0: measurement validity | Shader-derived alpha, render-refinement error below 0.003 mean absolute alpha, repeat/provenance checks, numerical controls | Fix measurement only; at most one implementation repair round before reporting a blocker |
| G1: worthwhile resolution effect | At least 0.03 mean absolute alpha difference between 32³ and 128³, sustained for three consecutive 0.2-s samples, in at least two of three views, in both predefined coupled scenes | Close appearance-technique proposal; enter fallback F1 |
| G2: useful remedy | On each untuned evaluation scene, reduce primary integrated opacity discrepancy by at least 50%; beat the best simple-gain baseline by at least 20%; no >0.005 increase in mean alpha error in any scored view/time sample | Report simple baseline if useful; otherwise F1. Do not add correction families indefinitely |
| G3: practical cost/portability | Added GPU cost <=5% of baseline smoke simulation+rendering cost and <=0.10 ms on RTX3070 laptop; valid on both available devices | Simplify once or drop the technique claim; no hidden readback or offline-fit cost presented as runtime-free |

The G2 comparisons use the same fields, views, ROI definition and simulation
parameters for every remedy. The correction must also keep the alpha-threshold
persistence-duration error no worse than the best simple baseline plus 0.2 s.
Satisfying G2/G3 does not establish visual preference or physical correctness.
Claim improved persistence only if uncensored held-out measurements establish it;
the non-degradation check alone does not support that claim. For G3, use a declared
practical runtime configuration, never the expensive accuracy-reference solve as
the overhead denominator. Verify remedy quality in that runtime configuration too.

## Experiment queue and deliverables

| ID | Question and deliverable | Allowed implementation | Status |
|---|---|---|---|
| E0 | Can we trust opacity capture and numerical accounting? Offscreen alpha/RGB capture, controls, manifests and validation report | Sibling diagnostics; minimal gated integration hooks | Rendering/source/accounting and offline 12-step integration pass; full-duration control blocked by handoff failure, with a separate primary identity rounding limitation; opacity refinement unmeasured |
| E1a | What explains the original loss? Reproduce it; measure explicit rejection/floor terms and boundary exposure; compare matched boundary controls if needed | Separate reference-harness counters; no mass-audit edits | Exact 32³/64³ historical replay complete; loss in interior sampling, zero rejection/flooring; no further boundary sweep justified before G1 |
| E1b | Does a meaningful resolution-dependent rendered effect occur in coupled scenes A/B? Produce G1 decision with all views | Corrected production kernels, evolving velocity, controlled sources/forces | G1 unmeasured; old Jacobi selection blocked; new offline path awaiting full-duration controls |
| E2 | Do fixed and time-dependent render gains suffice? Fit only on scene A; evaluate B | Renderer-only gains and frozen fitting procedure | Only after G1 |
| E3 | Does one predeclared calibration improve on simple gains and transfer to untouched scene C? Produce code, negative cases, cost and two-device replication | One correction family; freeze its design before fitting | Only if E2 leaves a useful gap |
| E4 | Assemble submission-ready paper/artifact or honest negative-result package | Documentation, reproducibility, regression | After decision |

E1a and E1b are complementary. E1a cannot substitute for the coupled bridge.
If boundary behavior dominates the original mass discrepancy but G1 passes, the
appearance question is still viable. If G1 fails despite intrinsic mass loss,
the technique proposal closes. No new remedy is allowed merely because a scalar
mass plot looks interesting.

Before E2, register the exact optimizer and parameter bounds without inspecting
evaluation corrections. Baselines are no correction, one constant positive gain
per resolution, and a positive piecewise-linear time gain with knots at 0,3,6 s.
The time gain's dependence on known effect age must be explicit. Neither baseline
may be fitted per evaluation scene. Bounds: gains in [0.25,4]. If a constant gain
already solves the problem, assess that modest result honestly instead of making
the method more complex to manufacture novelty.

Before committing to E3 or a paper claim, complete a focused primary-literature
review of smoke appearance calibration, resolution scaling and relevant benchmark
comparisons. Record the closest methods and the precise missing practical result.
An effect passing G1 establishes a problem in these scenes, not novelty. If prior
work already supplies the proposed solution, reproduce that baseline and reassess
the deliverable instead of relabelling it as a new technique.

## Precommitted negative-result path

F1 deliverable: a finite, corrected benchmark and limiter comparison, targeting
practical selection guidance rather than a new fluid technique.

- Reuse the trusted core without rewriting its history.
- Compare SL and MacCormack clamp/revert/current adaptive limiter on a fixed
  smooth/discontinuous transport set plus coupled scenes A/B with rendered output.
- Include boundary cases, failure examples, optimized cost with invariance checks,
  and a concise method-selection table backed by the measurements.
- Do not automatically add BFECC, cubic, RK3, confinement or new solver families.
  Choose at most one additional established comparator only if a literature review
  identifies it as necessary to assess the specific claim. Declare that choice
  before running it. Cubic cost and implementation effort are not assumed cheap.
- If this produces no useful decision guidance beyond established results, stop
  with a public-quality technical report/artifact. Do not promise a JCGT acceptance
  or open a new research branch inside F1.

## Freeze and provenance policy

This plan resolves the freeze tension through **separate instrumentation**,
not lifting the mass-audit freeze. Do not edit SmokeMassAudit.cpp, its diagnostic
runner, audited counters, archived results, or pressure algorithm for this work.
Do not revise old datasets to match new interpretations. Add dated caveats only
when the actual historical configuration was affected; the closed-top gradient
fix must not automatically be blamed for documented open-top runs.

The live renderer/reference code already contains later diagnostic extensions;
"frozen" does not imply the entire current tree is byte-identical to September 11.
Record present hashes and preserve archived source snapshots. If a new diagnostic
needs shared shader code, use an inert-by-default compilation variant or hook,
document it, and reproduce the relevant baseline before/after. If this cannot be
done without changing the frozen audit, record the conflict before proceeding.

No probe may silently apply clamped interpolation to a periodic case. Implement
wrapped indexing in the new diagnostic variant or reject that configuration.
No performance claim may use instrumented timing. Capture physical units, source
volume integrals, code/executable/runtime-shader hashes, device, driver, build
flags, rendering constants, seeds, run completeness and rejected runs.

## Coordination and stopping discipline

- One active experimental question at a time. Code review and literature review
  can support it, but nobody opens a parallel algorithm branch without updating
  this plan first. This document does not itself authorize spawning other agents.
- Every experiment links here, identifies its gate and lists expected outcomes
  before implementation. Add results to the decision log, including null results.
- A new question must show which current claim it could falsify and what decision
  would change. Otherwise park it rather than run it.
- Gate thresholds are immutable after scored data are inspected. Necessary
  changes get a new version, reason and prospective dataset; prior results remain
  labelled exploratory. A technical failure is neither a pass nor a null result.
- At most one targeted repair for a failed measurement implementation. Persistent
  validity failures are reported as a blocked gate, not invitations to change the
  scientific question. This is a planning rule, not a tool-goal status directive.

## Timeline targets, not guarantees

Scope decision of 12 September: triage first, then at most one working week of
reference feasibility work (review by 19 September), with the finite F1 benchmark
as the named fallback if reference feasibility fails. No automatic switch to
preset matching, obstacle research or a new stopping rule. Aim to freeze scientific
scope and experiments by 20 November, preserving six to eight weeks for writing
and revision. Publication by February remains outside our control.

By late September: E0/E1 and a recorded G1 decision. By mid-October: either a
bounded correction with transfer results or F1's fixed benchmark results. Aim for
a submission-ready package during November, leaving time for revisions before the
February 2027 aspiration. External review/publication timing is not controlled.
If E0/E1 slip, narrow scope instead of compressing validation or adding features.

## Decision log

| Date | Decision | Evidence/status |
|---|---|---|
| 2026-09-12 | Freeze plateau/pressure exploration as supporting work | Mechanism replicated; no practical new stopping rule established |
| 2026-09-12 | Select resolution-only rendered appearance claim | Hypothesis, not evidence; requires E1b |
| 2026-09-12 | Predeclare G1 and F1; keep mass audit frozen | User feedback incorporated; thresholds are chosen engineering bars |
| 2026-09-12 | E0 is next; no new correction or scheme implemented | Planning only in this revision |
| 2026-09-12 | Amend capture ray step from 0.02 to 0.01; preserve fixed error bars | Original control error 0.00331272 fails; revised 0.00141828 passes; no G1 data inspected |
| 2026-09-12 | Rendering and static source controls pass; retain accounting/bridge work | [E0 report](experiments/appearance-attribution/README.md); no technique claim or full G0 pass |
| 2026-09-12 | Accounting controls pass; close direct boundary-kill/floor explanation for historical SL references | [112 controls and exact 240-update replay](experiments/appearance-attribution/ACCOUNTING_FINDINGS.md); all loss in interior sampling; no coupled/MC appearance claim |
| 2026-09-12 | Advance to injection/pressure validity for coupled G1; do not expand attribution into another sampler branch | Historical initial masses differ by 24.4444%; normalized sources are necessary; no extra 128³/boundary experiment needed for the direct-loss question |
| 2026-09-12 | Retain source-injection v1 as an implementation failure and use the single registered repair | Six O3 120-step fields differed from debug; declaring the accumulation increment `precise` was the only change; sources, dt, cases and gates stayed fixed |
| 2026-09-12 | Standalone normalized-source injection passes; coupled integration remains open | [26-configuration v1.1 result](experiments/appearance-attribution/SOURCE_INJECTION_FINDINGS.md): bitwise CPU/repeat/compiler agreement, worst ideal relative error 7.24382e-7; future ping-pong binding, steps 1/120/121 schedule and pressure are unvalidated |
| 2026-09-12 | Fix a finite coupled-pressure selection protocol before coupled output | [Pressure controls](experiments/appearance-attribution/COUPLED_PRESSURE_CONTROLS.md): 180-step A/B pilots, candidate ladder 256--65536, raw-step criteria, distinct tightened solve, full-field checks and explicit blocker outcome |
| 2026-09-12 | Invalidate coupled v1.2 controls and the preliminary pressure-selection-v1 root | Review found density could remain in `COPY_SOURCE` when rebound as a UAV, a missing temperature UAV dependency barrier, and no direct InfoQueue capture; retain the files for debugging only and use no trajectory or pressure result |
| 2026-09-12 | Coupled source binding/schedule passes at 32³ for scenes A and B; pressure remains open | [Fresh v1.3 controls](experiments/appearance-attribution/COUPLED_BRIDGE_FINDINGS.md): 121 contiguous steps per scene, bitwise density/temperature reconstruction at steps 1/120/121, twelve valid snapshots per run, zero D3D12 errors/discarded messages; no G1 or pressure selection claim |
| 2026-09-12 | Stop pressure-selection-v1_1 and v1_2 for execution failure; repair the ImGui frame-resource lifetime contract | [Execution findings](experiments/appearance-attribution/TERMINATION_FINDINGS.md): live D3D12 message 921, two ImGui resource sets versus three fenced frames; align the counts, validate controls, then restart the unchanged full ladder in v1_3. Any renewed validity failure blocks G0; no G1 scoring or additional repair branch |
| 2026-09-12 | Close v1_3 as a numerical-feasibility blocker at 128³; stop before G1 | [30 valid runs / 5400 steps](experiments/appearance-attribution/COUPLED_PRESSURE_FINDINGS.md), zero D3D12 errors; 32/64 select 16384 primary and 65536 tightened, 128 selects 65536 primary with no tightened budget. No cap/threshold/solver changes; G1 is unmeasured, not a null result |
| 2026-09-12 | Authorize option 1 with option 3 fallback, beginning with same-state error triage | Prospective sibling captures and float64 analysis only; no new pressure solver in production, no old-gate relaxation, no appearance scoring |
| 2026-09-12 | Same-state triage earns the bounded reference phase | [Four validated captures](experiments/appearance-attribution/PRESSURE_TRIAGE_FINDINGS.md): 128³ residuals still fall about ninefold over the final checkpoint interval; direct reference with float32 projection passes the tightened criteria in the tested states. No new trajectory/G0/G1 pass; review reference feasibility by 19 September, F1 if blocked |
| 2026-09-12 | Offline reference integration and short replay pass after one upload-path execution repair | [v1_2 results](experiments/appearance-attribution/OFFLINE_REFERENCE_FINDINGS.md): 14 runs / 168 field-validated steps, zero D3D12 errors, exact upload/projection and repeats. Twelve-step horizon only; full-duration pressure and alpha-refinement controls still precede G1. Keep incomplete watchdog-stopped v1_1 |
| 2026-09-12 | Stop the full-duration matrix before opacity | [First run interrupted at step 43](experiments/appearance-attribution/FULL_DURATION_REFERENCE_FINDINGS.md); retained response marker is correct but read failure versus mismatch is not distinguished. Completed fields show ideal-identity ratio discrepancy above 5e-7 from step 22, attributable to arithmetic rounding in bitwise-reproduced projection. No numerical threshold or validation definition changed; no new repair round |
