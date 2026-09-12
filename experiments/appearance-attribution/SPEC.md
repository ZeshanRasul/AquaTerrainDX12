# E0/E1: appearance validity, attribution and coupled-flow gate

Protocol v1.2, 12 September 2026; prospective for G1. Governing document:
[RESEARCH_PLAN.md](../../RESEARCH_PLAN.md). Rendering, source-table, standalone
source-injection and accounting controls have run; no coupled G1 output has been
inspected.
Earlier mass/plateau results are motivation, not validation of G1.

Accuracy amendment: E0 v1.0's 0.02 ray step failed the fixed 0.003 refinement bar
(worst mean absolute alpha 0.00331272). Its 0.01 step measured 0.00141828 against
0.005. Select 0.01 and 192 iterations for all future primary captures and repeat
E0 in a new root. This is control-informed instrument calibration, not independent
confirmation of a scientific claim. Preserve v1.0 and its failed decision. No
source, camera, extinction, ROI or G1 threshold changes; no G1 outputs inspected.

## Fixed measurement definition

Render 512x512 offscreen images with fixed cameras in normalized domain coordinates:
(2.5,1.5,2.5),(-1.5,1.5,2.5),(0.5,1.5,-2). All look at (0.5,0.5,0.5), up +y,
vertical FOV 35 degrees. Map the physical unit cube consistently into the renderer;
do not infer dimensions from the existing smoke mesh's non-unit box dimensions.
Capture linear premultiplied RGB and accumulated alpha before display transforms.
Use transparent black for alpha capture; also composite on fixed linear gray 0.18
for inspection. No auto exposure, camera tracking, per-grid material adjustments,
temporal accumulation or selective frame choice.

Use the production pixel_smoke.hlsl extinction/integration formula. Fixed primary
constants: density scale 1, absorption 4, smoke colour (0.7,0.7,0.7), normalized
light direction (1,-1,1). Ray-step length is 0.01 in normalized texture coordinates,
with 192 iterations, fixed screen-space jitter as in production and fixed
scene depth representing no occlusion. The diagnostic loop must not truncate a
ray: 192 steps cover more than the unit cube's sqrt(3) maximum path. Preserve
the current shader's alpha cutoff and early-termination behavior in the primary
capture. No screenshots or synthetic plots substitute for shader-derived images.

E0 checks the same continuous static field sampled at all resolutions, constant
density, empty density, and repeated captures. Also refine ray step to 0.01 and
0.005 using a separate diagnostic variant with sufficient loop bounds; never let
the step cap confound refinement. Coarse/fine rendered mean-alpha disagreement
must be <0.003 under the primary metric. If it fails, E0 is invalid: a prospective
amendment may change rendering accuracy before G1 runs, not opacity gain to make
the effect larger. An E0 control does not count toward the G1 effect.

E0 implementation registration (before capture): constant density 0.25; Gaussian
peak 1, centre (0.5,0.5,0.5), sigma 0.15 in all axes, sampled at cell centres.
Run every field/resolution/step combination, three repeats and all three views.
Compare each coarser ray step to 0.005 on the same density texture; report
cross-resolution static-field discrepancies separately from ray refinement.
For these E0 controls only, the common ROI includes the union across all tested
ray steps as well as resolutions. G1 retains its primary-render-only ROI.
Empty images must be exactly zero; all outputs must be finite and valid
premultiplied RGBA. Constant-field alpha is also checked against the analytic
Beer-Lambert value along the unit-box ray (discrete marching error is reported).
For constant density 0.25 and absorption 4, extinction is 1: jittered marching
changes integrated path length by at most one ray step. At the 0.005 reference
step, require maximum analytic alpha error <=0.0051 (one step plus 0.0001 numeric
allowance) and mean error <0.003. This bound is an integration check, not an
additional smoke-effect threshold.
Capture uses a full-screen triangle with analytic box-entry coordinates feeding
the production pixel shader, a float render target cleared to transparent black,
and a depth texture filled with 10 to disable occlusion. It does not test the
application's mesh rasterization or compositing. The production shader is compiled
from its current source; only its 96-iteration bound changes for refinement.
Save that exact compilation input. No production shader or simulation is edited.

