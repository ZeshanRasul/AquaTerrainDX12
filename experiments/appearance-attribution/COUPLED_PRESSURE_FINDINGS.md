# Coupled pressure selection: blocked at the registered cap

12 September 2026. Authoritative fresh pilot: `runs/pressure-selection-v1_3`.
The completed `pilot-progress.json` records `blocked_at_iteration_cap` at 128³.
This is an E0/G0 control outcome, not a pressure-stopping technique or a G1
appearance result. The previous v1, v1_1 and v1_2 roots remain non-authoritative.

## Execution validity and numerical decisions

All 30 registered runs completed: two scenes, three resolutions, five budgets,
180 steps each (5400 raw steps). Every run passed the current source, schedule,
snapshot, configuration, lifecycle and provenance checks. D3D12 reported zero
errors and zero discarded messages across these runs. Instrumented timings are
not performance evidence. See [execution repair](TERMINATION_FINDINGS.md).

| Grid | First primary budget | Distinct tightened budget |
|---|---:|---:|
| 32³ | 16384 | 65536 |
| 64³ | 16384 | 65536 |
| 128³ | 65536 | Unavailable under the cap |

At 128³/65536, both scenes pass the primary criteria at every step. Both fail
the tightened divergence-ratio limit of 1e-5, despite passing its absolute
divergence limit of 1e-6:

| Scene | First failing step | Ratio at that step | Worst ratio (step 114) | Worst dt × max divergence | Tightened failures / 180 |
|---|---:|---:|---:|---:|---:|
| A | 1 | 2.13203975e-5 | 3.04836462e-5 | 4.13258892e-7 | 158 |
| B | 1 | 2.98545480e-5 | 4.30764717e-5 | 4.60942610e-7 | 165 |

The distinct-higher-budget requirement is also impossible once the primary
selection reaches the cap. These are compact divergence-ratio classifications;
the subsequently planned full-field residual identity replay was not reached,
so this report does not claim a directly validated pressure residual or diagnose
why the ratio fails.

## Decision

Stop under the unchanged [registered protocol](COUPLED_PRESSURE_CONTROLS.md).
Do not raise the cap, weaken thresholds, change the solver or seek favorable
frames. No selected-field replay, six-second confirmation, pressure-refinement
rendering, cross-resolution G1 scoring, or correction development was performed.
G0 is blocked; G1 remains unmeasured. This does not show that a rendered effect
is absent and does not trigger the predeclared invisible-effect fallback as
though a null G1 result had been observed.

The next research decision must be made explicitly in the central plan, retaining
this outcome. Further experiments require a prospective scope decision, rather
than another continuation of pressure-budget exploration.
