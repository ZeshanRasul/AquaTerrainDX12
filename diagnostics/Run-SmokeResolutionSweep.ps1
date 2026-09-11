<#
Resolution sweep for the passive-advection reference harness (frozen baseline).

Separates numerical correctness from performance:
 - Correctness run per config: PRODUCTION (SKIP_OPTIMIZATION) shaders, full error
   metrics. This is the accuracy of record and matches every other run / the audit.
 - Timing runs per config: OPTIMIZED (OPTIMIZATION_LEVEL3) shaders, warmup + repeats,
   for the isolated advection cost. Their final L1 is also recorded so the analyzer
   can verify that optimization does not materially change the numerics.

Physical problem held fixed across resolutions (see experiments/resolution-sweep):
translation speed scales r/32 (constant physical velocity); rotation is
resolution-independent. Each run is a hidden, env-var-driven instance launched at
its resolution (AQUA_SMOKE_RESOLUTION read before smoke resources are sized).

Build Release before running so the runtime shader matches the source hash.
#>
param(
    [string]$Executable = "$PSScriptRoot/../out/build/x64-Release/bin/Release/AquaTerrainDX12.exe",
    [string]$OutputRoot = "$PSScriptRoot/../diagnostics/runs/resolution-sweep",
    [int[]]$Resolutions = @(32, 64, 128),
    [ValidateSet('translation','rotation')][string[]]$Cases = @('translation','rotation'),
    [ValidateSet('sl','maccormack')][string[]]$Modes = @('sl','maccormack'),
    [int]$TimingRepeats = 3
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$root = [IO.Path]::GetFullPath($OutputRoot)
$repo = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$names = @('AQUA_SMOKE_REFERENCE','AQUA_SMOKE_REFERENCE_ADVECTION','AQUA_SMOKE_REFERENCE_OUTPUT',
    'AQUA_SMOKE_REFERENCE_SPEED','AQUA_SMOKE_RESOLUTION','AQUA_SMOKE_REFERENCE_OPTIMIZE','AQUA_SMOKE_AUDIT')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

$shaderHash = (Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) 'Shaders/3d_smoke_compute.hlsl') -Algorithm SHA256).Hash
$srcHash = (Get-FileHash -LiteralPath (Join-Path $repo 'src/Shaders/3d_smoke_compute.hlsl') -Algorithm SHA256).Hash
if ($shaderHash -ne $srcHash) { throw 'Runtime shader is stale. Rebuild (Release) before running.' }
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$gitHead = (& git -C $repo rev-parse HEAD)

function Invoke-Run($case, $res, $mode, $out, $optimize) {
    if (Test-Path -LiteralPath $out) { throw "Output already exists: $out. Choose a fresh OutputRoot." }
    New-Item -ItemType Directory -Path $out | Out-Null
    $env:AQUA_SMOKE_REFERENCE = $case
    $env:AQUA_SMOKE_REFERENCE_ADVECTION = $mode
    $env:AQUA_SMOKE_REFERENCE_OUTPUT = $out
    $env:AQUA_SMOKE_RESOLUTION = "$res"
    $env:AQUA_SMOKE_REFERENCE_OPTIMIZE = if ($optimize) { '1' } else { $null }
    $env:AQUA_SMOKE_REFERENCE_SPEED = if ($case -eq 'translation') { "$([double](0.375 * $res / 32))" } else { $null }
    @{
        utc_started = [DateTime]::UtcNow.ToString('o'); git_head = $gitHead
        executable_sha256 = $exeHash; runtime_shader_sha256 = $shaderHash
        resolution = $res; optimized = [bool]$optimize
        reference_speed = $env:AQUA_SMOKE_REFERENCE_SPEED
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'provenance.json')
    $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(300000)) { $process.Kill(); throw "Timed out: $out" }
    if ($process.ExitCode -ne 0) { throw "Run failed: $out (exit $($process.ExitCode))." }
    $m = Get-Content -Raw -LiteralPath (Join-Path $out 'manifest.json') | ConvertFrom-Json
    if ($m.recorded_steps -ne $m.total_steps) { throw "Incomplete run: $out" }
}

try {
    $env:AQUA_SMOKE_AUDIT = $null
    foreach ($res in $Resolutions) {
        foreach ($case in $Cases) {
            foreach ($mode in $Modes) {
                Write-Output "== $case r=$res $mode =="
                Invoke-Run $case $res $mode (Join-Path $root "$case-r$res-$mode-correctness") $false
                for ($i = 1; $i -le $TimingRepeats; ++$i) {
                    Invoke-Run $case $res $mode (Join-Path $root "$case-r$res-$mode-timing$i") $true
                }
            }
        }
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
Write-Output "Done. Analyzing..."
& (Join-Path $PSScriptRoot 'Analyze-SmokeResolutionSweep.ps1') -RunRoot $root
