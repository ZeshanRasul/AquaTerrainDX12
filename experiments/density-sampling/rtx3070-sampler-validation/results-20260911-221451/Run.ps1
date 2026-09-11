# Run from the extracted sampler-validation package on the second GPU.
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
    $output=Join-Path $PSScriptRoot "results-$stamp"
    if(Test-Path -LiteralPath $output){throw 'Output directory already exists'}
    $expected=Get-Content -Raw (Join-Path $PSScriptRoot 'input-hashes.json') | ConvertFrom-Json
    foreach($item in $expected){
        if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $item.path)).Hash -ne $item.sha256){throw "Changed input: $($item.path)"}
    }
    New-Item -ItemType Directory -Path $output | Out-Null
    Copy-Item -LiteralPath 'queries.json','input-hashes.json','Run.ps1' -Destination $output
    foreach($n in @(32,33,64)) {
        $folder=Join-Path $output "$n"
        & ./SamplerMicrobenchmark.exe $n "inputs/$n/positions.f32" ./sampler_microbenchmark.hlsl $folder
        if($LASTEXITCODE -ne 0){throw "Sampler failed at N=$n. Keep the console error."}
        Copy-Item -LiteralPath "inputs/$n/positions.f32" -Destination $folder
        Write-Output (Get-Content -LiteralPath (Join-Path $folder 'device.txt'))
    }
    Get-ChildItem -LiteralPath $output -Recurse -File | ForEach-Object {
        [pscustomobject]@{path=$_.FullName.Substring($output.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
    } | ConvertTo-Json | Set-Content (Join-Path $output 'result-hashes.json')
    Compress-Archive -LiteralPath $output -DestinationPath "$output.zip"
    Write-Output "Finished. Return this file: $output.zip"
} finally { Pop-Location }
