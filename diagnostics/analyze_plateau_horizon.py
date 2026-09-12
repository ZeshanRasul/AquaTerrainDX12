"""Validate final-two-step traces and repeated frozen-velocity transport horizons."""
import json
import hashlib
from pathlib import Path
import numpy as np
from analyze_projection import load_run,require
from analyze_transport_probe import traces

root=Path('diagnostics/runs/plateau-horizon-v2')
out=Path('experiments/plateau-horizon');out.mkdir(parents=True,exist_ok=True)
rows=[];checks=[]
for horizon in [2,8,32,120]:
    for n in [32,64]:
        budget=512*(n//32)**2; runs={}
        for i in [-1,budget,4096,8192]:
            for sampling in [0,1]:
                tag=f'sharp-r{n}-s0-d1-i{i}-f{sampling}';path=root/f's{horizon}'/tag
                m,f,metrics=load_run(path)
                require(m['advection_steps']==horizon and m['density_sampling_float']==bool(sampling),'Wrong mode')
                require(m['resolution']==n and m['iterations']==i and m['recorded_trials']==3,'Wrong config')
                t=traces(path,m,f,(horizon-1,horizon))
                require(np.array_equal(t[0][...,0,:3],t[1][...,0,:3]),'Departure not frozen')
                if horizon==2:
                    old=Path('diagnostics/runs/density-sampling-v1')/tag
                    for name in list(f)+['trace-step1','trace-step2']:
                        require((path/(name+'.f32')).read_bytes()==(old/(name+'.f32')).read_bytes(),'Two-step regression')
                runs[i,sampling]=(m,f,metrics,t)
        for i in [-1,budget,4096,8192]:
            hw,fp=runs[i,0],runs[i,1]
            for k in hw[1]:
                if k!='density':require(np.array_equal(hw[1][k],fp[1][k]),'Projection/input changed')
            for step in [0,1]:
                for rec in [0,1]: require(np.array_equal(hw[3][step][...,rec,:3],fp[3][step][...,rec,:3]),'Coordinates changed')
            checks.append(dict(horizon=horizon,resolution=n,iterations=i,projection_and_coordinates_identical=True,final_two_updates_valid=True))
        for sampling in [0,1]:
            c=runs[budget,sampling];r=runs[8192,sampling];earlier=runs[4096,sampling];exact=runs[-1,sampling]
            require(r[2]['residual_relative']<1e-4,'Pressure reference residual')
            for index,step in enumerate([horizon-1,horizon]):
                d=c[3][index][...,3,3];ref=r[3][index][...,3,3];norm=abs(ref).sum()
                refine=float(abs(earlier[3][index][...,3,3]-ref).sum()/norm)
                independent=float(abs(exact[3][index][...,3,3]-ref).sum()/norm)
                # Report accumulated reference disagreement, do not silently loosen prior tolerance.
                rows.append(dict(horizon=horizon,step=step,resolution=n,sampling='float' if sampling else 'hardware',
                    l1=float(abs(d-ref).sum()/norm),changed_cells=int(np.count_nonzero(d!=ref)),
                    l1_initial_mass_normalized=float(abs(d-ref).sum()/abs(c[1]['initial_density']).sum()),
                    max_abs=float(abs(d-ref).max()),reference_refinement_l1=refine,independent_reference_l1=independent,
                    initial_mass=float(c[1]['initial_density'].sum()),mass=float(d.sum()),reference_mass=float(ref.sum()),
                    density_min=float(d.min()),density_max=float(d.max())))
        del runs
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
(out/'validation.json').write_text(json.dumps(checks,indent=2)+'\n')
print('PASS',len(checks),'pairs; two-step historical regression passed')
for r in rows:
    if r['step']==r['horizon']:print(r['resolution'],r['step'],r['sampling'],'L1',r['l1'],'changed',r['changed_cells'],'refs',r['reference_refinement_l1'],r['independent_reference_l1'])

