"""Finite offline integration matrix. Stop at the first invalid run."""
import argparse
import json
import subprocess
import sys
from pathlib import Path
from analyze_offline_reference import validate
from analyze_pressure_triage import sha


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,required=True)
    args=parser.parse_args(); root=args.root.resolve(); root.mkdir(parents=True,exist_ok=True)
    repo=Path(__file__).resolve().parents[1]
    exe=repo/'out/build/x64-Release/bin/Release/AquaTerrainDX12.exe'
    pinned=[exe,Path(__file__),repo/'diagnostics/analyze_offline_reference.py',
            repo/'diagnostics/run_coupled_appearance.py',repo/'diagnostics/offline_reference_worker.py',
            repo/'diagnostics/analyze_pressure_triage.py',repo/'RESEARCH_PLAN.md',
            repo/'experiments/appearance-attribution/OFFLINE_REFERENCE_SPEC.md']
    pinned+=list((repo/'src/Renderer').glob('*.cpp'))+list((repo/'src/Renderer').glob('*.h'))
    baseline={str(p):sha(p) for p in pinned}
    progress=dict(status='running',environment=baseline,runs=[])
    work=[(n,s,m,False) for n in (32,64,128) for s in ('A','B') for m in ('pressure32','velocity64')]
    work += [(128,'A',m,True) for m in ('pressure32','velocity64')]
    try:
        for n,scene,mode,repeat in work:
            assert all(sha(Path(p))==h for p,h in baseline.items()),'Environment changed'
            run=root/f'n{n:03d}-{scene}-{mode}{"-repeat" if repeat else ""}'
            if not run.exists():
                subprocess.run([sys.executable,str(repo/'diagnostics/run_coupled_appearance.py'),
                    '--output',str(run),'--scene',scene,'--resolution',str(n),'--iterations','0',
                    '--steps','12','--emitter-steps','6','--offline-pressure',mode,'--timeout','180'],check=True)
            p=json.loads((run/'provenance.json').read_text())
            assert p['executable_sha256']==sha(exe)
            for name,h in p['source_sha256'].items(): assert sha(repo/name)==h,name
            result=validate(run)
            assert all(sha(Path(p))==h for p,h in baseline.items()),'Environment changed during run'
            progress['runs'].append(dict(run=str(run),valid=result['valid']))
            (root/'progress.json').write_text(json.dumps(progress,indent=2)+'\n')
        subprocess.run([sys.executable,str(repo/'diagnostics/analyze_offline_reference.py'),
            '--root',str(root),'--output',str(root/'results.json')],check=True)
        progress['status']='short_replay_validated'
    except Exception as error:
        progress['status']='blocked'; progress['error']=repr(error)
        raise
    finally:
        (root/'progress.json').write_text(json.dumps(progress,indent=2)+'\n')


if __name__=='__main__': main()