At each time/view, define one common ROI as pixels where any uncorrected resolution
has alpha >=0.05. Use that same ROI for all resolutions and later remedies. Require
at least 1024 pixels; otherwise the sample cannot satisfy G1. The comparison is
mean absolute alpha difference on that ROI, a dimensionless absolute difference
(0.03 means three alpha percentage points). Also report full projected-domain
error, signed integrated alpha, threshold coverage and RGB differences so the ROI
does not conceal displaced or weak smoke. For E2/E3, freeze ROIs from uncorrected
inputs; remedies cannot redefine their scoring mask.

Sample t=0,0.2,...,6 seconds. G1 requires error >=0.03 between 32³ and 128³ for
three consecutive samples in at least two fixed views, separately in **both**
coupled scenes A and B. Different views may qualify in the two scenes. Do not
discard inconvenient times; publish every view/time and 64³ comparisons too.
Saturation or insufficient ROI does not license changing absorption after seeing
G1 results. If fewer than three eligible consecutive samples exist, G1 fails.

For G2, primary integrated opacity discrepancy is the equal-weight mean of the
per-view/time ROI errors, over all eligible samples from the uncorrected data.
Evaluate improvements separately for 32³ and 64³ against uncorrected 128³, and
separately for each evaluation scene. Any zero-baseline discrepancy makes a
relative-improvement claim inapplicable; do not divide by a chosen epsilon.

Persistence duration: number of post-source-off samples with at least 1024 pixels
at alpha>=0.05, multiplied by 0.2 s, for each view. Report right-censoring if smoke
still qualifies at 6 s; do not invent a disappearance time. This is a reproducible
appearance metric, not a human detection threshold. G2 persistence constraints
apply only to uncensored comparisons; censored cases are explicitly inconclusive.

## Coupled scenes and the regime bridge

Primary simulation: closed unit cube; evolving MAC velocity, advection, buoyancy,
pressure projection and scalar transport; no obstacle or confinement. Fixed
dt=1/60 s, duration 6 s, grid32/64/128, clamp MacCormack, no density dissipation,
temperature cooling 0.5/s, buoyancy coefficient 0.6, smoke weight 0.05, ambient0.
These are research presets, not claimed optimal game settings.

Sources are stationary spheres, radius0.06 domain lengths. Total integrated density
injection per active source is 0.002 domain-volume*density units per second;
temperature increment uses the same integrated rate in its units. Evaluate sphere
cell coverage with deterministic CPU quadrature and normalize source weights to
that integral at every resolution. Verify the discrete injected totals rather than
using one source cell or matching peak values. Increase quadrature accuracy in E0
if its discretization changes the measured controls; document the final rule before
G1. In A/B, both density and temperature sources shut off at t=2 s; C uses the
pulse schedule below.

Source-control registration before source-table measurements: midpoint subcell
quadrature 16 cubed, checked against 32 cubed; classify fully inside/outside cells
analytically and sample boundary cells only. Normalize each sphere in float64,
store rates as R32_FLOAT in z,y,x order, and require relative integrated-rate
error <=1e-7 after storage. Compare static images of the A/B source rate fields
multiplied by one second (no velocity, buoyancy or time integration) at the primary
0.01 ray step. Each view uses the union ROI across all resolutions and both
quadrature levels, consistent with the common-mask rule above, and must have
>=1024 ROI pixels and quadrature-refinement
mean absolute alpha <0.003. These controls do not count as G1 scenes and do not
validate actual GPU injection; the coupled harness must audit that separately.
Do not generate held-out C fields in E0.

