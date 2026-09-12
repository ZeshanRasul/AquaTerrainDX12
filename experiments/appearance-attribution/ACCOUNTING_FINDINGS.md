# Direct advection accounting and historical replay

12 September 2026. Supports the [central plan](../../RESEARCH_PLAN.md) and
[registered accounting controls](ACCOUNTING_CONTROLS.md).

## Decision

The historical hardware-SL mass loss is reproduced and directly accounted for.
Top rejection, positivity flooring, damping and solid clearing contribute **zero**
over the complete 120-step histories. All measured loss occurs between the input
field and the point-sampled advected field, at interior destinations. This closes
the proposed top-kill/floor explanation for these particular reference runs.

Do not turn this into another sampler/plateau investigation. Next validate actual
GPU source injection and pressure accuracy for coupled scenes, then measure G1.
No correction is justified until the predeclared rendered-effect gate passes.

## Measurements

| Hardware-SL reference | 32³ | 64³ |
|---|---:|---:|
| Initial integrated density, unit-volume domain | 0.013671875 | 0.010986328125 |
| Final integrated density | 0.0117785225544 | 0.0103890711254 |
| Fraction of initial mass retained | 86.1514793% | 94.5636341% |
| Cumulative sampling-stage mass change | -0.00189335244563 | -0.000597256999594 |
| Top-rejection events | 0 | 0 |
| Negative values before flooring | 0 | 0 |
| Solid-clearing events | 0 | 0 |
| Damping-stage mass change | 0 | 0 |
| Net sampling change at boundary-stencil destinations | 0 | 0 |
| Maximum input or sampled density at those destinations, all steps | 0 | 0 |
| Maximum per-cell observer/output discrepancy | 0 | 0 |

Every step was captured; this extends the old experiment's final-two-step trace
checks. Each update was repeated three times with identical input, output and
observer traces. The chained replay matches archived step 2 and step 120 density
**bitwise**, plus step 119/120 input, output and departure coordinates. The original
projected velocity fields are loaded unchanged, so this is not a new pressure solve.

The spatial partition alone would not identify causal wall-clamping loss. Here the
boundary-involving destinations are additionally smoke-free in both the input and
sampled fields at every step. There is no measured direct scalar boundary-loss
term to pursue in this setup. Walls can still influence the prescribed velocity;
this does not establish a boundary-independent general result for other flows.

## A limitation of the old motivating comparison

The 32³ blob starts with **24.4444% more integrated density** than the 64³ blob.
Its discrete occupied bounds also differ in physical coordinates. Retained fractions
are correctly normalized to each run's own initial mass, but the pair is not an
equal-source, geometrically matched appearance comparison. Do not use these values
to claim that a resolution change removes a specified amount of visible smoke.

These are also **SL, frozen-velocity** results. They do not establish the planned
clamp-MacCormack effect in evolving, buoyant smoke. The validated normalized source
tables and the mandatory coupled G1 experiment address those gaps prospectively.

## Observer validation

The new standalone `AdvectionAccounting` target runs the actual production entry
point and then an independent observer of its inputs. It does not modify production
output, define `SMOKE_MASS_AUDIT`, or reuse the audit's GPU counters. The observer
shares production sampling, backtracing and limiter helpers, so the independent
analytic controls are necessary; observer agreement alone would not validate those
shared functions.

112 control configurations passed at 32³/64³, with debug/skip-optimization and O3
shader compilation, and three repeats each. Every observed final value matched
the corresponding production output bitwise; all control budgets closed exactly.
Optimized and debug production outputs also matched bitwise in these controls.
D3D12 validation recorded zero errors. Buffer initial-state informational messages
are retained; these tests make no timing claim.

Independent controls exercised identity, periodic x/y translation, known damping,
negative-value flooring, an exactly known top-exit slab, a CPU-classified sphere,
and manufactured lower/upper MacCormack limiter excursions. Full MC identity and
translation used actual forward and reverse production results; manufactured
combine fixtures are labelled as such and are not fluid test cases.

One code detail matters for attribution: raw MC forward/reverse passes do not
apply the final SL/MC top rejection or positivity floor. Separate pass records
verify that difference. The final MC chain starts at the original input and uses
the forward result as its base; forward/reverse mass changes must not be added
again to that final chain.

## Artifact and scope

Compact [accounting results](accounting-results.json) identify the complete hashed
manifests. Raw inputs, all repeated outputs/traces and snapshots remain locally in:

- `runs/accounting-controls-v1` — 60 base controls.
- `runs/accounting-edge-v1` — 52 additional branch controls.
- `runs/accounting-historical-v1` — 240 recorded historical updates, each repeated.

Reproduce with fresh output roots:

```powershell
cmake --build out/build/x64-Release --config Release --target AdvectionAccounting
python diagnostics/run_advection_accounting.py --output experiments/appearance-attribution/runs/new-accounting
python diagnostics/run_advection_accounting.py --suite edge --output experiments/appearance-attribution/runs/new-edge
python diagnostics/replay_advection_accounting.py --output experiments/appearance-attribution/runs/new-replay
```

The replay requires the preserved `diagnostics/runs/plateau-horizon-v2` archive.
Neither it, the mass audit, nor production shader files were edited. No new 128³
historical run, alternative boundary extension, coupled-flow result or new method
comparison was needed to answer the direct-rejection/floor question. More general
boundary counterfactuals remain untested and are not silently claimed as complete.
