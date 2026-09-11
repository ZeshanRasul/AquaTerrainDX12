"""Negative checks of perturbation-specific validation, using the small pilot."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import numpy as np
from analyze_plateau import analyze

source=Path(sys.argv[1])
tag='sharp-r32-sl-s0-d1-a2-i512'
def reject(name,mutate):
    with tempfile.TemporaryDirectory(prefix='plateau-validation-') as temp:
        root=Path(temp)/'pilot';shutil.copytree(source,root)
        mutate(root)
        try:
            with contextlib.redirect_stdout(io.StringIO()):analyze(root)
        except (ValueError,FileNotFoundError):print('PASS rejects',name)
        else:raise AssertionError('Accepted '+name)

def manifest(root,**changes):
    path=root/tag/'manifest.json';m=json.loads(path.read_text());m.update(changes);path.write_text(json.dumps(m))

def density(root,run,field,operation):
    path=root/run/(field+'.f32');a=np.fromfile(path,dtype='<f4');operation(a);a.tofile(path)

reject('wrong phase metadata',lambda p:manifest(p,shift_cells_diagonal=.25))
reject('wrong step count',lambda p:manifest(p,advection_steps=1))
reject('wrong time scale',lambda p:manifest(p,dt_scale=2))
reject('wrong initial density',lambda p:density(p,tag,'initial_density',lambda a:a.__setitem__(0,1)))
reject('second step is a no-op',lambda p:shutil.copyfile(p/'sharp-r32-sl-s0-d1-a1-i512/density.f32',p/tag/'density.f32'))
reject('reference not converged',lambda p:density(p,'sharp-r32-sl-s0-d1-a2-i8192','density',lambda a:a.__imul__(1.1)))
reject('missing convergence control',lambda p:(p/'sharp-r32-sl-s0-d1-a2-i4096/manifest.json').unlink())
