param(
    [string]$Executable="$PSScriptRoot/../out/build/x64-Release/bin/Release/AquaTerrainDX12.exe",
    [string]$OutputRoot="$PSScriptRoot/runs/plateau-horizon-single",
    [ValidateSet(2,8,32,120)][int]$Steps=120
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$exe=(Resolve-Path -LiteralPath $Executable).Path;$root=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $root){throw 'Choose a fresh output root'}
$cases=@(foreach($n in @(32,64)){[pscustomobject]@{resolution=$n;shape='sharp';shift=0.0;dt_scale=1.0}})
$jobs=@(foreach($c in $cases){foreach($i in @(-1,[int](512*($c.resolution/32)*($c.resolution/32)),4096,8192)){foreach($sampling in @(0,1)){
    $phase=$c.shift.ToString('0.##',[Globalization.CultureInfo]::InvariantCulture)
    $dt=$c.dt_scale.ToString('0.##',[Globalization.CultureInfo]::InvariantCulture)
    [pscustomobject]@{tag="$($c.shape)-r$($c.resolution)-s$phase-d$dt-i$i-f$sampling";case=$c;iterations=$i;sampling=$sampling}
}}})
$files=@('src/Renderer/SmokeTransportProbe.cpp','src/Renderer/SmokeProjectionExperiment.cpp','src/Renderer/SmokeAdvectionReference.cpp',
    'src/Renderer/Renderer.h','src/Renderer/Renderer.cpp','src/Renderer/SmokeGpuDiagnostics.cpp','src/Shaders/transport_probe.hlsl',
    'src/Shaders/3d_smoke_compute.hlsl','src/Shaders/projection_experiment.hlsl','src/Shaders/smoke_obstacle.hlsl',
    'diagnostics/Run-SmokePlateauHorizon.ps1','diagnostics/analyze_transport_probe.py','diagnostics/analyze_projection.py','diagnostics/analyze_density_sampling.py')
$hashes=@{};foreach($f in $files){$hashes[$f]=(Get-FileHash -LiteralPath (Join-Path $repo $f)).Hash}
foreach($f in @('transport_probe.hlsl','3d_smoke_compute.hlsl','projection_experiment.hlsl','smoke_obstacle.hlsl')){
    if((Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) "Shaders/$f")).Hash -ne $hashes["src/Shaders/$f"]){throw "Stale runtime shader: $f"}
}
New-Item -ItemType Directory -Path $root|Out-Null
foreach($f in $files){$dest=Join-Path $root "source-snapshot/$f";New-Item -ItemType Directory -Force -Path (Split-Path $dest)|Out-Null;Copy-Item -LiteralPath (Join-Path $repo $f) -Destination $dest}
$exeHash=(Get-FileHash -LiteralPath $exe).Hash
@{experiment='density_sampling_v1';utc_started=[DateTime]::UtcNow.ToString('o');git_head=(& git -C $repo rev-parse HEAD);
    executable_sha256=$exeHash;source_sha256=$hashes;cases=$cases;jobs=$jobs;trials=3;steps=$Steps;timing_claim='none; trace copies inside step interval';
    trace_layout=@('departure.xyz,hardware_sample','midpoint.xyz,float_interpolation','fraction.xyz,rounded_256_interpolation','stencil_min,stencil_max,input_density,actual_output_density')} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'provenance.json')
$names=@('AQUA_SMOKE_REFERENCE','AQUA_SMOKE_REFERENCE_ADVECTION','AQUA_SMOKE_REFERENCE_OUTPUT','AQUA_SMOKE_REFERENCE_OPTIMIZE',
    'AQUA_SMOKE_REFERENCE_SPEED','AQUA_SMOKE_RESOLUTION','AQUA_SMOKE_AUDIT','AQUA_SMOKE_PROJECTION','AQUA_SMOKE_PROJECTION_ITERATIONS',
    'AQUA_SMOKE_PROJECTION_TRIALS','AQUA_SMOKE_PROJECTION_SHIFT','AQUA_SMOKE_PROJECTION_DT_SCALE','AQUA_SMOKE_PROJECTION_STEPS','AQUA_SMOKE_TRANSPORT_PROBE','AQUA_SMOKE_DENSITY_FLOAT')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    $env:AQUA_SMOKE_REFERENCE='static';$env:AQUA_SMOKE_REFERENCE_ADVECTION='sl';$env:AQUA_SMOKE_REFERENCE_OPTIMIZE='1'
    $env:AQUA_SMOKE_REFERENCE_SPEED=$null;$env:AQUA_SMOKE_AUDIT=$null
    $env:AQUA_SMOKE_PROJECTION_TRIALS='3';$env:AQUA_SMOKE_PROJECTION_STEPS="$Steps"
    foreach($j in $jobs){
        Write-Output $j.tag;$c=$j.case;$out=Join-Path $root $j.tag
        $env:AQUA_SMOKE_REFERENCE_OUTPUT=$out;$env:AQUA_SMOKE_RESOLUTION="$($c.resolution)";$env:AQUA_SMOKE_PROJECTION=$c.shape
        $env:AQUA_SMOKE_PROJECTION_ITERATIONS="$($j.iterations)";$env:AQUA_SMOKE_TRANSPORT_PROBE="1";$env:AQUA_SMOKE_DENSITY_FLOAT="$($j.sampling)"
        $env:AQUA_SMOKE_PROJECTION_SHIFT=$c.shift.ToString([Globalization.CultureInfo]::InvariantCulture)
        $env:AQUA_SMOKE_PROJECTION_DT_SCALE=$c.dt_scale.ToString([Globalization.CultureInfo]::InvariantCulture)
        $p=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
        if(-not $p.WaitForExit(300000)){$p.Kill();throw "Timeout $($j.tag)"}
        if($p.ExitCode -ne 0){throw "Failed $($j.tag) exit $($p.ExitCode)"}
        $m=Get-Content -Raw (Join-Path $out 'manifest.json')|ConvertFrom-Json
        if($m.validation_failures -ne 0 -or $m.recorded_trials -ne 3){throw "Validation failure $($j.tag)"}
    }
    if((Get-FileHash -LiteralPath $exe).Hash -ne $exeHash){throw 'Executable changed'}
    foreach($f in $files){if((Get-FileHash -LiteralPath (Join-Path $repo $f)).Hash -ne $hashes[$f]){throw "Source changed $f"}}
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
Write-Output "Complete: $root"