- A: one source at (0.5,0.15,0.5), initially still air. Calibration scene.
- B: two sources at (0.35,0.15,0.5) and (0.65,0.15,0.5), initially still air.
  G1 confirmation and untuned remedy evaluation; baseline is necessarily inspected
  at G1, so do not call B a completely unseen scene.
- C: source at (0.4,0.15,0.5), active only during t=[0,0.5) and [1,1.5), with
  initial discrete-curl circulation of the previously validated streamfunction
  psi=0.12 sin²(pi*x)sin²(pi*y). Reserve all C outputs until the correction and
  fitting procedure are frozen. C is the strict held-out transfer scene, not a
  way to rescue G1 if A or B fails.

Pressure is a controlled confound, not a research variable here. The prospective
[coupled-pressure protocol](COUPLED_PRESSURE_CONTROLS.md) fixes the raw-step
metrics, numerical-zero convention, 180-step selection runs, finite candidate
ladder, full-field checks and six-second confirmation. Its primary targets are
relative residual <=1e-4 AND dt*max|div|<=1e-5; its distinct higher-budget targets
are tightened by 10x. Before revealing G1, every eligible primary-versus-tight
image must differ by <0.003 mean absolute alpha. This expensive accuracy
configuration is not the runtime claim. Failure at the registered finite cap is
a feasibility blocker rather than permission to start another solver project.

Source geometry, rendering-only resolution artifacts, pressure, boundaries and
advection can all affect the result. Repeat the scored setup three times; record
field/image repeatability on each device. Start on5090; replicate the decisive
G1/remedy results on3070 before generalizing across the two tested configurations.
Timing and correctness runs are separate and optimization invariance is required.

## Attribution cases (E1a)

Reproduce original plateau-horizon reference setup first at32/64,120steps. Add128
only after the instrumentation checks pass. Keep its physical setup and step count
explicitly separate from the six-second coupled scenes.

Control family: periodic constant translation; periodic spatially varying
divergence-free transport; original closed-domain spatially varying transport.
Construct the periodic field as a discrete curl of a periodic vertex potential,
so discrete divergence is checked independently. Constant translation is the
simple conservation/control case, not a decisive boundary-loss attribution.

New reference-only counters/readbacks must record signed changes with a declared
evaluation order: pre-advection field, unrestricted sampled value, explicit
boundary rejection, positivity flooring, intentional damping and final output.
For MacCormack, account for forward/reverse sampling, correction and limiter
changes explicitly; a single SL sampling budget is not a budget of its final update.
Include solid-destination clearing when applicable. Every named term must have
an implemented counterfactual and the budget must telescope to the actual update
within a numerically justified accumulation tolerance fixed in E0.

Count departures outside the domain, departures outside texel-centre bounds,
boundary-stencil involvement, rejected samples and negative samples separately.
Clamping is not itself a uniquely additive loss term: compare it against an
explicit alternative only when that alternative has a defined physical/analytic
extension. Do not label a periodic-vs-closed difference "boundary loss" when the
velocity field also changed. Add a padded-domain comparison preserving the same
local field and initial blob if boundary involvement needs causal attribution;
the local velocity extension and boundary treatment must be written down first.

Report integrated mass, peak, second moments/spread and rendered alpha separately.
Mass conservation does not imply absence of diffusion, and mass loss does not
alone establish reduced visibility. Include numerical velocity divergence in the
accounting interpretation. A periodic probe must wrap its Load indices or reject
the case; the current clamped ProbeInterpolation must not be reused silently.

## Artifact and gate record

Deliver manifests, counter definitions, source/executable hashes, raw numeric
fields, linear alpha/RGB captures, all comparison frames, uninstrumented timing,
validation failures, and a short G0/G1 decision referencing the central plan.
All image settings and source normalization constants must be serialized, not
left as GUI state. No fitting, new limiter or calibration implementation before
the coupled visibility gate is recorded. On a valid G1 failure, follow F1 in the
central plan immediately; on measurement failure, report G0 as invalid.
