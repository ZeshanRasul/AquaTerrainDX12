"""Check late-trace validation rejects corruption after unrecorded earlier steps."""
import json
from pathlib import Path
import shutil
import tempfile
import numpy as np
from analyze_projection import load_run
from analyze_transport_probe import traces
source=Path('diagnostics/runs/plateau-horizon-v2/s120/sharp-r32-s0-d1-i512-f1')
results=[]
for name,step,record,component in [('late_input_chain',120,3,2),('late_actual_output',120,3,3),('late_float_model',119,1,3)]:
    with tempfile.TemporaryDirectory(dir='diagnostics/runs') as tmp:
        path=Path(tmp)/'run';shutil.copytree(source,path)
        f=path/f'trace-step{step}.f32';a=np.fromfile(f,dtype='<f4').reshape(-1,4,4);a[0,record,component]+=.125;a.tofile(f)
        m,fields,_=load_run(path)
        try: traces(path,m,fields,(119,120))
        except ValueError: results.append(dict(case=name,rejected=True))
        else: raise AssertionError('Accepted corruption: '+name)
Path('experiments/plateau-horizon/negative-validation.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS',len(results),'late-trace corruption checks')
