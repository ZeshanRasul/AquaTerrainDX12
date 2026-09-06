# Smoke benchmark results

## GPU demo controls and comparisons

Select **3D Smoke Solver - GPU** to open **Smoke 3D GPU controls**. It provides
pause/resume, emitter enable, single step, reset, and a Jacobi iteration slider.
Interactive playback uses a fixed 1/60 second timestep, capped at two catch-up
steps per frame. It drops excess accumulated time under load. Single step
pauses playback and advances exactly one step. Reset clears the state without
changing the emitter or pause setting.

The displayed GPU milliseconds measure the most recently collected timestep
using queue timestamps. They exclude volume rendering and arrive a few frames
late. They are not whole-frame milliseconds. When two steps run in one frame,
the live display reports the last step.

For a comparison:

1. Run the existing CPU benchmark in Release with your chosen total steps,
   emitter steps and warmup. Prefer **Simulation-only run**.
2. Select GPU mode and use the same settings. **Start GPU benchmark** resets
   the GPU state, freezes benchmark controls, and records exactly one fixed
   step per application frame. It uses the CPU reference's buoyancy, cooling,
   density dissipation and grid spacing. The source rates remain 30 density/s
   and 10 temperature/s with no direct source acceleration.
3. Wait until the panel reports the saved directory. Stop-and-save drains
   pending GPU readbacks before exporting a partial run. Keep the app open
   until saving completes.
4. Compare the output directories with:

```powershell
.\diagnostics\Compare-SmokeRuns.ps1 -CpuRun "path\to\cpu_run" -GpuRun "path\to\gpu_run"
```

The script checks grid, schedule, source, physics, build and rendering settings.
It compares shared post-warmup steps, solver timing means, final density integral,
and density-weighted centre distance. A timing ratio is not an equal-accuracy or
whole-application speedup: CPU PCG and fixed-iteration GPU Jacobi solve pressure
to different accuracies.

GPU runs save under `diagnostics/runs/<epoch-microseconds>_gpu_jacobi/`, relative
to the app's working directory. The panel shows the absolute output path:

- `manifest.json`: schedule, physics, grid, GPU identity, build, precision,
  Jacobi iterations and measurement limitations.
- `steps.csv`: GPU solver milliseconds, density min/max/sum/integral/centre,
  and nonfinite density count for every step.
- `summary.csv`: mean and p50/p95/p99 GPU solver milliseconds after warmup.

GPU density readback happens after the end timestamp and is consumed only once
the corresponding frame fence completes. It is excluded from solver timing but
can still affect frame cost and memory traffic. Terrain continues rendering in
simulation-only mode; smoke visualisation is hidden. GPU divergence, velocity,
temperature and pressure-residual metrics are currently unavailable, rather
than reported as zero. A fixed schedule does not promise bitwise determinism
across GPUs. Rerun the CPU baseline with matching settings when comparing.

## CPU export format

The deterministic smoke benchmark writes each run to
`diagnostics/runs/<UTC timestamp>_<implementation>/`.

Each run contains:

- `run.json`: the scenario, grid, fixed timestep, source schedule, physics
  parameters, algorithm labels, build type, precision and measurement mode.
- `steps.csv`: one row per fixed simulation step. It contains solver-only CPU
  time, throughput, pressure diagnostics, scalar statistics, velocity and
  energy statistics, and the density-weighted plume centre and spread.
- `summary.csv`: one row designed for comparing several runs in a spreadsheet,
  dataframe or plotting script. Timing percentiles exclude the configured
  initial warmup rows, while every row remains in `steps.csv` for inspection.

The benchmark resets the solver and advances it exactly once per rendered app
frame using its own fixed timestep and emitter schedule. Wall-clock frame time
does not determine the simulated state. Solver timing surrounds source
injection and `SmokeSolver3::Step`; metric collection and file output are timed
separately and excluded.

Use the same `scenario`, grid, schedule and physics values when comparing CPU
and GPU implementations. Compare individual fields after one or a few steps
when validating numerical parity. Over longer runs, compare density integral,
plume centre and spread, kinetic energy and divergence because small floating
point differences naturally accumulate.

For performance reports, use Release builds and report at least three runs.
Quote median and p95 solver time, real-time factor, cell throughput and pressure
iterations together with the grid resolution and hardware used.
