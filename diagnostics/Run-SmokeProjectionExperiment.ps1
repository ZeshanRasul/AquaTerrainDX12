param(
    [string]$Executable = "$PSScriptRoot/../out/build/x64-Release/bin/Release/AquaTerrainDX12.exe",
    [string]$OutputRoot = "$PSScriptRoot/runs/projection-v1",
    [int[]]$Resolutions = @(32,64),
    [ValidateSet('smooth','sharp')][string[]]$Shapes = @('smooth','sharp'),
    [ValidateSet('sl','maccormack')][string[]]$Modes = @('sl','maccormack'),
    [int[]]$Iterations = @(-1,0,8,32,128,512,2048,4096,8192),
    [int]$Trials = 24,
    [int]$Repeats = 1,
    [switch]$ProductionShaders
)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $Executable).Path
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$root=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $root){throw "Choose a fresh output directory: $root"}
$sources=@{}
foreach($f in @('src/Shaders/3d_smoke_compute.hlsl','src/Shaders/projection_experiment.hlsl','src/Renderer/SmokeProjectionExperiment.cpp','src/Renderer/SmokeAdvectionReference.cpp','src/Renderer/Renderer.h','src/Renderer/Renderer.cpp','src/Renderer/SmokeGpuDiagnostics.cpp','diagnostics/Run-SmokeProjectionExperiment.ps1','diagnostics/analyze_projection.py')){
    $sources[$f]=(Get-FileHash -LiteralPath (Join-Path $repo $f) -Algorithm SHA256).Hash
}
foreach($f in @('3d_smoke_compute.hlsl','projection_experiment.hlsl','smoke_obstacle.hlsl')){
    if((Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) "Shaders/$f")).Hash -ne (Get-FileHash -LiteralPath (Join-Path $repo "src/Shaders/$f")).Hash){throw "Stale runtime shader: $f"}
}
$gpu=try{@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)}catch{@{status='OS inventory unavailable';reason=$_.Exception.Message}}
$provenance=@{utc_started=[DateTime]::UtcNow.ToString('o');git_head=(& git -C $repo rev-parse HEAD);git_status=@(& git -C $repo status --short);executable_sha256=(Get-FileHash -LiteralPath $exe).Hash;source_sha256=$sources;gpu=$gpu;trials=$Trials;repeats=$Repeats;resolutions=$Resolutions;shapes=$Shapes;modes=$Modes;iterations=$Iterations;optimized=(-not $ProductionShaders);order='seeded shuffle, seed 271828'}
New-Item -ItemType Directory -Path $root | Out-Null
$provenance | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'provenance.json')
$names=@('AQUA_SMOKE_REFERENCE','AQUA_SMOKE_REFERENCE_ADVECTION','AQUA_SMOKE_REFERENCE_OUTPUT','AQUA_SMOKE_REFERENCE_OPTIMIZE','AQUA_SMOKE_REFERENCE_SPEED','AQUA_SMOKE_RESOLUTION','AQUA_SMOKE_AUDIT','AQUA_SMOKE_PROJECTION','AQUA_SMOKE_PROJECTION_ITERATIONS','AQUA_SMOKE_PROJECTION_TRIALS')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$jobs=@(foreach($r in $Resolutions){foreach($s in $Shapes){foreach($m in $Modes){foreach($i in $Iterations){for($rep=1;$rep -le $Repeats;$rep++){[pscustomobject]@{r=$r;s=$s;m=$m;i=$i;rep=$rep}}}}}})
$rng=[Random]::new(271828)
for($j=$jobs.Count-1;$j -gt 0;$j--){$k=$rng.Next($j+1);$tmp=$jobs[$j];$jobs[$j]=$jobs[$k];$jobs[$k]=$tmp}
try{
    $env:AQUA_SMOKE_AUDIT=$null;$env:AQUA_SMOKE_REFERENCE='static';$env:AQUA_SMOKE_REFERENCE_SPEED=$null
    $env:AQUA_SMOKE_REFERENCE_OPTIMIZE=if($ProductionShaders){$null}else{'1'}
    $env:AQUA_SMOKE_PROJECTION_TRIALS="$Trials"
    foreach($job in $jobs){
        $tag="$($job.s)-r$($job.r)-$($job.m)-i$($job.i)-rep$($job.rep)"
        Write-Output $tag
        $out=Join-Path $root $tag
        $env:AQUA_SMOKE_REFERENCE_ADVECTION=$job.m;$env:AQUA_SMOKE_REFERENCE_OUTPUT=$out
        $env:AQUA_SMOKE_RESOLUTION="$($job.r)";$env:AQUA_SMOKE_PROJECTION=$job.s;$env:AQUA_SMOKE_PROJECTION_ITERATIONS="$($job.i)"
        $p=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
        if(-not $p.WaitForExit(300000)){$p.Kill();throw "Timeout: $tag"}
        if($p.ExitCode -ne 0){throw "Process failed ($($p.ExitCode)): $tag"}
        $manifest=Get-Content -Raw -LiteralPath (Join-Path $out 'manifest.json') | ConvertFrom-Json
        if($manifest.recorded_trials -ne $Trials -or $manifest.validation_failures -ne 0){throw "Validation failed: $tag"}
    }
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
Write-Output "Complete: $root"
