<#
Analyze the passive-advection resolution sweep: build the error-vs-cost and
error-vs-resolution tables, and plot them.

For each run: final normalized L1 (accuracy) and mean isolated advection time
over post-warmup steps (cost). Grouped by case and scheme so SL and MacCormack
can be compared at matched resolution and matched cost.
#>
param(
    [string]$RunRoot = "$PSScriptRoot/../diagnostics/runs/resolution-sweep",
    [int]$WarmupSteps = 30
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RunRoot)
$summary = @()
foreach ($dir in Get-ChildItem -LiteralPath $root -Directory | Sort-Object Name) {
    $stepsPath = Join-Path $dir.FullName 'steps.csv'
    $manifestPath = Join-Path $dir.FullName 'manifest.json'
    if (-not (Test-Path $stepsPath) -or -not (Test-Path $manifestPath)) { continue }
    $m = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    $rows = @(Import-Csv -LiteralPath $stepsPath)
    $nonfinite = ($rows | Measure-Object -Property nonfinite -Sum).Sum
    $measured = @($rows | Where-Object { [int]$_.step -gt $WarmupSteps })
    $meanMs = ($measured | ForEach-Object { [double]$_.advection_ms } | Measure-Object -Average).Average
    $summary += [pscustomobject][ordered]@{
        case = ($m.scenario -replace 'reference_|_v1','')
        advection = $m.advection
        resolution = [int]$m.resolution[0]
        final_l1_normalized = [double]$rows[-1].l1_normalized
        final_mass_error_rel = [double]$rows[-1].mass_error_rel
        mean_advection_ms = [math]::Round($meanMs, 5)
        nonfinite = $nonfinite
    }
}
if ($summary.Count -eq 0) { throw "No sweep runs found under $root" }
$summary = $summary | Sort-Object case, advection, resolution
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'summary.json')
$summary | Format-Table case,advection,resolution,final_l1_normalized,mean_advection_ms,final_mass_error_rel,nonfinite -AutoSize

# Convergence rate: slope of log(L1) vs log(resolution) per (case, scheme).
Write-Output "`nObserved convergence order (|d log L1 / d log N|; higher = faster error decay):"
$summary | Group-Object case, advection | ForEach-Object {
    $pts = $_.Group | Sort-Object resolution
    if ($pts.Count -ge 2) {
        $x = $pts | ForEach-Object { [math]::Log($_.resolution) }
        $y = $pts | ForEach-Object { [math]::Log([math]::Max($_.final_l1_normalized, 1e-12)) }
        $n = $x.Count; $sx = ($x | Measure-Object -Sum).Sum; $sy = ($y | Measure-Object -Sum).Sum
        $sxx = 0.0; $sxy = 0.0
        for ($i = 0; $i -lt $n; ++$i) { $sxx += $x[$i]*$x[$i]; $sxy += $x[$i]*$y[$i] }
        $slope = ($n*$sxy - $sx*$sy) / ($n*$sxx - $sx*$sx)
        "{0,-14} {1,-11} order ~= {2}" -f $pts[0].case, $pts[0].advection, [math]::Round(-$slope, 2)
    }
}

$python = (Get-Command python -ErrorAction SilentlyContinue)
if ($python) {
    & $python.Source (Join-Path $PSScriptRoot 'plot_resolution_sweep.py') $root
    Write-Output "Plots written under $root/figures"
} else {
    Write-Warning "python not found; skipped plots. summary.json still written."
}
if ($summary.nonfinite -gt 0) { Write-Warning "Some runs produced nonfinite cells; treat those points with caution." }
