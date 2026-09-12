# Coarse obstacles: first controlled baseline

Completed 12 September 2026. The production implementation previously provided
sphere classification; this diagnostic adds inert-by-default reference modes
10–13 for no wall, an aligned one-cell wall, a slanted one-cell wall, and a
quarter-cell wall. The existing SL backtrace, sampler and solid destination
clearing are exercised. No boundary correction has been introduced.

## Protocol and scope

32³/64³ grids; Courant numbers 0,0.5,2,4; three reset trials per configuration:
32 launches / 96 trials. Nonzero-speed horizons satisfy steps=ceil(0.375*N/CFL),
giving a nominal unobstructed travel distance of 0.375 domain lengths. Static
controls run eight steps. Initial density occupies x in [0.20,0.35), y/z in
[0.25,0.75), safely upstream of every wall.

The prescribed velocity is +x at CFL*h/dt on unblocked faces, zero on every
blocked face, and zero in y/z. It is frozen and **not divergence-free**. Pressure,
forces, sources, cooling and dissipation are disabled. This isolates the transport
and voxel classification path, but cannot assess physical mass conservation or
production plume persistence. In particular, advection of a scalar in a compressive
velocity field need not conserve its domain integral even for a correct method.

The aligned wall is centred at x=0.5+0.5/N, width 1/N. The slanted wall has centre
x=0.5+0.5/N+0.25*(y-0.5), width 1/N measured along x. Both span the domain in y/z.
The thin wall is centred at x=0.5, width 0.25/N, between cell centres. Thickness
is grid-relative; this is not refinement of one fixed physical geometry.

## Results

| Geometry | Solid cells at 32³ / 64³ | Voxel sides disconnected? | Downstream density at tested nonzero speeds |
|---|---:|---|---|
| No wall | 0 / 0 | No | Positive |
| Aligned one-cell wall | 1024 / 4096 | Yes | Exactly zero |
| Slanted one-cell wall | 1024 / 4096 | Yes | Exactly zero |
| Quarter-cell wall | 0 / 0 | No | Same field as no-wall control |

Every static control preserves the initial density bitwise. At nonzero speed,
the quarter-cell wall's final density is bitwise identical to the no-wall run.
This is missing geometry in centre-sampled classification, not transport across
a represented solid barrier. No scalar tunnelling through the represented walls
was observed at the sampled end states, for CFL up to 4. Only the final two steps
are traced; this is not an all-time no-leakage proof.

At CFL0.5, the aligned wall cases retain 6.965%/1.579% of initial integrated
density at 32³/64³; slanted walls retain 8.504%/2.921%. At CFL2/4 those wall cases
end at zero density. The no-wall cases retain approximately 100%. These are
descriptive scalar-integral measurements in the prescribed compressive field.
They must not be presented as a measured production-solver defect, nor used to
claim a conservation fix is needed without a divergence-free flow experiment.

## Validation

- Manifests, three recorded trials, finite fields and runtime repeat hashes pass.
- CPU geometry matches the initial field and complete prescribed velocity field,
  including all zero blocked faces. CPU/GPU divergence agrees; pressure is zero.
- Six-neighbour fluid connectivity is checked using the exact 2D reduction of
  these z-extruded masks. Both represented walls disconnect the sides; the thin
  wall does not.
- Final two actual updates match hardware samples with solid destination clearing;
  manual interpolation matches independent CPU evaluation, the recorded late input
  chain agrees, and the final trace equals the exported density.
- Solid cells contain exactly zero density. All zero-speed identity controls pass.
- An eight-launch old 32³ density-sampling pilot passes after the source change,
  including bitwise reproduction of previous hardware field/trace buffers.
- Release build succeeded. The centre-slice figure was rendered and inspected.

## Next decision

The useful first finding is that a thin obstacle can be absent from the simulation
even while it would be visible as geometry. That is a known limitation of this
representation, not yet a novel contribution. The next step is projected flow
around a finite obstacle and through narrow passages, with geometric phase/angle
variations and a reference mask/flow. Measure downstream leakage, upstream
persistence and passage throughput together: simply thickening solids could stop
leaks while incorrectly closing narrow gaps.

Do not change the production boundary method based on the scalar-integral loss in
this kinematic test. A practical contribution would require a demonstrated problem
in suitable flows and a remedy with measured limits and cost.

## Reproduce and inspect

```powershell
# Build AquaTerrainDX12 Release first, including copied runtime shaders.
./diagnostics/Run-CoarseObstacles.ps1 -OutputRoot diagnostics/runs/coarse-obstacles-new
```

The analyzer currently targets `diagnostics/runs/coarse-obstacles-v1`:
`python diagnostics/analyze_coarse_obstacles.py`.

Raw data and exact input snapshots are in that run directory. `summary.json` and
`summary.csv` contain all results. [slices.svg](slices.svg) shows centre slices at
32³, CFL0.5, with a shared density scale. `regression/` contains the old pilot
validation. Source, analyzer and artifact hashes are recorded alongside this report.
