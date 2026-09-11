"""Reject corrupted ablation semantics using the validated pilot."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import numpy as np
from analyze_density_sampling import analyze

source=Path('diagnostics/runs/density-sampling-pilot')
tag='sharp-r32-s0-d1-i512-f1'
results=[]
def reject(name, mutate):
    with tempfile.TemporaryDirectory(dir=source.parent,prefix='density-validation-') as tmp:
        root=Path(tmp)/'pilot';shutil.copytree(source,root)
        mutate(root)
        try:
            with contextlib.redirect_stdout(io.StringIO()): analyze(root,Path(tmp)/'out')
        except (ValueError,FileNotFoundError): results.append({'case':name,'rejected':True})
        else: raise AssertionError('Accepted '+name)
def change(root,step,record,component):
    p=root/tag/f'trace-step{step}.f32';a=np.fromfile(p,dtype='<f4').reshape(-1,4,4)
    a[0,record,component]+=.125;a.tofile(p)
def mode(root):
    p=root/tag/'manifest.json';m=json.loads(p.read_text());m['density_sampling_float']=False;p.write_text(json.dumps(m))
reject('wrong sampling mode',mode)
reject('actual update inconsistent with float sample',lambda p:change(p,1,3,3))
reject('incorrect interpolation',lambda p:change(p,1,1,3))
reject('changed midpoint',lambda p:change(p,1,1,0))
reject('broken second-step source chain',lambda p:change(p,2,3,2))
reject('missing reference',lambda p:(p/'sharp-r32-s0-d1-i8192-f1'/'density.f32').unlink())
Path('experiments/density-sampling/negative-validation.json').write_text(json.dumps(results,indent=2)+'\n')
print(f'PASS {len(results)} negative checks')
