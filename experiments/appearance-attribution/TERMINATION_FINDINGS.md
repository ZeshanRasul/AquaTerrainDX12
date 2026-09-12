# Coupled execution diagnostics

12 September 2026. Investigation of the interrupted `pressure-selection-v1_1`
pilot. No density rendering or cross-resolution G1 scoring is part of this work.

The earlier executable repeatedly exited with process code 2173 before its first
completed readback at 128 cubed / 1024 iterations. No caught-exception or normal
exit record was produced. That evidence does not establish device removal, a GPU
timeout, or a solver defect. The numerical pressure selections from the completed
32/64 portions remain provisional; the interrupted pilot is not a G0 pass.

The new instrumentation enables DRED before device creation, registers a flushed
InfoQueue message callback, and records device-removal status before/after command
submission and on frame-resource reuse. If removal is observed, it writes DRED
breadcrumb counts and page-fault address before throwing. Normal completion still
requires the existing raw source, snapshot, schedule, configuration and InfoQueue
checks. No pressure shader, solver equation, source or numerical threshold changed.

Five two-step execution controls completed in this session: the 128/1024 case,
its repeat, the 128/65536 maximum-budget case, and a matched pair at 128/1024 with
the new DRED/live tracing disabled and enabled. All passed harness validation with
zero D3D12 errors and zero discarded messages. Device status was S_OK whenever
queried in traced runs. The seven raw float files in the matched pair are byte
identical. GPU/driver remained RTX 5090 / 596.36.

Run roots under `runs/`:

- `termination-live-v1-n128-i1024`
- `termination-live-v1-repeat-n128-i1024`
- `termination-live-v1-n128-i65536`
- `termination-ablation-v1-off-n128-i1024`
- `termination-ablation-v1-on-n128-i1024`

**Attribution remains unresolved.** Passing the disabled-tracing control rules out
claiming that the new diagnostics were a demonstrated repair. It does not prove
the earlier binary was correct or establish why the historical process exited.
DRED has no removal event to explain in the new completed runs. Instrumented
execution times are not performance evidence.

One complete fresh attempt, `pressure-selection-v1_2`, will use enabled diagnostics
and the unchanged registered ladder. It must start at 32/256 and cannot reuse the
old pilot after the executable changes. A renewed execution failure stops this
attempt. A valid pressure failure at the cap is recorded as the registered
numerical-feasibility blocker. Neither outcome permits threshold or cap changes.

## Fresh pilot failure and frame-resource repair

`pressure-selection-v1_2` completed the 32/64 ladders (provisional primary
16384, tightened 65536 for each), then stopped at 128/256 scene A. The live
InfoQueue recorded message 921: a resource was final-released while referenced
by in-flight GPU commands. The last lifecycle record was frame-resource-ready
after two submissions, with completed fence zero and device status S_OK.
The stuck process was explicitly terminated by the investigator; its resulting
4294967295 exit code is not another spontaneous 2173. The pilot is a
measurement failure and cannot be resumed after the repair.

Code inspection found ImGui configured for two frames in flight (the swap-chain
buffer count), while the renderer fences a three-entry CPU frame-resource ring.
The backend reuses its upload buffers modulo that configured count and releases
them when growing them. Thus the third frame can overwrite/release buffers from
the first frame before its fence completes. The repair sets ImGui's count to
`NumFrameResources` (three). No backend, solver, source, timestep, or gate changed.
This fixes a concrete unsafe lifetime contract consistent with message 921;
the unnamed resource and historical 2173 exit were not independently attributed
by a call stack, so they are not claimed as conclusively identified.

Post-repair controls `frame-ring-v1-n128-i1024` (two steps) and
`frame-ring-v1-n128-i256` (180 steps) passed all existing harness checks. The
full pilot must restart in `pressure-selection-v1_3` after verification, without
reusing earlier selections. Any renewed execution-validity failure stops this
attempt as the predeclared measurement blocker; no further repair branch is
authorized by this plan.

## Completed verification

The repaired 32³/256 control reproduced all 29 raw float fields and all 26
non-timing columns over 180 steps from the corresponding v1_2 run exactly.
The repaired full 128³/256 control crossed the third submission while the first
fence was still incomplete, then completed without an error.

Fresh pilot v1_3 completed all 30 runs and 5400 raw steps, including both full
128³/1024 runs and both 128³/65536 cap runs, with zero D3D12 errors/discarded
messages. No further execution repair was needed. The pilot ended normally with
the registered numerical-feasibility blocker, not a measurement failure:
128³ has primary 65536 and no tightened selection. See
[pressure findings](COUPLED_PRESSURE_FINDINGS.md). Historical exit-code attribution
remains qualified as above; successful reruns do not recover that missing evidence.
