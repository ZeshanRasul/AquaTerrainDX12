param([int[]]$Resolutions=@(32,64),[int[]]$Budgets=@(0,16,64,256,1024,4096,8192,32768),[string]$OutputRoot="$PSScriptRoot/runs/obstacle-pressure-v1",[string]$Executable="$PSScriptRoot/../out/build/x64-Release/bin/Release/AquaTerrainDX12.exe")
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..");$exe=(Resolve-Path $Executable).Path;$root=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $root){throw 'Choose a fresh output root'}
$files=@('src/Shaders/3d_smoke_compute.hlsl','src/Shaders/coarse_obstacle_test.hlsl','src/Shaders/transport_probe.hlsl','src/Shaders/projection_experiment.hlsl','src/Renderer/SmokeProjectionExperiment.cpp','src/Renderer/SmokeTransportProbe.cpp','src/Renderer/SmokeAdvectionReference.cpp','src/Renderer/Renderer.h','diagnostics/Run-ObstaclePressure.ps1')
$hashes=@{};foreach($f in $files){$hashes[$f]=(Get-FileHash -LiteralPath (Join-Path $repo $f)).Hash}
foreach($f in @('3d_smoke_compute.hlsl','coarse_obstacle_test.hlsl','transport_probe.hlsl')){
    if((Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) "Shaders/$f")).Hash -ne $hashes["src/Shaders/$f"]){throw 'Stale shader'}
}
$jobs=@(foreach($n in $Resolutions){foreach($mode in @(14,15,16)){foreach($budget in $Budgets){
    [pscustomobject]@{tag="r$n-wall$mode-i$budget";resolution=$n;mode=$mode;cfl=0.5;steps=60;iterations=$budget}
}}})
New-Item -ItemType Directory -Path $root | Out-Null
foreach($f in $files){$dest=Join-Path $root "source-snapshot/$f";New-Item -ItemType Directory -Force -Path (Split-Path $dest)|Out-Null;Copy-Item -LiteralPath (Join-Path $repo $f) -Destination $dest}
$exeHash=(Get-FileHash $exe).Hash
@{experiment='obstacle_pressure_v1';jobs=$jobs;source_sha256=$hashes;executable_sha256=$exeHash;trials=3;utc=[DateTime]::UtcNow.ToString('o');velocity='masked discrete curl, project once, freeze projected velocity';timing_claim='none'} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $root 'provenance.json')
$names=@('AQUA_SMOKE_REFERENCE','AQUA_SMOKE_REFERENCE_ADVECTION','AQUA_SMOKE_REFERENCE_OUTPUT','AQUA_SMOKE_REFERENCE_OPTIMIZE','AQUA_SMOKE_REFERENCE_SPEED','AQUA_SMOKE_RESOLUTION','AQUA_SMOKE_AUDIT','AQUA_SMOKE_PROJECTION','AQUA_SMOKE_PROJECTION_ITERATIONS','AQUA_SMOKE_PROJECTION_TRIALS','AQUA_SMOKE_PROJECTION_SHIFT','AQUA_SMOKE_PROJECTION_DT_SCALE','AQUA_SMOKE_PROJECTION_STEPS','AQUA_SMOKE_TRANSPORT_PROBE','AQUA_SMOKE_DENSITY_FLOAT','AQUA_SMOKE_COARSE_OBSTACLE','AQUA_SMOKE_OBSTACLE_CFL')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process');[Environment]::SetEnvironmentVariable($name,$null,'Process')}
try{
    $env:AQUA_SMOKE_REFERENCE='static';$env:AQUA_SMOKE_REFERENCE_ADVECTION='sl';$env:AQUA_SMOKE_REFERENCE_OPTIMIZE='1'
    $env:AQUA_SMOKE_PROJECTION='sharp';$env:AQUA_SMOKE_PROJECTION_ITERATIONS='-1';$env:AQUA_SMOKE_PROJECTION_TRIALS='3';$env:AQUA_SMOKE_TRANSPORT_PROBE='1'
    foreach($j in $jobs){
        Write-Output $j.tag
        $env:AQUA_SMOKE_REFERENCE_OUTPUT=Join-Path $root $j.tag;$env:AQUA_SMOKE_RESOLUTION="$($j.resolution)"
        $env:AQUA_SMOKE_PROJECTION_ITERATIONS="$($j.iterations)";$env:AQUA_SMOKE_COARSE_OBSTACLE="$($j.mode)";$env:AQUA_SMOKE_OBSTACLE_CFL=$j.cfl.ToString([Globalization.CultureInfo]::InvariantCulture);$env:AQUA_SMOKE_PROJECTION_STEPS="$($j.steps)"
        $p=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
        if(-not $p.WaitForExit(300000)){$p.Kill();throw 'Timeout'}
        if($p.ExitCode -ne 0){throw 'Application failed'}
        $m=Get-Content -Raw (Join-Path $env:AQUA_SMOKE_REFERENCE_OUTPUT 'manifest.json')|ConvertFrom-Json
        if($m.validation_failures -ne 0 -or $m.recorded_trials -ne 3){throw 'Runtime validation failed'}
    }
    foreach($f in $files){if((Get-FileHash -LiteralPath (Join-Path $repo $f)).Hash -ne $hashes[$f]){throw "Changed source $f"}}
    if((Get-FileHash $exe).Hash -ne $exeHash){throw 'Changed executable'}
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
Write-Output "Complete: $root"


