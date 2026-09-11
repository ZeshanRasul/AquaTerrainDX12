param(
    [Parameter(Mandatory=$true)][string]$Python,
    [string]$RunRoot = 'diagnostics/runs/sampler-v2',
    [string]$ReportRoot = 'experiments/sampler-microbenchmark',
    [string]$Exe = 'out/build/x64-Release/SamplerMicrobenchmark.exe'
)
$ErrorActionPreference = 'Stop'
$files = @('CMakeLists.txt','diagnostics/SamplerMicrobenchmark.cpp','diagnostics/sampler_microbenchmark.hlsl','diagnostics/analyze_sampler.py','diagnostics/Run-SamplerMicrobenchmark.ps1',$Exe)
$before = @($files | ForEach-Object { Get-FileHash -LiteralPath $_ -Algorithm SHA256 | Select-Object Path,Hash })
& $Python diagnostics/analyze_sampler.py prepare $RunRoot
if ($LASTEXITCODE -ne 0) { throw 'Query generation failed' }
New-Item -ItemType Directory -Path (Join-Path $RunRoot 'snapshot') | Out-Null
foreach ($file in $files) { Copy-Item -LiteralPath $file -Destination (Join-Path $RunRoot 'snapshot') }
foreach ($n in @(32,33,64)) {
    & $Exe $n (Join-Path $RunRoot "$n/positions.f32") diagnostics/sampler_microbenchmark.hlsl (Join-Path $RunRoot "$n")
    if ($LASTEXITCODE -ne 0) { throw "Sampler failed at N=$n" }
}
foreach ($item in $before) {
    if ((Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash -ne $item.Hash) { throw "Input changed: $($item.Path)" }
}
& $Python diagnostics/analyze_sampler.py analyze $RunRoot --out $ReportRoot
if ($LASTEXITCODE -ne 0) { throw 'Analysis failed' }
$provenance = [ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); run_root = (Resolve-Path $RunRoot).Path; files = $before; artifacts = @(Get-ChildItem -LiteralPath $RunRoot -Recurse -File | Where-Object { $_.Directory.Name -ne 'snapshot' } | ForEach-Object { Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 | Select-Object Path,Hash }) }
$provenance | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $ReportRoot 'provenance.json')
$provenance | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $RunRoot 'provenance.json')
