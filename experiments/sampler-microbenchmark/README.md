# Isolated sampler microbenchmark

Completed 11 September 2026. The standalone DX12 executable measures linear-clamp
R32_FLOAT texture sampling without loading the renderer or smoke solver.

See [FINDINGS.md](FINDINGS.md), [summary.json](summary.json), and
[the figure](figures/sampler-response.svg). Raw data and source/executable snapshots
are under `diagnostics/runs/sampler-v2`; `sampler-v1` is the exploratory first run.

## Reproduce

From an MSVC developer shell at the repository root:

```powershell
cmake -S . -B out/build/x64-Release
cmake --build out/build/x64-Release --config Release --target SamplerMicrobenchmark
./diagnostics/Run-SamplerMicrobenchmark.ps1 -Python python -RunRoot diagnostics/runs/sampler-new -ReportRoot experiments/sampler-new
```

Python requires NumPy. The runner requires a new raw output directory, snapshots
inputs, checks their hashes after execution, and records device and data provenance.
The executable takes N, a normalized float4 query file, shader path, and output
directory. Three dispatch/readback repetitions run on the same device instance.

The validation and figure scripts currently target the recorded `sampler-v2` run:

```powershell
python diagnostics/test_sampler_validation.py
python diagnostics/plot_sampler.py
```

## Protocol

For each N in {32,33,64}, use base corner (N/3, N/2-1, 3N/8), with integer
division. Sixteen textures contain eight basis stencils, a globally constant
field, an integer ramp, dyadic fractions, two generic [0,1] patterns, a signed
pattern, and scaled/offset variants. Corner order is x+2y+4z. Actual float32 corner
values are saved in `corners.f32`.

Each size has 23,503 queries: eight exact corners, one previous-probe sentinel,
12,291 axis samples, 3,840 near-threshold samples, 3,267 plane samples, and 4,096
deterministic random points (NumPy seed 20260911). Normalized float32 inputs are
saved separately from intended fractions. GPU-reconstructed fractions are checked
against CPU float32 reconstruction. These reconstructed fractions are a diagnostic;
they are not assumed to expose the sampler's internal coordinate conversion.

Models include ideal products and products of independently quantized fractions
at 4–16 bits. Axis samples select a model by maximum basis-weight error; plane
and random points are not used in that selection. Nearest-with-upward-ties was
added after observing the first run's axis tie behavior, so this is exploratory
model characterization, not preregistered confirmation. The basis reconstruction
test uses measured weights at each query to predict other texture values; it is
not a coordinate-only prediction model.

The shader uses cs_5_1, strictness and O3, with a static MIN_MAG_MIP_LINEAR clamp
sampler. No timing or solver-performance conclusions are drawn.
