# Findings: the rounded-coordinate model is incomplete

On the tested RTX 5090 and driver 9007199255733668, the isolated sampler reproduces
the previous transport probe's discrepancy exactly. At fractions approximately
(0.9881506, 0.1764555, 0), its four nonzero-plane basis weights are
(2,209,1,44)/256. The upper-right corner therefore contributes **0.171875**, whereas
multiplying independently rounded 1/256 fractions predicts **0.1737213134765625**.
This observation isolates the discrepancy from pressure solving and advection.

## What the measurements establish

- Across all 70,509 queried positions (three texture sizes), every measured basis
  weight is an exact multiple of 1/256. Their sums equal one exactly.
- The weights are not separable into three independent linear weights. Even using
  the measured x/y/z marginals, the maximum corner-weight reconstruction error is
  0.0052318572998046875 at each size. This is much larger than the rounding error
  observed when predicting arbitrary values from the measured basis weights.
- At N=32 and N=64, nearest 1/256 rounding with upward ties reproduces all axis
  and near-threshold basis samples exactly. It fails on multidimensional samples:
  maximum held-out random basis error is 0.00376129150390625. Correcting the tie
  convention alone therefore does not repair the previous model.
- At N=33, the same axis model is not exact. Explicit float32 reconstruction of
  normalized coordinates need not match the sampler's internal conversion near
  thresholds. The summary's minimax-selected `float` label at this size is a tied
  minimum among inadequate candidates, not evidence for full-precision sampling.
- Dotting the measured eight weights with the two generic [0,1] corner patterns
  predicts outputs to a maximum absolute error of 2.9802322387695312e-8. The signed
  pattern's maximum is 9.73232090473175e-8. The integer and dyadic patterns are exact.
  Multiplying the generic pattern by 256 commutes with sampling exactly; adding
  eight agrees within 8.344650268554688e-7. The scaled pattern's absolute dot-product
  error is 7.62939453125e-6, so errors must be interpreted relative to value scale.

Together these support describing the observed response as quantized, nonseparable
effective corner weights, with nearly linear dependence on the supplied corner
values. They do not identify a unique hardware algorithm or a universal precision
rule. The measured weights themselves are not a cheap coordinate-only emulator.

## Verification

Release target built successfully. Each size ran three times, giving 211,527 query
evaluations and 3,384,432 texture samples. All repeated output buffers were bitwise
identical. Exact corners, constant preservation, finite outputs, dimensions, and
GPU/CPU coordinate reconstruction passed. Six corruption checks confirmed rejection
of changed repeats, nonfinite samples, wrong corners, wrong coordinates, broken
constant preservation, and truncated results. The figure was rendered and inspected.

Raw data, inputs, executable/source snapshots, hashes, and device descriptions are
recorded under `diagnostics/runs/sampler-v2`. No smoke-solver kernels were changed
for this experiment. This single-device result does not establish portability,
mass conservation, solver convergence, or a useful pressure stopping criterion.

## Recommended next experiment

Make one controlled intervention in the existing frozen-velocity transport test:
replace only the final density SampleLevel with eight explicit loads and float
interpolation. Keep projection, velocity sampling, RK2 departures, input density,
boundaries, time steps, and budgets fixed. Compare the original and replacement
paths against their own high-budget references at N=32/64, for the original plateau
and the already tested phase/time-step perturbations, over one and two steps.

The primary question is whether candidate/reference density differences emerge
under explicit interpolation where the hardware sampler gives exact equality.
The existing manual probe suggests they will at the original one-step plateau;
running the actual update tests propagation into the second step. Record changed
cells, normalized L1, extrema and mass; verify unchanged departures. This is an
ablation to measure sampler dependence, not a proposal to ship slower sampling.

For the paper, the useful direction is demonstrating when an apparent transport
plateau is a sampling-dependent observation and how to diagnose it reliably.
Cross-device replication and a validated actionable diagnostic remain necessary
before making a broad practical claim. This microbenchmark alone is not a new
fluid method or a publication contribution.
