<#
Analyze passive-advection reference runs: validate, summarize, and plot.

Two kinds of run:
 - Identity-expected (source-only, static, whole-cell translation): a correct
   scheme reproduces the reference exactly, so normalized L1 should sit at the
   float floor (~1e-6). Anything above -Tolerance is flagged.
 - Real transport (fractional translation, rotation): the error IS the result -
   it measures numerical diffusion. Here we only require finiteness and report
   the final normalized L1 so SL and MacCormack can be compared.

Error plots are written under <RunRoot>/figures via plot_reference.py.
#>
param(
    [string]$RunRoot = "$PSScriptRoot/../diagnostics/runs/reference",
    [double]$Tolerance = 1e-4
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
    $maxL1 = ($rows | Measure-Object -Property l1_normalized -Maximum).Maximum
    $finalL1 = [double]$rows[-1].l1_normalized
    $finalMass = [double]$rows[-1].mass_error_rel
    # Whole-cell translation is an integer shift, hence exact (identity).
    $speed = [double]$m.reference_speed
    $identity = ($m.velocity_mode -eq 0) -or
                ($m.velocity_mode -eq 1 -and [Math]::Abs($speed - [Math]::Round($speed)) -lt 1e-9)
    $pass = if ($identity) { ($nonfinite -eq 0) -and ($maxL1 -le $Tolerance) } else { $nonfinite -eq 0 }
    $summary += [pscustomobject][ordered]@{
        run = $dir.Name
        scenario = $m.scenario
        advection = $m.advection
        speed = $speed
        steps = $rows.Count
        nonfinite = $nonfinite
        identity_expected = $identity
        max_l1_normalized = $maxL1
        final_l1_normalized = $finalL1
        final_mass_error_rel = $finalMass
        pass = $pass
    }
}
if ($summary.Count -eq 0) { throw "No reference runs found under $root" }
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'summary.json')
$summary | Format-Table run,advection,identity_expected,max_l1_normalized,final_l1_normalized,final_mass_error_rel,pass -AutoSize

$python = (Get-Command python -ErrorAction SilentlyContinue)
if ($python) {
    & $python.Source (Join-Path $PSScriptRoot 'plot_reference.py') $root
    Write-Output "Error plots written under $root/figures"
} else {
    Write-Warning "python not found; skipped error plots. summary.json still written."
}

$failed = @($summary | Where-Object { -not $_.pass })
if ($failed.Count) {
    Write-Warning "Runs failing validation (identity cases must be ~0; all cases must be finite):"
    $failed | Format-Table run,identity_expected,max_l1_normalized,nonfinite -AutoSize
} else {
    Write-Output "All reference runs valid. Transport-case errors are in final_l1_normalized above."
}
