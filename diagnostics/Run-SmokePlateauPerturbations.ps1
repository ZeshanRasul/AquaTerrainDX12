param(
    [string]$Executable="$PSScriptRoot/../out/build/x64-Release/bin/Release/AquaTerrainDX12.exe",
    [string]$OutputRoot="$PSScriptRoot/runs/plateau-v1",
    [ValidateRange(2,100)][int]$Trials=3,
    [switch]$Pilot,
    [switch]$ProductionShaders
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$exe=(Resolve-Path -LiteralPath $Executable).Path
$root=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $root){throw "Choose a fresh output: $root"}
$cases=@(foreach($n in @(32,64)){
    foreach($shape in @('sharp','smooth')){foreach($mode in @('sl','maccormack')){
        foreach($shift in @(0.0,0.25,0.5,0.75)){foreach($dt in @(0.5,1.0,2.0)){foreach($steps in @(1,2)){
            $include=($shape -eq 'sharp' -and $mode -eq 'sl') -or
                ($shape -eq 'sharp' -and $mode -eq 'maccormack' -and ($shift -eq 0 -or ($shift -eq 0.5 -and $dt -eq 1))) -or
                ($shape -eq 'smooth' -and $mode -eq 'sl' -and $shift -in @(0,0.5) -and $dt -eq 1)
            if($Pilot){$include=$n -eq 32 -and $shape -eq 'sharp' -and $mode -eq 'sl' -and $shift -in @(0,0.5) -and $dt -eq 1}
            if($include){[pscustomobject]@{resolution=$n;shape=$shape;mode=$mode;shift=$shift;dt_scale=$dt;steps=$steps}}
        }}}}}
})
$jobs=@(foreach($c in $cases){foreach($iterations in @(-1,[int](512*($c.resolution/32)*($c.resolution/32)),4096,8192)){
    $culture=[Globalization.CultureInfo]::InvariantCulture
    $shift=$c.shift.ToString('0.##',$culture);$dt=$c.dt_scale.ToString('0.##',$culture)
    [pscustomobject]@{tag="$($c.shape)-r$($c.resolution)-$($c.mode)-s$shift-d$dt-a$($c.steps)-i$iterations";case=$c;iterations=$iterations}
}})
$sourceFiles=@('src/Shaders/3d_smoke_compute.hlsl','src/Shaders/projection_experiment.hlsl','src/Shaders/smoke_obstacle.hlsl',
    'src/Renderer/SmokeProjectionExperiment.cpp','src/Renderer/SmokeAdvectionReference.cpp','src/Renderer/Renderer.h',
    'src/Renderer/Renderer.cpp','src/Renderer/SmokeGpuDiagnostics.cpp','diagnostics/Run-SmokePlateauPerturbations.ps1',
    'diagnostics/analyze_projection.py','diagnostics/analyze_plateau.py')
$hashes=@{};foreach($f in $sourceFiles){$hashes[$f]=(Get-FileHash -LiteralPath (Join-Path $repo $f)).Hash}
foreach($f in @('3d_smoke_compute.hlsl','projection_experiment.hlsl','smoke_obstacle.hlsl')){
    if((Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) "Shaders/$f")).Hash -ne $hashes["src/Shaders/$f"]){throw "Stale runtime shader $f"}
}
New-Item -ItemType Directory -Path $root | Out-Null
foreach($f in $sourceFiles){$dest=Join-Path $root "source-snapshot/$f";New-Item -ItemType Directory -Force -Path (Split-Path $dest)|Out-Null;Copy-Item -LiteralPath (Join-Path $repo $f) -Destination $dest}
$exeHash=(Get-FileHash -LiteralPath $exe).Hash
@{schema=1;experiment='plateau_perturbations_v1';utc_started=[DateTime]::UtcNow.ToString('o');git_head=(& git -C $repo rev-parse HEAD);
    source_sha256=$hashes;executable_sha256=$exeHash;trials=$Trials;optimized=(-not $ProductionShaders);cases=$cases;jobs=$jobs;
    order='seeded shuffle 314159';timing_claim='none; numerical sensitivity experiment'} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'provenance.json')
$rng=[Random]::new(314159);for($j=$jobs.Count-1;$j -gt 0;$j--){$k=$rng.Next($j+1);$t=$jobs[$j];$jobs[$j]=$jobs[$k];$jobs[$k]=$t}
$names=@('AQUA_SMOKE_REFERENCE','AQUA_SMOKE_REFERENCE_ADVECTION','AQUA_SMOKE_REFERENCE_OUTPUT','AQUA_SMOKE_REFERENCE_OPTIMIZE',
    'AQUA_SMOKE_REFERENCE_SPEED','AQUA_SMOKE_RESOLUTION','AQUA_SMOKE_AUDIT','AQUA_SMOKE_PROJECTION','AQUA_SMOKE_PROJECTION_ITERATIONS',
    'AQUA_SMOKE_PROJECTION_TRIALS','AQUA_SMOKE_PROJECTION_SHIFT','AQUA_SMOKE_PROJECTION_DT_SCALE','AQUA_SMOKE_PROJECTION_STEPS')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    $env:AQUA_SMOKE_AUDIT=$null;$env:AQUA_SMOKE_REFERENCE='static';$env:AQUA_SMOKE_REFERENCE_SPEED=$null
    $env:AQUA_SMOKE_REFERENCE_OPTIMIZE=if($ProductionShaders){$null}else{'1'}
    $env:AQUA_SMOKE_PROJECTION_TRIALS="$Trials"
    foreach($job in $jobs){
        $c=$job.case;$out=Join-Path $root $job.tag;Write-Output $job.tag
        $env:AQUA_SMOKE_REFERENCE_ADVECTION=$c.mode;$env:AQUA_SMOKE_REFERENCE_OUTPUT=$out
        $env:AQUA_SMOKE_RESOLUTION="$($c.resolution)";$env:AQUA_SMOKE_PROJECTION=$c.shape
        $env:AQUA_SMOKE_PROJECTION_ITERATIONS="$($job.iterations)";$env:AQUA_SMOKE_PROJECTION_STEPS="$($c.steps)"
        $env:AQUA_SMOKE_PROJECTION_SHIFT=$c.shift.ToString([Globalization.CultureInfo]::InvariantCulture)
        $env:AQUA_SMOKE_PROJECTION_DT_SCALE=$c.dt_scale.ToString([Globalization.CultureInfo]::InvariantCulture)
        $p=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
        if(-not $p.WaitForExit(300000)){$p.Kill();throw "Timeout: $($job.tag)"}
        if($p.ExitCode -ne 0){throw "Run failed: $($job.tag) ($($p.ExitCode))"}
        $m=Get-Content -Raw -LiteralPath (Join-Path $out 'manifest.json') | ConvertFrom-Json
        if($m.recorded_trials -ne $Trials -or $m.validation_failures -ne 0){throw "Invalid trial: $($job.tag)"}
    }
    if((Get-FileHash -LiteralPath $exe).Hash -ne $exeHash){throw 'Executable changed during run'}
    foreach($f in $sourceFiles){if((Get-FileHash -LiteralPath (Join-Path $repo $f)).Hash -ne $hashes[$f]){throw "Source changed during run: $f"}}
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
Write-Output "Complete: $root"
