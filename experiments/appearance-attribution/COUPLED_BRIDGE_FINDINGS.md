# Coupled source bridge findings

Status: the coupled source binding and emission schedule pass for registered
scenes A and B at 32³. Pressure selection remains open. These controls do not
measure G1 and do not support an appearance or performance claim.

Governing documents: [research plan](../../RESEARCH_PLAN.md),
[appearance protocol](SPEC.md), and
[coupled pressure controls](COUPLED_PRESSURE_CONTROLS.md).

## Question

Does the validated normalized-source update remain exact when it is inserted into
the evolving smoke step, including the application's scalar ping-pong resources
and the transition from active emission to source-off evolution?

The fresh controls use the fixed coupled configuration: 32³, `dt` stored as
0.0166666675359011 s, 121 steps, emission active through step 120 and inactive at
step 121, corrected clamp MacCormack advection, a closed obstacle-free domain,
zero vorticity confinement and density dissipation, and 256 pressure iterations.
Scene A has one source with integrated rate 0.002 per second; scene B has two
sources with total integrated rate 0.004 per second. Instrumentation is enabled,
so timings from these runs cannot support a performance claim.

## Fresh result

The authoritative runs are:

- `runs/coupled-source-schedule-v1_3-A-n32-i256`
- `runs/coupled-source-schedule-v1_3-B-n32-i256`

Both runs contain 121 contiguous valid step records. At steps 1, 120 and 121,
independent Python reconstruction matches the GPU density and temperature fields
bit for bit. All six audited updates have zero mismatched, nonfinite and negative
cells. The source is active at steps 1 and 120 and inactive at step 121 as
registered; the inactive update is an exact identity for both fields.

Each run also contains twelve density snapshots at steps 0, 12, 24, ..., 120 and
121. Every file has the expected size, finite nonnegative values, and matching
metadata, step and provenance hashes. The declared configuration identity and
hash, source-rate hash, solver settings and step bounds all validate.

D3D12 InfoQueue capture reports zero errors and zero discarded messages in each
run. Each run contains one advisory that buffers ignore an explicit `COPY_DEST`
initial state because buffers begin effectively in `COMMON`; it does not identify
an invalid operation.

The 256-iteration bridge controls fail the registered primary and tight pressure
criteria on all 121 steps. The worst relative residual is 0.0255338337 in scene A
and 0.0372387315 in scene B; the worst `dt * max|divergence|` is 1.3188025e-5 and
2.3630759e-5, respectively. These are 121-step source controls, not the registered
180-step pressure-selection pilot, so they do not select or reject a production
pressure budget. The pressure ladder, selected full-field checks and confirmation
run in [COUPLED_PRESSURE_CONTROLS.md](COUPLED_PRESSURE_CONTROLS.md) remain pending.

## Invalidated preliminary data

`runs/coupled-source-schedule-v1_2-A-n32-i256`, its scene-B counterpart, and the
entire `runs/pressure-selection-v1` root are implementation-debugging artifacts.
They predate a repair for two synchronization defects: an ordinary custom-source
step could bind density as a UAV while its tracked state remained `COPY_SOURCE`
after benchmark readback, and temperature lacked a UAV dependency barrier between
scalar advection and the next source update. They also predate direct InfoQueue
capture and the registered per-step density/configuration hashes. No trajectory,
residual or apparent pressure decision from those roots is evidence.

The invalid files remain on disk with `INVALID.txt` markers so the failure history
is auditable. They must not be pooled with the v1.3 controls or used to shorten the
fresh pressure ladder.

## Decision

The coupled source binding/schedule slice of E0 is closed for both registered
source layouts at 32³. This establishes that the source update is applied to the
intended evolving scalar resources with the registered 120-on/one-off schedule.
It does not validate the pressure budget at 32³, transfer the bridge to 64³ or
128³, complete G0, or measure G1.

Next, restart the finite pressure-selection protocol from fresh output roots using
the repaired harness. Do not render or compare resolution-dependent appearance
until that protocol selects and confirms valid primary and tightened pressure
budgets, or records its predeclared blocker outcome.

## Provenance

| Run | Configuration FNV-1a64 | `analysis.json` SHA-256 | `provenance.json` SHA-256 |
|---|---:|---|---|
| Scene A | 9415485975120296889 | `bc1acfe46b8720a2a4b76bf1d76330b3991077cb18051a26eeccaffd62434221` | `cc3391479d11650d43dbf390eedde475b0e6ed70dbecb0dbe80b0859581b9c26` |
| Scene B | 11418890183680235939 | `e46e5e926a5b965e1a170dfea3f08ae805c26cfe8a3041e95e854cd702c72f98` | `e73983d2e4bd074b516295455a9668bc7ceaba59a80a354a3471d369ec5eca20` |

Each provenance manifest also hashes the executable, runtime shader, source table,
analyzer, monitored source files and every retained artifact.
