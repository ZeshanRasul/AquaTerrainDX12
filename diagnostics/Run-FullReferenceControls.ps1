param(
    [string]$Root,
    [string]$Python = "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $Root) {
    $Root = Join-Path $repo ("experiments/appearance-attribution/runs/full-reference-controls-v1_1-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
if (-not (Test-Path -LiteralPath $Python)) { throw "Python not found: $Python. Supply -Python with a NumPy-enabled interpreter." }
Push-Location $repo
try {
    $Root = [System.IO.Path]::GetFullPath($Root)
    if (Test-Path -LiteralPath $Root) { throw "Use a fresh output root: $Root" }
    New-Item -ItemType Directory -Force -Path (Split-Path $Root -Parent) | Out-Null
    Write-Host "26 numerical runs; output: $Root"
    Write-Host 'Keep the source/build unchanged and run no other Aqua smoke experiments until this completes.'
    & $Python -u diagnostics/run_full_reference_controls.py --root $Root 2>&1 | Tee-Object -FilePath "$Root.console.log"
    if ($LASTEXITCODE -ne 0) { throw "Matrix stopped. Preserve $Root and return progress.json plus $Root.console.log." }
    Write-Host "Numerical matrix passed. Return $Root/progress.json and retain all raw data for opacity refinement."
}
finally { Pop-Location }
