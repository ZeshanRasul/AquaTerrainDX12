# Density plateau perturbations

This follow-up asks whether the previously observed SL density plateau survives
changes to density alignment, timestep, and the number of advection steps. It is
a numerical sensitivity experiment, not a performance sweep or a stopping policy.

## Experiment fixed before the sweep

The original closed-box discrete curl plus gradient velocity and scalar fields
are retained. Candidate pressure budgets remain **512 at 32³ and 2048 at 64³**.
The test does not search for a new first plateau at each perturbation.

The principal matrix uses sharp density and SL:

- Resolutions 32³ and 64³.
- Diagonal density shifts of 0, 0.25, 0.5, and 0.75 cells (each axis).
- Timesteps 1/120, 1/60, and 1/30, with physical velocity unchanged.
- One and two successive density-advection steps.

MacCormack checks cover all timesteps without a shift, plus a half-cell shift at
the original timestep, at both resolutions and step counts. Smooth-density SL
checks use shifts 0 and 0.5 at the original timestep, both resolutions and step
counts. This is 72 perturbation cases. Each has four pressure configurations:
the candidate budget, 4096, 8192, and the independently initialized divergence-free
velocity control (-1). Total: **288 launches**, three reset trials each.

All simulations are optimized, configurations are shuffled with seed 314159, and
full fields must repeat identically. The short trial count is for repeatability;
no timing claims are made from this experiment.

## What two steps means

Each trial initializes the same density and provisional velocity, projects once,
then advects density once or twice using the same projected velocity. The second
step consumes the first step's density. There is no density reinitialization,
second projection, force update, or velocity advection between them.

Thus the test isolates whether subsequent scalar transport reveals an existing
velocity error. It does not represent a fully coupled two-step smoke simulation.
The two-step case also spans twice the physical time of its one-step counterpart;
each is compared against its own matched reference.

## Validation

- Independent CPU reconstruction of the shifted initial box/Gaussian.
- Actual initial-array hashes distinguish changed phases from shifts that leave
  a point-sampled box unchanged.
- Identical pressure and velocity across density phases, shapes, methods and step
  counts at a fixed resolution, timestep, and pressure budget.
- The same initial density for all pressure budgets and both step counts.
- A second-step result must differ from its first-step result (no accidental no-op).
- Inherited full-field finite, wall-normal velocity, repeatability, and CPU/GPU
  divergence/pressure-operator identity checks.
- Each case's 8192-iteration relative pressure residual must be below 1e-4;
  normalized density L1 differences to both 4096 iterations and the independent
  velocity control must be below 2e-6.

Exact plateau status means equality of the complete exported float32 density
arrays to the matched 8192-iteration result. Also report normalized L1/L2,
maximum absolute error, number of changed cells, velocity error, pressure residual,
density-weighted divergence, and mass change. A small nonzero density difference
is not labeled an exact plateau.

## Run

After building Release, use a fresh output directory:

```powershell
diagnostics/Run-SmokePlateauPerturbations.ps1 -OutputRoot diagnostics/runs/plateau-v1
python diagnostics/analyze_plateau.py diagnostics/runs/plateau-v1
```

`-Pilot` runs the 16-launch 32³ sharp-SL subset (shifts 0 and 0.5, original dt,
both step counts, four controls). `-ProductionShaders` allows a compiler comparison.
Python requires NumPy. The inherited shared validator is `analyze_projection.py`.

Run the perturbation-specific negative tests against a completed pilot:

```powershell
python diagnostics/test_plateau_validation.py diagnostics/runs/plateau-pilot
```

Sources, runtime shader hashes, executable hash, and the explicit job matrix are
snapshotted before each sweep. Raw fields and repeated-trial CSVs are retained in
the gitignored run directory. The new initializer uses existing reference constants
for the density shift; production advection/projection kernels are unchanged.

An unshifted one-step pilot reproduced all eight fields of the previous 32³
sharp-SL candidate run bit for bit. This checks that the two-step plumbing did not
alter the baseline.
