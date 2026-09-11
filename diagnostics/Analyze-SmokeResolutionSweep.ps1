<#
Analyze the passive-advection resolution sweep (frozen baseline).

Per (case, scheme, resolution) there is one correctness run (production shaders,
the accuracy of record) and several timing runs (optimized shaders, warmup +
repeats). This script:
 - takes accuracy from the correctness run,
 - takes cost as the MEDIAN mean-advection-ms over the optimized timing runs,
 - VERIFIES optimization invariance: the optimized runs' final L1 must match the
   correctness L1 (else the timing shaders changed the numerics and are untrusted),
 - reports empirical refinement rates (slope of log L1 vs log N) -- an observed
   error-decay rate for THIS smooth test, not a proof of formal convergence order.
#>
param(
    [string]$RunRoot = "$PSScriptRoot/../diagnostics/runs/resolution-sweep",
    [int]$WarmupSteps = 30,
    # Material-change threshold on relative L1, not bit-exactness: optimization may
    # legitimately reorder floating-point ops. A real numerical change is far larger.
    [double]$InvarianceRelTol = 1e-2
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RunRoot)

function Read-Run($dir) {
    $m = Get-Content -Raw -LiteralPath (Join-Path $dir 'manifest.json') | ConvertFrom-Json
    $rows = @(Import-Csv -LiteralPath (Join-Path $dir 'steps.csv'))
    $measured = @($rows | Where-Object { [int]$_.step -gt $WarmupSteps })
    [pscustomobject]@{
        case = ($m.scenario -replace 'reference_|_v1','')
        mode = $m.advection
        resolution = [int]$m.resolution[0]
        optimized = [bool]$m.optimized_shaders
        final_l1 = [double]$rows[-1].l1_normalized
        final_mass = [double]$rows[-1].mass_error_rel
        mean_ms = ($measured | ForEach-Object { [double]$_.advection_ms } | Measure-Object -Average).Average
        nonfinite = ($rows | Measure-Object -Property nonfinite -Sum).Sum
    }
}

$runs = @()
foreach ($dir in Get-ChildItem -LiteralPath $root -Directory) {
    if ((Test-Path (Join-Path $dir.FullName 'manifest.json')) -and (Test-Path (Join-Path $dir.FullName 'steps.csv'))) {
        $runs += Read-Run $dir.FullName
    }
}
if ($runs.Count -eq 0) { throw "No runs found under $root" }

$summary = @()
foreach ($g in $runs | Group-Object case, mode, resolution) {
    $correctness = @($g.Group | Where-Object { -not $_.optimized })
    $timing = @($g.Group | Where-Object { $_.optimized })
    if ($correctness.Count -ne 1) { throw "Expected exactly one correctness run for $($g.Name); found $($correctness.Count)." }
    $c = $correctness[0]
    $costs = @($timing | ForEach-Object { $_.mean_ms } | Sort-Object)
    $median = if ($costs.Count) { $costs[[int][math]::Floor(($costs.Count - 1) / 2)] } else { $null }
    $maxL1Delta = if ($timing.Count) { ($timing | ForEach-Object { [math]::Abs($_.final_l1 - $c.final_l1) } | Measure-Object -Maximum).Maximum } else { 0 }
    $invarianceOk = ($c.final_l1 -le 0) -or ($maxL1Delta / [math]::Max($c.final_l1, 1e-12) -le $InvarianceRelTol)
    $summary += [pscustomobject][ordered]@{
        case = $c.case; advection = $c.mode; resolution = $c.resolution
        final_l1_normalized = $c.final_l1
        final_mass_error_rel = $c.final_mass
        median_advection_ms = if ($median) { [math]::Round($median, 5) } else { $null }
        timing_runs = $timing.Count
        opt_vs_prod_max_l1_delta = $maxL1Delta
        optimization_invariant = $invarianceOk
        nonfinite = $c.nonfinite
    }
}
$summary = $summary | Sort-Object case, advection, resolution
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'summary.json')
$summary | Format-Table case,advection,resolution,final_l1_normalized,median_advection_ms,optimization_invariant,nonfinite -AutoSize

Write-Output "`nEmpirical refinement rate (|d log L1 / d log N| for this smooth test; not a formal order proof):"
$summary | Group-Object case, advection | ForEach-Object {
    $pts = $_.Group | Sort-Object resolution
    if ($pts.Count -ge 2) {
        $x = $pts | ForEach-Object { [math]::Log($_.resolution) }
        $y = $pts | ForEach-Object { [math]::Log([math]::Max($_.final_l1_normalized, 1e-12)) }
        $n = $x.Count; $sx = ($x | Measure-Object -Sum).Sum; $sy = ($y | Measure-Object -Sum).Sum
        $sxx = 0.0; $sxy = 0.0
        for ($i = 0; $i -lt $n; ++$i) { $sxx += $x[$i]*$x[$i]; $sxy += $x[$i]*$y[$i] }
        $slope = ($n*$sxy - $sx*$sy) / ($n*$sxx - $sx*$sx)
        "{0,-14} {1,-11} rate ~= {2}" -f $pts[0].case, $pts[0].advection, [math]::Round(-$slope, 2)
    }
}

$badInvariance = @($summary | Where-Object { -not $_.optimization_invariant })
if ($badInvariance.Count) {
    Write-Warning "OPTIMIZATION CHANGED THE NUMERICS for these configs (timing untrusted):"
    $badInvariance | Format-Table case,advection,resolution,opt_vs_prod_max_l1_delta -AutoSize
} else {
    Write-Output "`nOptimization invariant: optimized timing shaders reproduce the production-run accuracy for every config."
}
if (($summary.nonfinite | Measure-Object -Sum).Sum -gt 0) { Write-Warning "Some runs produced nonfinite cells." }

$python = (Get-Command python -ErrorAction SilentlyContinue)
if ($python) {
    & $python.Source (Join-Path $PSScriptRoot 'plot_resolution_sweep.py') $root
    Write-Output "Plots written under $root/figures"
} else { Write-Warning "python not found; skipped plots." }
