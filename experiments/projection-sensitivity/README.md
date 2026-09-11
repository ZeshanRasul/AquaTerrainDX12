# One-step projection–advection sensitivity

This experiment measures how incomplete pressure projection changes transported
density, and what the combined projection plus advection costs on the GPU. It is
a mechanism study, not a new solver or a measurement of total continuum error.

## Fixed problem

- Closed unit cube, resolutions 32³ and 64³, dt = 1/60, fluid density 1.
- Smooth Gaussian (sigma 0.085) and discontinuous box, both centered at
  (0.43, 0.61, 0.5). The box half-widths are (0.12, 0.10, 0.12).
- Identical provisional velocity for both density shapes and advection methods:
  a discrete curl of the vertex streamfunction
  `psi(x,y) = 0.12 sin²(pi x) sin²(pi y)`, plus the discrete gradient of the
  cell-centered potential `q(x,y,z) = 0.035 cos(2pi x) cos(2pi y) cos(2pi z)`.
  Normal velocity is explicitly zero on all six walls. The discrete curl is
  divergence-free up to floating-point rounding; projection should remove the
  gradient. The pressure reference is defined up to an additive constant.
- Production weighted Jacobi (weight 2/3), production SL and clamped MacCormack.
  Both pressure and advection are compiled with strictness and optimization level
  3 in measured runs. The initializer is always compiled identically.
- No sources, velocity advection, buoyancy, obstacles, confinement, or damping.

Every trial reinitializes density, velocity, and pressure. These are repeated
measurements of **one step**, not an evolving trajectory. Trials must reproduce
all eight exported fields bit for bit, including across process launches.

## Controls and interpretation

Iteration budgets: 0, 8, 32, 128, 512, 2048. Two long solves, 4096 and 8192,
establish the reference. Budget -1 omits the gradient from the initial velocity
and bypasses pressure subtraction, providing an independent velocity control.
The latter is not a zero-cost solver competitor.

Primary plotted error is
`sum(abs(density[k] - density[8192])) / sum(abs(density[8192]))`, separately
for each advection method. It measures the contribution of incomplete projection.
It does **not** compare the total accuracy of SL and MacCormack against a common
exact transport solution. Small differences between their curves cannot establish
which method is the better complete simulation under a fixed budget.

Required convergence gates:

- Relative L2 pressure residual below 1e-4 at 8192 iterations.
- Density normalized L1 below 2e-6 between 4096 and 8192 iterations, and between
  the 8192 solve and the independent divergence-free-velocity control.
- All wall-normal velocities exactly zero. CPU divergence from exported face
  velocities agrees with the GPU field to RMS below 1e-5.
- CPU Neumann pressure residual satisfies `div(u_projected) = dt/rho * residual`
  to RMS below 2e-5. This checks the operator and boundary consistency directly.

The reported density-weighted divergence is
`sqrt(sum(initial_density * divergence²) / sum(initial_density))`. It is a
diagnostic candidate, not a validated predictor. Additional velocity families
and held-out scenes are required to compare it against ordinary residuals.

## Timing and provenance

Three independent launches per configuration, each with 24 reset trials and the
first six discarded from timing. Configurations are shuffled with a fixed seed.
Each plotted cost is the median of the three launch medians; bars show the IQR
of those medians. These bars are descriptive variability, not confidence intervals.

Timing begins before divergence calculation and pressure clearing, includes all
Jacobi dispatches, gradient subtraction, scalar advection and their intervening
resource transitions, and ends after scalar advection. Initialization, field
readbacks, diagnostic recomputation, CPU analysis, and rendering are excluded.
Reference solves are excluded from the plotted cost frontier.

Raw outputs live under `diagnostics/runs/projection-v1` (gitignored). Each run
exports initial density, initial divergence, pressure, final divergence, three
face velocity components, and final density as packed little-endian float32.
Array storage is z/y/x (x contiguous); U, V, W have one additional face along
their component axis. Manifests include hashes, device and driver identification,
grid, compiler mode, and trial counts. Root provenance includes executable and
source hashes; analysis records its own script hash and NumPy version. The analyzer
was refined while the GPU sweep ran; `analysis-provenance.json` identifies the final
analyzer used for the reported results, and the raw run's source snapshot includes it.

## Reproduce

Reconfigure and build Release first (the new C++ file is picked up by CMake's
source glob), then run from the repository root:

```powershell
diagnostics/Run-SmokeProjectionExperiment.ps1 -OutputRoot diagnostics/runs/projection-v1 -Repeats 3
python diagnostics/analyze_projection.py diagnostics/runs/projection-v1
```

Use a fresh output directory. Python requires NumPy; SVG plotting uses the
standard library. `--partial` validates incomplete experimental matrices without
claiming reference convergence or generating the final plots. The runner must
still complete every configuration specified in its root provenance.

Negative validation checks can use any complete run as a fixture:

```powershell
python diagnostics/test_projection_validation.py diagnostics/runs/projection-v1/smooth-r32-sl-i128-rep1
```

## Integration corrections

The production pressure-gradient kernel unconditionally used an open-top pressure
gradient at the top V face, even with `openTopEnabled = 0`. The condition now
honors that flag. Closed-domain results from earlier coupled simulations therefore
need regeneration; passive-advection cases do not execute this stage.

The reference dispatcher now resolves only timestamps it actually writes, avoiding
unwritten entries in the timestamp query heap. The new experiment reuses that
dispatcher and leaves the existing passive reference cases available.
