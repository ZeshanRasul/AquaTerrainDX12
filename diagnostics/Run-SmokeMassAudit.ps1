param(
    [string]$Executable = "$PSScriptRoot/../out/build/x64-Release/bin/Release/AquaTerrainDX12.exe",
    [string]$OutputRoot = "$PSScriptRoot/../experiments/mass-budget-audit/data",
    [ValidateSet('sl', 'maccormack')][string[]]$Modes = @('sl', 'maccormack'),
    [switch]$SkipControl
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$root = [IO.Path]::GetFullPath($OutputRoot)
$repo = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$names = @('AQUA_SMOKE_AUDIT','AQUA_SMOKE_AUDIT_OUTPUT','AQUA_SMOKE_AUDIT_PROBES')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    foreach ($mode in $Modes) {
        $variants = if ($SkipControl) { @('audit') } else { @('audit','control') }
        foreach ($variant in $variants) {
            $out = Join-Path $root "$mode-$variant"
            if (Test-Path -LiteralPath $out) { throw "Output already exists: $out. Choose a fresh OutputRoot." }
            New-Item -ItemType Directory -Path $out | Out-Null
            $env:AQUA_SMOKE_AUDIT = $mode
            $env:AQUA_SMOKE_AUDIT_OUTPUT = $out
            $env:AQUA_SMOKE_AUDIT_PROBES = if ($variant -eq 'audit') { '1' } else { '0' }
            $hashes = @{}
            foreach ($file in @('src/Renderer/Renderer.cpp','src/Renderer/Renderer.h',
                    'src/Renderer/SmokeGpuDiagnostics.cpp','src/Renderer/SmokeMassAudit.cpp',
                    'src/Shaders/3d_smoke_compute.hlsl','src/WinMain.cpp','src/Window.cpp',
                    'diagnostics/Run-SmokeMassAudit.ps1')) {
                $hashes[$file] = (Get-FileHash -LiteralPath (Join-Path $repo $file) -Algorithm SHA256).Hash
            }
            $shaderHash = (Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) 'Shaders/3d_smoke_compute.hlsl') -Algorithm SHA256).Hash
            if ($shaderHash -ne $hashes['src/Shaders/3d_smoke_compute.hlsl']) {
                throw 'Runtime shader is stale. Rebuild before running the audit.'
            }
            @{
                utc_started = [DateTime]::UtcNow.ToString('o')
                git_head = (& git -C $repo rev-parse HEAD)
                source_sha256 = $hashes
                executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
                runtime_shader_sha256 = $shaderHash
            } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'provenance.json')
            Write-Output "Running $mode $variant"
            $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
            if (-not $process.WaitForExit(180000)) {
                $process.Kill()
                throw "Audit timed out: $mode $variant"
            }
            $manifestPath = Join-Path $out 'manifest.json'
            if (-not (Test-Path -LiteralPath $manifestPath)) { throw "No manifest; exit $($process.ExitCode). Check smoke-audit-error.txt alongside executable." }
            $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
            if ($process.ExitCode -ne 0 -or -not $manifest.completed -or $manifest.audit_failed_steps -ne 0) {
                throw "Audit failed: $out (exit $($process.ExitCode), failed steps $($manifest.audit_failed_steps))"
            }
            Write-Output "Passed: $out"
        }
        if (-not $SkipControl) {
            $audit = Import-Csv -LiteralPath (Join-Path $root "$mode-audit/end-fields.csv")
            $control = Import-Csv -LiteralPath (Join-Path $root "$mode-control/end-fields.csv")
            $different = @(Compare-Object $audit $control -Property step,density_hash_fnv1a64,kinetic_energy,rms_divergence_after)
            if ($different.Count -ne 0) { throw "Instrumented and control trajectories differ for $mode." }
            Write-Output "${mode}: all 480 density hashes and velocity diagnostics match the uninstrumented control."
        }
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
