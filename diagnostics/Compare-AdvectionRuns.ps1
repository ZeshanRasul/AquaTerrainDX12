<#
.SYNOPSIS
Compare two matched GPU smoke runs that differ only in advection scheme
(Semi-Lagrangian vs MacCormack), as produced by "Start matched A/B benchmark".

.EXAMPLE
.\diagnostics\Compare-AdvectionRuns.ps1 `
    -RunA "diagnostics\runs\<sl_epoch>_gpu_jacobi" `
    -RunB "diagnostics\runs\<maccormack_epoch>_gpu_jacobi"

Both directories must contain manifest.json and steps.csv. The script refuses to
compare runs whose grid, schedule, source or physics differ, so a reported
difference is attributable to the advection scheme alone.
#>
param(
    [Parameter(Mandatory)][string]$RunA,
    [Parameter(Mandatory)][string]$RunB
)
$ErrorActionPreference = 'Stop'

$a = Get-Content -LiteralPath (Join-Path $RunA 'manifest.json') -Raw | ConvertFrom-Json
$b = Get-Content -LiteralPath (Join-Path $RunB 'manifest.json') -Raw | ConvertFrom-Json

function Assert-Match($name, $left, $right) {
    $l = @($left); $r = @($right)
    if ($l.Count -ne $r.Count) { throw "$name differs between runs; configurations are not matched." }
    for ($i = 0; $i -lt $l.Count; ++$i) {
        if ([math]::Abs([double]$l[$i] - [double]$r[$i]) -gt 1e-8) {
            throw "$name differs between runs; configurations are not matched."
        }
    }
}

# The comparison is only meaningful if everything BUT the advection scheme matches.
if ($a.scenario -ne $b.scenario) { throw 'Scenario differs between runs.' }
Assert-Match 'Resolution'          $a.resolution        $b.resolution
Assert-Match 'Grid spacing'        $a.grid_spacing      $b.grid_spacing
Assert-Match 'Time step'           $a.time_step_s       $b.time_step_s
Assert-Match 'Total steps'         $a.total_steps       $b.total_steps
Assert-Match 'Emitter steps'       $a.emitter_steps     $b.emitter_steps
Assert-Match 'Source cell'         $a.source_cell       $b.source_cell
Assert-Match 'Pressure iterations' $a.pressure_iterations $b.pressure_iterations
foreach ($f in @('fluid_density','ambient_temperature','temperature_buoyancy','smoke_weight',
    'density_dissipation_per_s','temperature_cooling_per_s')) {
    Assert-Match $f $a.$f $b.$f
}
if ("$($a.open_top_enabled)" -ne "$($b.open_top_enabled)") { throw 'Open-top setting differs between runs.' }
if ($a.advection_mode -eq $b.advection_mode) {
    Write-Warning "Both runs report advection_mode=$($a.advection_mode). Did you switch the mode between runs?"
}

$stepsA = @(Import-Csv -LiteralPath (Join-Path $RunA 'steps.csv'))
$stepsB = @(Import-Csv -LiteralPath (Join-Path $RunB 'steps.csv'))
$warmup = [int]$a.warmup_steps

function Measured($steps) { @($steps | Where-Object { [int]$_.step_index -gt $warmup }) }
function MeanMs($steps)   { ((Measured $steps) | ForEach-Object { [double]$_.solver_gpu_ms } | Measure-Object -Average).Average }
function P95Ms($steps) {
    $v = @((Measured $steps) | ForEach-Object { [double]$_.solver_gpu_ms } | Sort-Object)
    if ($v.Count -eq 0) { return 0.0 }
    $v[[math]::Min($v.Count - 1, [int][math]::Floor(0.95 * ($v.Count - 1)))]
}
function PeakDensity($steps) { (($steps | ForEach-Object { [double]$_.density_max }) | Measure-Object -Maximum).Maximum }
function FinalIntegral($steps) { [double]($steps | Sort-Object { [int]$_.step_index } | Select-Object -Last 1).density_integral }
# Integral at the last emitting step: mass present when the source shuts off,
# i.e. the amount each scheme then either conserves or diffuses away.
function IntegralAtEmitterEnd($steps) {
    $e = [int]$a.emitter_steps
    $row = $steps | Where-Object { [int]$_.step_index -le $e } | Sort-Object { [int]$_.step_index } | Select-Object -Last 1
    if ($null -eq $row) { return 0.0 }
    [double]$row.density_integral
}

$nonfinite = @(($stepsA + $stepsB) | Where-Object { [int]$_.nonfinite_density_cells -gt 0 }).Count

$result = [pscustomobject]@{
    RunA_mode              = $a.advection_mode
    RunB_mode              = $b.advection_mode
    Resolution             = ($a.resolution -join 'x')
    StepsCompared          = (Measured $stepsA).Count
    WarmupExcluded         = $warmup
    A_SolverMeanMs         = [math]::Round((MeanMs $stepsA), 4)
    B_SolverMeanMs         = [math]::Round((MeanMs $stepsB), 4)
    SolverMs_B_over_A      = [math]::Round((MeanMs $stepsB) / (MeanMs $stepsA), 3)
    A_SolverP95Ms          = [math]::Round((P95Ms $stepsA), 4)
    B_SolverP95Ms          = [math]::Round((P95Ms $stepsB), 4)
    A_IntegralAtEmitterEnd = [math]::Round((IntegralAtEmitterEnd $stepsA), 6)
    B_IntegralAtEmitterEnd = [math]::Round((IntegralAtEmitterEnd $stepsB), 6)
    A_FinalIntegral        = [math]::Round((FinalIntegral $stepsA), 6)
    B_FinalIntegral        = [math]::Round((FinalIntegral $stepsB), 6)
    A_RetainedFraction     = [math]::Round((FinalIntegral $stepsA) / [math]::Max(1e-12, (IntegralAtEmitterEnd $stepsA)), 4)
    B_RetainedFraction     = [math]::Round((FinalIntegral $stepsB) / [math]::Max(1e-12, (IntegralAtEmitterEnd $stepsB)), 4)
    A_PeakDensity          = [math]::Round((PeakDensity $stepsA), 4)
    B_PeakDensity          = [math]::Round((PeakDensity $stepsB), 4)
}
$result | Format-List

Write-Output 'RetainedFraction = final density integral / integral when the emitter stopped.'
Write-Output 'A scheme with less numerical diffusion retains more mass (higher fraction) and a higher peak density.'
Write-Output 'Solver ms is GPU execution time for the solver stages only (excludes volume rendering and readback).'
if ($nonfinite -gt 0) { Write-Warning 'A run contains nonfinite density cells; treat its numbers with caution.' }
if (!$a.completed -or !$b.completed) { Write-Warning 'At least one run is partial (Stop-and-save); only recorded steps were used.' }
