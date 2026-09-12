# Projected baffles: screening practical pressure tolerances

Completed 12 September 2026 on RTX 5090. This test connects stopping criteria to
forward passage flux and transported density, rather than exact output equality.

## Setup

Three finite baffles: no slot (mode14), a narrow slot (15), and a wide slot (16).
The baffle occupies x=[0.5,0.53125), y=[0.1875,0.8125), all z. Slots start at
y=0.375 and are 0.03125/0.125 wide. Physical geometry is fixed across 32³/64³:
wall thickness is one/two cells and narrow-slot width one/two cells respectively.
Flow can return around the baffle ends. The initial upstream scalar slab is the
same as the first coarse-obstacle test.

Initialize the discrete curl used by the previous reference harness, zero blocked
faces, then apply the production obstacle-aware weighted-Jacobi pressure solve.
Transport density for 60 steps at dt=1/60 with this projected velocity frozen.
The flow is not a time-evolving plume and pressure is not solved again each step.
No buoyancy, sources, damping or moving geometry. Production SL density sampling.
The inherited `obstacle_cfl` field is unused by this curl initializer; it is not
the measured Courant number for these cases.

Candidate checkpoints: 0,16,64,256,1024,4096,8192,32768 iterations. An additional
65536-iteration run establishes a refined numerical reference. Three reset trials
each: 54 launches / 162 trials, plus an eight-launch old-harness regression.

## Explicit acceptance criteria

Exploratory engineering limits: density normalized L1 <=1% after 60 steps,
positive-x flow through the midplane within 5%, and positive-x slot flow within
5% where a slot exists. These are not perceptual thresholds or guarantees.
Flux is an instantaneous one-way volume flux of the frozen velocity field, not
net flux or accumulated smoke throughput. Downstream smoke mass is also reported.

Stopping rules are evaluated offline as the first sampled budget meeting a
threshold. No GPU controller, monitoring overhead or performance gain is measured.

## Main result: a tolerance does not automatically transfer across resolution

For relative pressure residual <=0.01:

| Grid | Baffle | Selected budget | Density L1 | Slot flux error | Passes all limits? |
|---|---|---:|---:|---:|---|
| 32³ | No slot | 4096 | 0.000376% | n/a | Yes |
| 32³ | Narrow | 4096 | 0.7820% | 1.6067% | Yes |
| 32³ | Wide | 4096 | 0.3627% | 0.9959% | Yes |
| 64³ | No slot | 4096 | 3.1676% | n/a | No |
| 64³ | Narrow | 8192 | 3.0376% | 6.2793% | No |
| 64³ | Wide | 8192 | 3.2689% | 4.2545% | No |

Both stricter ordinary baselines pass all six cases: relative residual <=0.001
and dt*max(abs(divergence)) <=0.0001. The latter picks 4096 iterations for every
32³ case, 8192 for the unslotted 64³ baffle, and 32768 for both slotted 64³ cases.
Those match the lowest acceptable budgets in the sampled set. The stricter
relative-residual rule selects 8192 for the slotted 32³ cases and 32768 for the
unslotted 64³ case, so it is more conservative at those checkpoints.

This does not establish that one criterion is generally superior. Budget gaps are
large, thresholds are exploratory, and these are only six related flow/geometry
cases. Tightening an ordinary residual threshold already succeeds here; a novel
transport-aware rule is not necessary to explain the observed acceptance results.
The looser global dt*max-divergence <=0.001 passes only one of six cases; restricting
that maximum to the baffle neighbourhood passes none under these thresholds.

## Reference quality

The initial 8192/32768 comparison was inadequate for the slotted 64³ cases, so the
65536 reference was added before interpreting stopping choices. Density L1 between
32768/65536 is zero for the three 32³ cases and unslotted 64³ case, 0.13844% for
the narrow 64³ slot, and 0.03824% for the wide 64³ slot. Corresponding positive
midplane flux differences are approximately 0.06948% and 0.01902% for those slots.
The reference is therefore approximate, not an exact solution. Disagreements are
smaller than the margins of the highlighted pass/fail outcomes, but are material
when reporting small errors. No continuum-accuracy claim is made.

## Validation

All runs pass finite-field and repeat checks, exact initial density/divergence
consistency across budgets, zero velocity through blocked faces, and zero density
inside solid cells. The CPU pressure operator excludes fluid-solid connections,
matching the GPU obstacle stencil. Its pressure/divergence identity has maximum
RMS discrepancy 5.47e-7 across runs. CPU/GPU divergence and manual interpolation
agree. Final-two-step actual hardware samples, input chaining and final readback
are checked. Earlier steps are not fully traced.

The old eight-launch 32³ pilot passes after the source changes, including bitwise
hardware field/trace comparisons with the prior transport dataset. Release build
and whitespace checks pass. Probe-contaminated timing measurements are not used
as evidence of performance.

## Practical direction

The defensible finding is that pressure tolerances need validation against the
behaviour a developer wants to preserve, such as passage flux and longer-horizon
transport. A relative residual percentage by itself is not that error tolerance.
This experiment does not yet supply a production-ready universal stopping rule.

The strongest simple candidate to test next is a time-step-scaled maximum
divergence threshold, alongside the stricter ordinary relative-residual baseline.
Keep a hard iteration/time cap and expose whether the tolerance was met in any
eventual implementation. The numeric 1e-4 threshold is only a tested candidate,
not a recommended universal default.

Before further threshold tuning, test unseen slot placement/orientation and time
steps, and add a stronger pressure-solver baseline. These thousands-of-Jacobi-step
budgets are diagnostic reference/screening workloads, not an acceptable real-time
implementation. A useful contribution must demonstrate target behaviour at
practical cost, including monitoring overhead. The sampler and obstacle tests
provide validation infrastructure, not a speedup or publication claim.

## Artifacts and reproduction

`summary.json` contains field/flux/residual metrics and `policies.json` the offline
selections. Raw sweep: `diagnostics/runs/obstacle-pressure-v1`; reference extension:
`diagnostics/runs/obstacle-pressure-reference`. Each records input source snapshots
and executable hashes. The runner gained optional budget/resolution parameters
after the first sweep; snapshots preserve the exact versions used.

```powershell
./diagnostics/Run-ObstaclePressure.ps1 -OutputRoot diagnostics/runs/obstacle-pressure-new
./diagnostics/Run-ObstaclePressure.ps1 -Budgets 65536 -OutputRoot diagnostics/runs/obstacle-pressure-new-reference
```

`analyze_obstacle_pressure.py` targets the recorded directories and does not invoke
the renderer. Source and analysis hashes accompany this report.
