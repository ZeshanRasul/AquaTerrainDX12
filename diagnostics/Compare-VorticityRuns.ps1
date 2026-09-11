<#
.SYNOPSIS
Compare two matched GPU smoke runs that differ only in vorticity-confinement
strength (epsilon), as produced by "Start matched A/B benchmark". Hold the
advection mode fixed, run once at epsilon = 0 and once at epsilon > 0.

.EXAMPLE
.\diagnostics\Compare-VorticityRuns.ps1 `
    -BaselineRun "diagnostics\runs\<eps0>_gpu_jacobi" `
    -ConfinedRun "diagnostics\runs\<epsN>_gpu_jacobi"

Both directories must contain manifest.json and steps.csv. The script refuses to
compare runs whose grid, schedule, physics or advection mode differ, so a
reported difference is attributable to vorticity confinement alone.
#>
param(
    [Parameter(Mandatory)][string]$BaselineRun,   # epsilon = 0
    [Parameter(Mandatory)][string]$ConfinedRun    # epsilon > 0
)
$ErrorActionPreference = 'Stop'

$a = Get-Content -LiteralPath (Join-Path $BaselineRun 'manifest.json') -Raw | ConvertFrom-Json
$b = Get-Content -LiteralPath (Join-Path $ConfinedRun 'manifest.json') -Raw | ConvertFrom-Json

function Assert-Match($name, $left, $right) {
    $l = @($left); $r = @($right)
    if ($l.Count -ne $r.Count) { throw "$name differs between runs; configurations are not matched." }
    for ($i = 0; $i -lt $l.Count; ++$i) {
        if ([math]::Abs([double]$l[$i] - [double]$r[$i]) -gt 1e-8) {
            throw "$name differs between runs; configurations are not matched."
        }
    }
}

# Everything but the vorticity strength must match.
if ($a.scenario -ne $b.scenario) { throw 'Scenario differs between runs.' }
if ($a.advection_mode -ne $b.advection_mode) { throw 'Advection mode differs; hold it fixed for a vorticity A/B.' }
Assert-Match 'Resolution'          $a.resolution        $b.resolution
Assert-Match 'Grid spacing'        $a.grid_spacing      $b.grid_spacing
Assert-Match 'Time step'           $a.time_step_s       $b.time_step_s
Assert-Match 'Total steps'         $a.total_steps       $b.total_steps
Assert-Match 'Emitter steps'       $a.emitter_steps     $b.emitter_steps
Assert-Match 'Pressure iterations' $a.pressure_iterations $b.pressure_iterations
foreach ($f in @('fluid_density','ambient_temperature','temperature_buoyancy','smoke_weight',
    'density_dissipation_per_s','temperature_cooling_per_s')) {
    Assert-Match $f $a.$f $b.$f
}
if ([double]$a.vorticity_epsilon -ne 0) {
    Write-Warning "BaselineRun reports vorticity_epsilon=$($a.vorticity_epsilon); expected 0."
}
if ([double]$a.vorticity_epsilon -eq [double]$b.vorticity_epsilon) {
    Write-Warning "Both runs report vorticity_epsilon=$($a.vorticity_epsilon). Did you change the slider between runs?"
}

$stepsA = @(Import-Csv -LiteralPath (Join-Path $BaselineRun 'steps.csv'))
$stepsB = @(Import-Csv -LiteralPath (Join-Path $ConfinedRun 'steps.csv'))
$warmup = [int]$a.warmup_steps

function Measured($steps) { @($steps | Where-Object { [int]$_.step_index -gt $warmup }) }
function Mean($steps, $col) { ((Measured $steps) | ForEach-Object { [double]$_.$col } | Measure-Object -Average).Average }
function Peak($steps, $col) { (($steps | ForEach-Object { [double]$_.$col }) | Measure-Object -Maximum).Maximum }

$eEns = Mean $stepsB 'enstrophy'; $bEns = Mean $stepsA 'enstrophy'
$eKe  = Mean $stepsB 'kinetic_energy'; $bKe = Mean $stepsA 'kinetic_energy'

$result = [pscustomobject]@{
    AdvectionMode          = $a.advection_mode
    Baseline_epsilon       = [double]$a.vorticity_epsilon
    Confined_epsilon       = [double]$b.vorticity_epsilon
    Resolution             = ($a.resolution -join 'x')
    StepsAveraged          = (Measured $stepsA).Count
    Baseline_SolverMeanMs  = [math]::Round((Mean $stepsA 'solver_gpu_ms'), 4)
    Confined_SolverMeanMs  = [math]::Round((Mean $stepsB 'solver_gpu_ms'), 4)
    SolverMs_Overhead      = ('{0:P1}' -f (((Mean $stepsB 'solver_gpu_ms') / (Mean $stepsA 'solver_gpu_ms')) - 1))
    Baseline_MeanEnstrophy = [math]::Round($bEns, 6)
    Confined_MeanEnstrophy = [math]::Round($eEns, 6)
    Enstrophy_Ratio        = [math]::Round($eEns / [math]::Max(1e-12, $bEns), 3)
    Baseline_MeanKE        = [math]::Round($bKe, 6)
    Confined_MeanKE        = [math]::Round($eKe, 6)
    KE_Ratio               = [math]::Round($eKe / [math]::Max(1e-12, $bKe), 3)
    Baseline_PeakVorticity = [math]::Round((Peak $stepsA 'max_vorticity'), 3)
    Confined_PeakVorticity = [math]::Round((Peak $stepsB 'max_vorticity'), 3)
}
$result | Format-List

Write-Output 'Enstrophy = integral of |curl(u)|^2; the small-scale rotational energy vorticity confinement is meant to sustain.'
Write-Output 'A well-tuned epsilon raises sustained enstrophy and kinetic energy (more swirl kept alive) for a few percent solver time.'
$nf = @(($stepsA + $stepsB) | Where-Object { [int]$_.nonfinite_density_cells -gt 0 }).Count
if ($nf -gt 0) { Write-Warning 'A run contains nonfinite density cells; treat its numbers with caution.' }
