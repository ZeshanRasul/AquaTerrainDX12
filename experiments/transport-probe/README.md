# Departure and scalar-sampling probe

This diagnostic follows the density-plateau perturbation experiment. It compares
candidate and reference velocities at the characteristic-tracing and scalar-sampling
stages, without modifying the production SL advection formula.

## Recorded data

A separate compute shader runs before each SL step using the exact input density,
projected velocity, and constants. It records four float4 values per cell:

1. Departure xyz and the hardware scalar sample.
2. Midpoint xyz and manual float-weight trilinear interpolation.
3. Fractional stencil coordinates xyz and manual interpolation with fractions
   rounded to multiples of 1/256.
4. Stencil minimum, stencil maximum, input density, and actual production output.

The actual output is recorded after the unchanged production dispatch. Both steps
are captured, so the second probe's input must equal the first production output.
The final captured output must equal the independent full-density readback.

The 1/256 model is an explicit diagnostic hypothesis. It is **not** assumed to
represent the sampler implementation, and discrepancies are reported. Manual
float interpolation is also a diagnostic comparison, not a proposed replacement
advection scheme or a measurement of continuum transport accuracy.

## Matrix

Five cases at each of 32³ and 64³:

- Original sharp-density case, dt = 1/60, no shift.
- Sharp density shifted 0.75 cells at 32³ or 0.25 cells at 64³, original dt.
- Unshifted sharp density, double timestep.
- Sharp density shifted 0.5 cells, half timestep.
- Original smooth density, original dt.

All use two scalar steps with one projection and frozen projected velocity. Each
case has candidate pressure iterations (512 or 2048), 4096, 8192, and the independent
divergence-free velocity control (-1). Every configuration runs with the probe
enabled and disabled: **80 launches, three reset trials each**. All measured kernels
use the optimized build. The smaller `-Pilot` covers only the original 32³ sharp case.

## Validation

- All eight simulation fields must match the probe-disabled control bit for bit.
- Probe records must repeat bit for bit across the three reset trials.
- Every probe's hardware sample must equal the actual production output bit for bit.
- Input fields must chain correctly between the two steps.
- CPU reconstruction independently checks stencil bounds, fractional coordinates,
  and both manual interpolation models (absolute tolerance 5e-7 for interpolation).
  Coordinate-to-fraction arithmetic matches the GPU's float32 operations, including
  values just outside the first cell center.
- Inherited pressure/divergence consistency, finite-field and closed-wall checks apply.
- Both step references must agree with 4096 iterations and the independent velocity
  control within normalized L1 2e-6, with relative pressure residual below 1e-4.

Probe dispatches and readbacks are inside the step timestamp interval. Manifests
explicitly mark these timings as instrumented; **no performance claims** use them.
Only simulation-field invariance establishes that the probe is passive numerically.

## Run

Reconfigure and build Release to include `SmokeTransportProbe.cpp`, then:

```powershell
diagnostics/Run-SmokeTransportProbe.ps1 -OutputRoot diagnostics/runs/transport-probe-v1
python diagnostics/analyze_transport_probe.py diagnostics/runs/transport-probe-v1
```

Use a fresh output directory. The analysis requires NumPy. Test damaged-trace
rejection against the completed pilot:

```powershell
python diagnostics/test_transport_probe.py diagnostics/runs/transport-probe-pilot
```

Raw `trace-step1.f32` and `trace-step2.f32` are little-endian float32 arrays with
shape `(N,N,N,4,4)`, ordered z/y/x/record/component. Sources and the explicit matrix
are snapshotted beside the raw runs, with executable and runtime-shader hashes.
The probe uses the existing u15 root UAV slot; it adds no production constant-buffer
fields or descriptor-table reordering.
