<#
Resolution sweep for the passive-advection reference harness.

Runs the moving-field cases (translation, rotation) at several cubic resolutions
and both schemes, then analyzes error-vs-cost. Each run is a hidden, env-var-driven
instance launched at its resolution (AQUA_SMOKE_RESOLUTION is read before any smoke
GPU resource is sized). The physical problem is held fixed:
 - domain stays the unit cube (grid spacing 1/r),
 - blob physical size stays 0.12 of the domain (sigma = 0.12*r cells),
 - translation physical velocity is held constant: speed_cells = 0.375 * r/32,
 - rotation angular velocity is resolution-independent (one revolution / 300 steps).
So higher resolution resolves the SAME feature with more cells; a correct
convergence study then shows error falling with resolution (faster for the
higher-order scheme), traded against GPU cost.

Isolated advection cost (no pressure/buoyancy) is timed inside each run and
written per step to steps.csv (advection_ms).

Build (Release) before running so the runtime shader matches the source hash.
#>
param(
    [string]$Executable = "$PSScriptRoot/../out/build/x64-Release/bin/Release/AquaTerrainDX12.exe",
    [string]$OutputRoot = "$PSScriptRoot/../diagnostics/runs/resolution-sweep",
    [int[]]$Resolutions = @(32, 64, 128),
    [ValidateSet('translation','rotation')][string[]]$Cases = @('translation','rotation'),
    [ValidateSet('sl','maccormack')][string[]]$Modes = @('sl','maccormack')
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$root = [IO.Path]::GetFullPath($OutputRoot)
$repo = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$names = @('AQUA_SMOKE_REFERENCE','AQUA_SMOKE_REFERENCE_ADVECTION','AQUA_SMOKE_REFERENCE_OUTPUT',
    'AQUA_SMOKE_REFERENCE_SPEED','AQUA_SMOKE_RESOLUTION','AQUA_SMOKE_AUDIT')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:AQUA_SMOKE_AUDIT = $null
    foreach ($res in $Resolutions) {
        foreach ($case in $Cases) {
            foreach ($mode in $Modes) {
                $out = Join-Path $root "$case-r$res-$mode"
                if (Test-Path -LiteralPath $out) { throw "Output already exists: $out. Choose a fresh OutputRoot." }
                New-Item -ItemType Directory -Path $out | Out-Null
                $env:AQUA_SMOKE_REFERENCE = $case
                $env:AQUA_SMOKE_REFERENCE_ADVECTION = $mode
                $env:AQUA_SMOKE_REFERENCE_OUTPUT = $out
                $env:AQUA_SMOKE_RESOLUTION = "$res"
                # Hold physical translation velocity constant across resolutions.
                $env:AQUA_SMOKE_REFERENCE_SPEED = if ($case -eq 'translation') { "$([double](0.375 * $res / 32))" } else { $null }

                $shaderHash = (Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) 'Shaders/3d_smoke_compute.hlsl') -Algorithm SHA256).Hash
                $srcHash = (Get-FileHash -LiteralPath (Join-Path $repo 'src/Shaders/3d_smoke_compute.hlsl') -Algorithm SHA256).Hash
                if ($shaderHash -ne $srcHash) { throw 'Runtime shader is stale. Rebuild (Release) before running.' }
                @{
                    utc_started = [DateTime]::UtcNow.ToString('o')
                    git_head = (& git -C $repo rev-parse HEAD)
                    executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
                    runtime_shader_sha256 = $shaderHash
                    resolution = $res
                    reference_speed = $env:AQUA_SMOKE_REFERENCE_SPEED
                } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'provenance.json')

                Write-Output "Running $case r=$res mode=$mode$(if ($env:AQUA_SMOKE_REFERENCE_SPEED) { " speed=$($env:AQUA_SMOKE_REFERENCE_SPEED)" })"
                $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
                if (-not $process.WaitForExit(300000)) { $process.Kill(); throw "Timed out: $case r=$res $mode" }
                if ($process.ExitCode -ne 0) { throw "Run failed: $case r=$res $mode (exit $($process.ExitCode))." }
                $manifest = Get-Content -Raw -LiteralPath (Join-Path $out 'manifest.json') | ConvertFrom-Json
                if ($manifest.recorded_steps -ne $manifest.total_steps) {
                    throw "Incomplete run: $out ($($manifest.recorded_steps)/$($manifest.total_steps) steps)"
                }
                Write-Output "Completed: $out"
            }
        }
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
Write-Output "Done. Analyzing..."
& (Join-Path $PSScriptRoot 'Analyze-SmokeResolutionSweep.ps1') -RunRoot $root
