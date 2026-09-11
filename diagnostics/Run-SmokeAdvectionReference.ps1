<#
Passive-advection reference harness runner (cases 1-4).

Runs every (case x method) combination as a hidden, env-var-driven instance,
saves manifest + steps.csv + provenance per run, validates completion, then
analyzes. Cases:
  1 source_only  - zero velocity, source on   (identity + injection check)
  2 static       - zero velocity, blob         (identity transport check)
  3 translation  - periodic +x, whole (1.0) and fractional (0.5) cells/step
  4 rotation     - solid-body rotation, one revolution over 300 steps

Build (Release) before running so the runtime shader matches the source hash.
#>
param(
    [string]$Executable = "$PSScriptRoot/../out/build/x64-Release/bin/Release/AquaTerrainDX12.exe",
    [string]$OutputRoot = "$PSScriptRoot/../diagnostics/runs/reference",
    [ValidateSet('source_only','static','translation','rotation')]
        [string[]]$Cases = @('source_only','static','translation','rotation'),
    [ValidateSet('sl','maccormack')][string[]]$Modes = @('sl','maccormack')
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$root = [IO.Path]::GetFullPath($OutputRoot)
$repo = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$names = @('AQUA_SMOKE_REFERENCE','AQUA_SMOKE_REFERENCE_ADVECTION','AQUA_SMOKE_REFERENCE_OUTPUT','AQUA_SMOKE_REFERENCE_SPEED','AQUA_SMOKE_AUDIT')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

# Build the job list. Each job: env case name, output tag, optional speed override.
$jobs = @()
foreach ($case in $Cases) {
    switch ($case) {
        'translation' {
            $jobs += @{ case = 'translation'; tag = 'translation-whole'; speed = '1.0' }
            $jobs += @{ case = 'translation'; tag = 'translation-frac';  speed = '0.5' }
        }
        default { $jobs += @{ case = $case; tag = $case; speed = $null } }
    }
}

try {
    $env:AQUA_SMOKE_AUDIT = $null
    foreach ($job in $jobs) {
        foreach ($mode in $Modes) {
            $out = Join-Path $root "$($job.tag)-$mode"
            if (Test-Path -LiteralPath $out) { throw "Output already exists: $out. Choose a fresh OutputRoot." }
            New-Item -ItemType Directory -Path $out | Out-Null
            $env:AQUA_SMOKE_REFERENCE = $job.case
            $env:AQUA_SMOKE_REFERENCE_ADVECTION = $mode
            $env:AQUA_SMOKE_REFERENCE_OUTPUT = $out
            $env:AQUA_SMOKE_REFERENCE_SPEED = $job.speed  # $null clears it

            $hashes = @{}
            foreach ($file in @('src/Renderer/Renderer.cpp','src/Renderer/Renderer.h',
                    'src/Renderer/SmokeGpuDiagnostics.cpp','src/Renderer/SmokeAdvectionReference.cpp',
                    'src/Shaders/3d_smoke_compute.hlsl','src/WinMain.cpp','src/Window.cpp',
                    'diagnostics/Run-SmokeAdvectionReference.ps1','diagnostics/Analyze-SmokeAdvectionReference.ps1')) {
                $hashes[$file] = (Get-FileHash -LiteralPath (Join-Path $repo $file) -Algorithm SHA256).Hash
            }
            $shaderHash = (Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) 'Shaders/3d_smoke_compute.hlsl') -Algorithm SHA256).Hash
            if ($shaderHash -ne $hashes['src/Shaders/3d_smoke_compute.hlsl']) {
                throw 'Runtime shader is stale. Rebuild (Release) before running.'
            }
            @{
                utc_started = [DateTime]::UtcNow.ToString('o')
                git_head = (& git -C $repo rev-parse HEAD)
                source_sha256 = $hashes
                executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
                runtime_shader_sha256 = $shaderHash
            } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'provenance.json')

            Write-Output "Running $($job.tag) mode=$mode$(if ($job.speed) { " speed=$($job.speed)" })"
            $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
            if (-not $process.WaitForExit(180000)) { $process.Kill(); throw "Reference run timed out: $($job.tag) $mode" }
            if ($process.ExitCode -ne 0) { throw "Reference run failed: $($job.tag) $mode (exit $($process.ExitCode))." }
            $manifest = Get-Content -Raw -LiteralPath (Join-Path $out 'manifest.json') | ConvertFrom-Json
            if ($manifest.recorded_steps -ne $manifest.total_steps) {
                throw "Incomplete run: $out ($($manifest.recorded_steps)/$($manifest.total_steps) steps)"
            }
            Write-Output "Completed: $out"
        }
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
Write-Output "Done. Analyzing..."
& (Join-Path $PSScriptRoot 'Analyze-SmokeAdvectionReference.ps1') -RunRoot $root
