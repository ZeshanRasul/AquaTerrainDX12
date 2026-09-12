# Final bounded plateau experiment

Question: does the hidden pressure sensitivity accumulate under repeated scalar
transport, or stay small as the density evolves? This is the last frozen-velocity
plateau test before reassessing the research direction.

The original sharp box is transported for 2,8,32,120 steps at dt=1/60, at 32³
and 64³. Pressure is projected once, then velocity is held fixed. Density evolves;
velocity, sources, buoyancy, obstacles and temperature feedback do not. This is
not the previously discussed fully evolving plume/budget-controller experiment.

At each horizon and grid, compare hardware and explicit-float density sampling,
candidate pressure budgets 512/2048, 4096/8192 references, and the independent
divergence-free control (-1). Three reset trials per launch. Each method uses its
own reference. Total final dataset: 64 launches / 192 trials.

Final fields plus the last two traces are saved. The last two updates are checked
against their selected sampler, CPU interpolation, stencil bounds and each other.
Earlier unrecorded steps are not independently checked via a full trace chain.
Repeated final fields/traces must match; the two-step runs must reproduce the
previous density-sampling dataset bitwise. Sampling paths must preserve identical
projection fields and final midpoint/departure coordinates.

Reference differences are reported at each horizon rather than silently relaxing
the earlier two-step tolerance. We also record both current-reference-mass and
initial-mass-normalized L1, so density loss cannot hide denominator effects.
There are no timing or visual-quality claims.

Decision: this test can establish accumulation and continued sampling dependence,
but cannot by itself justify an adaptive pressure controller. A larger density
difference is not automatically a visible defect. If it provides no actionable
advantage over conventional residual/divergence diagnostics, preserve the result
and prioritize coarse-obstacle robustness instead of more plateau characterization.

Raw final dataset: `diagnostics/runs/plateau-horizon-v2`.
The partial v1 run was excluded after a provenance guard caught a concurrent
analyzer edit; v2 was rerun with stable monitored inputs.

Reproduction (Release renderer build required):

```powershell
foreach ($steps in @(2,8,32,120)) {
    ./diagnostics/Run-SmokePlateauHorizon.ps1 -Steps $steps -OutputRoot "diagnostics/runs/plateau-horizon-new/s$steps"
}
```

`analyze_plateau_horizon.py` and `test_plateau_horizon.py` target the recorded v2
directories. The runner snapshots the GPU source, existing shared analyzers and
executable hash; analysis provenance separately hashes the new horizon analyzer.
Only two trace slots are allocated regardless of horizon, preserving bounded
readback memory. The renderer accepts 1–120 scalar steps for this diagnostic.
