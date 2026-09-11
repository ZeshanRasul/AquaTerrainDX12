"""Negative checks for trace semantics using the completed small pilot."""
import contextlib
import io
from pathlib import Path
import shutil
import sys
import tempfile
import numpy as np
from analyze_transport_probe import analyze

source=Path(sys.argv[1]);tag='sharp-r32-s0-d1-i512-p1'
def rejected(name,mutate):
    with tempfile.TemporaryDirectory(prefix='transport-probe-test-') as tmp:
        root=Path(tmp)/'pilot';shutil.copytree(source,root)
        mutate(root)
        try:
            with contextlib.redirect_stdout(io.StringIO()):analyze(root)
        except (ValueError,FileNotFoundError):print('PASS rejects',name)
        else:raise AssertionError('Accepted invalid probe: '+name)

def change(root,step,record,component):
    p=root/tag/f'trace-step{step}.f32';a=np.fromfile(p,dtype='<f4').reshape(-1,4,4)
    a[0,record,component]+=.125;a.tofile(p)

rejected('departure inconsistent with fraction',lambda p:change(p,1,0,0))
rejected('hardware sample inconsistent with actual output',lambda p:change(p,1,0,3))
rejected('incorrect float interpolation',lambda p:change(p,1,1,3))
rejected('incorrect rounded interpolation',lambda p:change(p,1,2,3))
rejected('incorrect stencil bounds',lambda p:change(p,1,3,0))
rejected('broken second-step input chain',lambda p:change(p,2,3,2))
rejected('missing second trace',lambda p:(p/tag/'trace-step2.f32').unlink())
