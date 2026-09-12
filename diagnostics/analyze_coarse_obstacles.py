"""Validate kinematic wall transport separately from incompressible projection."""
import csv
import json
from collections import deque
from pathlib import Path
import numpy as np
from analyze_transport_probe import interpolate
from analyze_projection import require

root=Path('diagnostics/runs/coarse-obstacles-v1');out=Path('experiments/coarse-obstacles');out.mkdir(parents=True,exist_ok=True)
prov=json.loads((root/'provenance.json').read_text(encoding='utf-8-sig'));rows=[]
for job in prov['jobs']:
    path=root/job['tag'];m=json.loads((path/'manifest.json').read_text());n=job['resolution'];mode=job['mode'];steps=job['steps']
    require((m['resolution'],m['coarse_obstacle_case'],m['obstacle_cfl'],m['advection_steps'])==(n,mode,job['cfl'],steps),'Wrong config')
    require(m['recorded_trials']==3 and m['validation_failures']==0 and m['iterations']==-1 and m['advection']=='sl','Wrong runtime mode')
    trial=list(csv.DictReader((path/'trials.csv').open()))
    require(len(trial)==3 and all(int(t['nonfinite'])==0 and int(t['repeat_identical'])==1 for t in trial),'Repeat failed')
    z,y,x=np.meshgrid(*([np.arange(n,dtype=np.float32)]*3),indexing='ij');qx=(x+.5)/n;qy=(y+.5)/n;qz=(z+.5)/n
    centre=np.full_like(qx,.5 if mode==13 else .5+.5/n)
    if mode==12:centre+=.25*(qy-.5)
    width=(.25 if mode==13 else 1)/n
    solid=(abs(qx-centre)<=width/2) if mode!=10 else np.zeros_like(qx,dtype=bool)
    initial=((qx>=.2)&(qx<.35)&(qy>=.25)&(qy<.75)&(qz>=.25)&(qz<.75)&~solid).astype(float)
    fields={}
    for k in ['initial_density','density','pressure','div_before','div_after','u','v','w']:
        shape=(n,n,n+1) if k=='u' else (n,n+1,n) if k=='v' else (n+1,n,n) if k=='w' else (n,n,n)
        a=np.fromfile(path/(k+'.f32'),dtype='<f4');require(a.size==np.prod(shape) and np.isfinite(a).all(),'Bad field')
        fields[k]=a.astype(float).reshape(shape)
    require(np.array_equal(initial,fields['initial_density']),'Initial density mismatch')
    require(np.all(fields['pressure']==0),'Unexpected pressure solve')
    padded=np.pad(solid,1,constant_values=True)
    blocked_u=padded[1:-1,1:-1,:-1]|padded[1:-1,1:-1,1:]
    blocked_v=padded[1:-1,:-1,1:-1]|padded[1:-1,1:,1:-1]
    blocked_w=padded[:-1,1:-1,1:-1]|padded[1:,1:-1,1:-1]
    require(all(np.all(fields[k][b]==0) for k,b in [('u',blocked_u),('v',blocked_v),('w',blocked_w)]),'Nonzero blocked-face flow')
    speed=np.float32(np.float32(job['cfl'])*np.float32(1/n)/np.float32(1/60))
    require(np.array_equal(fields['u'],np.where(blocked_u,0,speed)) and np.all(fields['v']==0) and np.all(fields['w']==0),'Prescribed velocity/mask mismatch')
    div=n*(np.diff(fields['u'],axis=2)+np.diff(fields['v'],axis=1)+np.diff(fields['w'],axis=0))
    # Production divergence explicitly zeroes solid cells.
    div[solid]=0
    require(np.max(abs(div-fields['div_after']))<1e-4,'Divergence mismatch')
    previous=None
    for step in [steps-1,steps]:
        t=np.fromfile(path/f'trace-step{step}.f32',dtype='<f4').reshape(n,n,n,4,4).astype(float)
        require(np.isfinite(t).all(),'Nonfinite trace')
        source=t[...,3,2];actual=t[...,3,3];expected=np.where(solid,0,t[...,0,3])
        require(np.array_equal(actual,expected),'Actual output not hardware sample with solid clearing')
        require(np.max(abs(interpolate(source,t[...,0,:3])-t[...,1,3]))<5e-7,'CPU/manual sample mismatch')
        if previous is not None:require(np.array_equal(source,previous),'Broken late input chain')
        require(np.all(source[solid]==0) and np.all(actual[solid]==0),'Density stored inside solids')
        previous=actual
    require(np.array_equal(previous,fields['density']),'Final trace mismatch')
    if job['cfl']==0:require(np.array_equal(fields['density'],initial),'Static preservation failed')
    if mode==13:
        control=root/job['tag'].replace('wall13','wall10')
        require((path/'density.f32').read_bytes()==(control/'density.f32').read_bytes(),'Subcell empty mask differs from no-wall control')
    # Extruded walls permit an exact 2D six-neighbor connectivity reduction.
    mask=solid[0];seen=np.zeros_like(mask);queue=deque()
    for j in range(n):
        if not mask[j,0]:seen[j,0]=True;queue.append((j,0))
    while queue:
        j,i=queue.popleft()
        for jj,ii in [(j-1,i),(j+1,i),(j,i-1),(j,i+1)]:
            if 0<=jj<n and 0<=ii<n and not mask[jj,ii] and not seen[jj,ii]:seen[jj,ii]=True;queue.append((jj,ii))
    separated=not seen[:,-1].any();receiver=(qx>centre+width/2)&~solid
    d=fields['density'];mass0=initial.sum();down=float(d[receiver].sum())
    rows.append(dict(tag=job['tag'],resolution=n,mode=mode,cfl=job['cfl'],steps=steps,solid_cells=int(solid.sum()),
        voxel_wall_separates_sides=bool(separated),blocked_face_velocity_max=0,solid_density_max=0,
        downstream_mass=down,downstream_mass_over_initial=down/mass0,remaining_mass_over_initial=float(d.sum()/mass0),
        max_density=float(d.max()),validated=True))
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
with (out/'summary.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
for r in rows:print(r['tag'],'solid',r['solid_cells'],'sealed',r['voxel_wall_separates_sides'],'downstream%',round(100*r['downstream_mass_over_initial'],5),'remaining%',round(100*r['remaining_mass_over_initial'],3))
