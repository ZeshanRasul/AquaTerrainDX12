"""Finite full-duration offline numerical controls. Never scores G1."""
import argparse
import json
import subprocess
import shutil
import sys
from pathlib import Path
from analyze_offline_reference import validate
from analyze_pressure_triage import sha


def compare_prefix(old,new,steps):
    for i in range(1,steps+1):
        source=old/'offline'/f'step-{i:04d}'
        for p in source.iterdir():
            if p.suffix in ('.f32','.f64'):
                assert sha(p)==sha(new/'offline'/source.name/p.name),str(p)
    for p in (old/'snapshots').glob('*.f32'):
        assert sha(p)==sha(new/'snapshots'/p.name),str(p)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,required=True)
    args=parser.parse_args();root=args.root.resolve();root.mkdir(parents=True,exist_ok=False)
    repo=Path(__file__).resolve().parents[1]
    exe=repo/'out/build/x64-Release/bin/Release/AquaTerrainDX12.exe'
    paths=list((repo/'src/Renderer').glob('*.cpp'))+list((repo/'src/Renderer').glob('*.h'))
    paths+=list((repo/'src/Shaders').glob('*.hlsl'))
    paths += [repo/p for p in ('RESEARCH_PLAN.md','CMakeLists.txt',
        'diagnostics/run_coupled_appearance.py','diagnostics/analyze_coupled_appearance.py',
        'diagnostics/offline_reference_worker.py','diagnostics/analyze_offline_reference.py',
        'diagnostics/analyze_pressure_triage.py','diagnostics/appearance_source.py',
        'diagnostics/run_full_reference_controls.py',
        'diagnostics/Run-FullReferenceControls.ps1',
        'experiments/appearance-attribution/OFFLINE_REFERENCE_SPEC.md',
        'experiments/appearance-attribution/MARKER_ACCESS_SPEC.md',
        'experiments/appearance-attribution/ROUNDING_IDENTITY_AMENDMENT.md',
        'experiments/appearance-attribution/FULL_DURATION_REFERENCE_SPEC.md')]
    baseline={str(p):sha(p) for p in paths+[exe]}
    for p in paths:
        target=root/'sources'/p.relative_to(repo);target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(p.read_bytes())
    progress=dict(status='running',environment=baseline,runs=[],scope='Numerical control only; no opacity or G1 scoring')
    work=[(steps,n,s,m,False) for steps in (180,360) for n in (32,64,128)
          for s in ('A','B') for m in ('pressure32','velocity64')]
    work += [(360,128,'A',m,True) for m in ('pressure32','velocity64')]
    try:
        for steps,n,scene,mode,repeat in work:
            assert all(sha(Path(p))==h for p,h in baseline.items()),'Environment changed'
            name=f's{steps}-n{n:03d}-{scene}-{mode}{"-repeat" if repeat else ""}'
            run=root/name
            progress['active_run']=name
            (root/'progress.json').write_text(json.dumps(progress,indent=2)+'\n')
            print('Run',name,flush=True)
            subprocess.run([sys.executable,str(repo/'diagnostics/run_coupled_appearance.py'),
                '--output',str(run),'--scene',scene,'--resolution',str(n),'--iterations','0',
                '--steps',str(steps),'--emitter-steps','120','--offline-pressure',mode,
                '--timeout','7200'],check=True)
            result=validate(run,steps,120)
            if steps==360:
                previous=root/(f's360-n128-A-{mode}' if repeat else f's180-n{n:03d}-{scene}-{mode}')
                compare_prefix(previous,run,360 if repeat else 180)
                result['independent_prefix_steps_matched']=360 if repeat else 180
            assert all(sha(Path(p))==h for p,h in baseline.items()),'Environment changed during run'
            (run/'full-validation.json').write_text(json.dumps(result,indent=2)+'\n')
            progress['runs'].append(dict(run=name,valid=True,validation_sha256=sha(run/'full-validation.json')))
            print('Validated',name,flush=True)
        progress['status']='full_numerical_controls_passed'
    except Exception as error:
        progress['status']='blocked';progress['error']=repr(error)
        progress['failure_details']=list(error.args)
        for name in ('smoke-automation-error.txt','smoke-automation-exit.txt'):
            source=exe.parent/name
            if source.exists(): shutil.copy2(source,root/name)
        raise
    finally:
        (root/'progress.json').write_text(json.dumps(progress,indent=2,default=str)+'\n')


if __name__=='__main__': main()
