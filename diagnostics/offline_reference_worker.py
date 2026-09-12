"""Offline closed-box projection worker. Never used for real-time timing."""
import json
import time
from functools import lru_cache
from pathlib import Path
import numpy as np
from analyze_pressure_triage import solve_box, laplace, divergence, project, rms, sha


@lru_cache(None)
def eigensystem(n):
    a = np.diag(np.full(n, -2.0)) + np.diag(np.ones(n-1), 1) + np.diag(np.ones(n-1), -1)
    a[0,0] = a[-1,-1] = -1
    eigen, vectors = np.linalg.eigh(a*n*n)
    assert np.max(np.abs(a*n*n @ vectors-vectors*eigen)) < 1e-8
    eigen[-1] = 0
    return eigen, vectors


def solve_eigen(rhs):
    eigen, vectors = eigensystem(rhs.shape[0])
    t = rhs-rhs.mean()
    for axis in range(3):
        t = np.moveaxis(np.moveaxis(t, axis, -1) @ vectors, -1, axis)
    denominator = eigen[:,None,None]+eigen[None,:,None]+eigen[None,None,:]
    denominator[-1,-1,-1] = 1
    t /= denominator
    t[-1,-1,-1] = 0
    for axis in range(3):
        t = np.moveaxis(np.moveaxis(t, axis, -1) @ vectors.T, -1, axis)
    return t-t.mean(), float(rhs.mean())


def read_fields(root, n, suffix):
    arrays=[]
    for key,shape in [('u',(n,n,n+1)),('v',(n,n+1,n)),('w',(n+1,n,n))]:
        a=np.fromfile(root/f'{key}_{suffix}.f32',dtype='<f4').reshape(shape)
        assert np.isfinite(a).all()
        arrays.append(a)
    return arrays


def answer(root):
    request=json.loads((root/'request.json').read_text())
    n,dt,mode=request['resolution'],request['dt'],request['mode']
    assert mode in (1,2) and n in (32,64,128)
    velocity=read_fields(root,n,'before')
    div=np.fromfile(root/'div_before.f32',dtype='<f4').reshape(n,n,n).astype(float)
    assert np.isfinite(div).all()
    rhs=(div if mode==1 else divergence(velocity))/dt
    pressure,mean=(solve_box if mode==1 else solve_eigen)(rhs)
    error=rms(laplace(pressure)-(rhs-mean))/max(rms(rhs),1e-30)
    assert error <= 1e-9, error
    pressure.astype('<f4').tofile(root/'pressure.f32')
    pressure.astype('<f8').tofile(root/'pressure64.f64')
    if mode==2:
        for key,v in zip('uvw',project(velocity,pressure,dt,store32=True)):
            v.astype('<f4').tofile(root/f'{key}_response.f32')
    record=dict(**request,mean_rhs=mean,relative_solver_residual=error,
                incompatible_ratio=abs(mean)/max(rms(rhs),1e-30),
                request_hashes={p.name:sha(p) for p in root.glob('*before.f32')},
                pressure_sha256=sha(root/'pressure.f32'))
    (root/'cpu.json').write_text(json.dumps(record,indent=2)+'\n')
    (root/'response.tmp').write_text(str(request['step']))
    (root/'response.tmp').replace(root/'response.ready')


def serve(output, stop):
    seen=set()
    while not stop.is_set():
        for request in sorted((Path(output)/'offline').glob('step-*/request.json')):
            if request in seen: continue
            seen.add(request)
            try:
                answer(request.parent)
            except Exception as error:
                (request.parent/'error.txt').write_text(repr(error))
                return
        stop.wait(.01)
