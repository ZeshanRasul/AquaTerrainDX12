param(
    [Parameter(Mandatory)][string]$CpuRun,
    [Parameter(Mandatory)][string]$GpuRun
)
$ErrorActionPreference = 'Stop'
$cpu = Get-Content -LiteralPath (Join-Path $CpuRun 'run.json') -Raw | ConvertFrom-Json
$gpu = Get-Content -LiteralPath (Join-Path $GpuRun 'manifest.json') -Raw | ConvertFrom-Json
function Assert-Match($name, $left, $right) {
    $a = @($left); $b = @($right)
    if ($a.Count -ne $b.Count) { throw "$name differs between runs." }
    for ($i = 0; $i -lt $a.Count; ++$i) {
        if ([math]::Abs([double]$a[$i] - [double]$b[$i]) -gt 1e-8) {
            throw "$name differs between runs. Use matching configurations."
        }
    }
}
if ($cpu.scenario -ne $gpu.scenario) { throw 'Scenario differs between runs.' }
Assert-Match 'Resolution' $cpu.grid.resolution $gpu.resolution
Assert-Match 'Spacing' $cpu.grid.spacing $gpu.grid_spacing
Assert-Match 'Origin' $cpu.grid.origin $gpu.origin
Assert-Match 'Time step' $cpu.schedule.fixed_time_step_s $gpu.time_step_s
Assert-Match 'Emitter steps' $cpu.schedule.emitter_steps $gpu.emitter_steps
Assert-Match 'Source cell' $cpu.emitter.cell $gpu.source_cell
Assert-Match 'Density rate' $cpu.emitter.density_rate_per_s $gpu.density_rate
Assert-Match 'Temperature rate' $cpu.emitter.temperature_rate_per_s $gpu.temperature_rate
Assert-Match 'Source acceleration' $cpu.emitter.acceleration $gpu.source_acceleration
foreach ($field in @('fluid_density','ambient_temperature','temperature_buoyancy','smoke_weight',
    'density_dissipation_per_s','temperature_cooling_per_s')) {
    Assert-Match $field $cpu.physics.$field $gpu.$field
}
Assert-Match 'Rendering enabled' $cpu.measurement.rendering_enabled_during_run $gpu.rendering_enabled
if ($cpu.build.configuration -ne $gpu.build) { throw 'Build configurations differ.' }
$cpuSteps = @(Import-Csv -LiteralPath (Join-Path $CpuRun 'steps.csv'))
$gpuSteps = @(Import-Csv -LiteralPath (Join-Path $GpuRun 'steps.csv'))
$warmup = [math]::Max([int]$cpu.schedule.performance_warmup_steps, [int]$gpu.warmup_steps)
$lookup = @{}
foreach ($row in $cpuSteps) { $lookup[[int]$row.step_index] = $row }
$pairs = @($gpuSteps | Where-Object { $lookup.ContainsKey([int]$_.step_index) })
$measured = @($pairs | Where-Object { [int]$_.step_index -gt $warmup })
if ($measured.Count -eq 0) { throw 'No shared post-warmup steps to compare.' }
$cpuMs = ($measured | ForEach-Object { [double]$lookup[[int]$_.step_index].solver_cpu_ms } | Measure-Object -Average).Average
$gpuMs = ($measured | ForEach-Object { [double]$_.solver_gpu_ms } | Measure-Object -Average).Average
if ($gpuMs -le 0) { throw 'GPU timestamps must be positive.' }
$lastGpu = $pairs | Sort-Object { [int]$_.step_index } | Select-Object -Last 1
$lastCpu = $lookup[[int]$lastGpu.step_index]
$centreErrorSquared = 0.0
foreach ($axis in @('x','y','z')) {
    $field = "density_centre_$axis"
    $centreErrorSquared += [math]::Pow(([double]$lastCpu.$field - [double]$lastGpu.$field), 2)
}
[pscustomobject]@{
    SharedMeasuredSteps = $measured.Count
    WarmupExcluded = $warmup
    CpuSolverMeanMs = [math]::Round($cpuMs, 4)
    GpuSolverMeanMs = [math]::Round($gpuMs, 4)
    CpuTimeOverGpuTime = [math]::Round($cpuMs / $gpuMs, 2)
    FinalSharedStep = [int]$lastGpu.step_index
    CpuDensityIntegral = [double]$lastCpu.density_integral
    GpuDensityIntegral = [double]$lastGpu.density_integral
    DensityCentreDistance = [math]::Sqrt($centreErrorSquared)
} | Format-List
if (@($pairs | Where-Object { [int]$_.nonfinite_density_cells -gt 0 }).Count -gt 0) {
    Write-Warning 'GPU density contains nonfinite values. Do not treat this as a valid quality comparison.'
}
Write-Output 'Timing ratio compares CPU solver wall time with GPU solver execution time; it is not total application speedup or equal-accuracy speedup.'
Write-Output 'GPU Jacobi and CPU PCG have different projection accuracy. Density statistics do not establish divergence correctness.'
if ($cpu.status -ne 'complete' -or !$gpu.completed) { Write-Warning 'At least one run is partial; only shared steps were compared.' }
