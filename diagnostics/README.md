# Smoke benchmark results

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
