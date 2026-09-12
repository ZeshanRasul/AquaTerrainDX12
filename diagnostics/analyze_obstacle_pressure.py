"""Masked Poisson validation and offline stopping-rule screen for baffles."""
import csv
import json
from pathlib import Path
import numpy as np
from analyze_projection import require
from analyze_transport_probe import interpolate

root=Path('diagnostics/runs/obstacle-pressure-v1');out=Path('experiments/obstacle-pressure');out.mkdir(parents=True,exist_ok=True)
prov=json.loads((root/'provenance.json').read_text(encoding='utf-8-sig'));rows=[];policies=[]
for n in [32,64]:
    z,y,x=np.meshgrid(*([np.arange(n,dtype=np.float32)]*3),indexing='ij');qx=(x+.5)/n;qy=(y+.5)/n
    for mode in [14,15,16]:
        width=1/32 if mode==15 else 1/8
        slot=(qy>=.375)&(qy<.375+width) if mode!=14 else np.zeros_like(qy,dtype=bool)
        solid=(qx>=.5)&(qx<.53125)&(qy>=.1875)&(qy<.8125)&~slot;fluid=~solid
        padded=np.pad(solid,1,constant_values=True)
        masks={'u':padded[1:-1,1:-1,:-1]|padded[1:-1,1:-1,1:],
               'v':padded[1:-1,:-1,1:-1]|padded[1:-1,1:,1:-1],
               'w':padded[:-1,1:-1,1:-1]|padded[1:,1:-1,1:-1]}
        states={};metrics={};initial=None;divbefore=None
        for budget in [0,16,64,256,1024,4096,8192,32768,65536]:
            path=(Path('diagnostics/runs/obstacle-pressure-reference') if budget==65536 else root)/f'r{n}-wall{mode}-i{budget}';m=json.loads((path/'manifest.json').read_text())
            require((m['coarse_obstacle_case'],m['resolution'],m['iterations'],m['advection_steps'])==(mode,n,budget,60),'Mode mismatch')
            require(m['recorded_trials']==3 and m['validation_failures']==0,'Runtime failure')
            trials=list(csv.DictReader((path/'trials.csv').open()))
            require(len(trials)==3 and all(int(t['nonfinite'])==0 and int(t['repeat_identical'])==1 for t in trials),'Repeat failure')
            f={}
            for key in ['initial_density','density','div_before','div_after','pressure','u','v','w']:
                shape=(n,n,n+1) if key=='u' else (n,n+1,n) if key=='v' else (n+1,n,n) if key=='w' else (n,n,n)
                v=np.fromfile(path/(key+'.f32'),dtype='<f4');require(v.size==np.prod(shape) and np.isfinite(v).all(),'Invalid fields');f[key]=v.astype(float).reshape(shape)
            if initial is None:initial=f['initial_density'];divbefore=f['div_before']
            require(np.array_equal(f['initial_density'],initial) and np.array_equal(f['div_before'],divbefore),'Initial state changed across budgets')
            require(all(np.all(f[k][mask]==0) for k,mask in masks.items()),'Blocked-face velocity nonzero')
            require(np.all(f['density'][solid]==0) and np.all(initial[solid]==0),'Density inside solids')
            div=n*(np.diff(f['u'],axis=2)+np.diff(f['v'],axis=1)+np.diff(f['w'],axis=0));div[solid]=0
            require(np.max(abs(div-f['div_after']))<2e-5,'CPU/GPU divergence mismatch')
            p=f['pressure'];lap=np.zeros_like(p)
            for axis in [0,1,2]:
                lo=[slice(None)]*3;hi=lo.copy();lo[axis]=slice(None,-1);hi[axis]=slice(1,None);lo=tuple(lo);hi=tuple(hi)
                d=(p[hi]-p[lo])*(n*n)*(fluid[hi]&fluid[lo]);lap[lo]+=d;lap[hi]-=d
            # The shader receives float32 dt even though the manifest stores double.
            dt=float(np.float32(m['dt']));rhs=f['div_before']*m['fluid_density']/dt;res=rhs-lap
            identity=float(np.sqrt(np.mean((div[fluid]-res[fluid]*dt/m['fluid_density'])**2)))
            require(identity<2e-5,'Masked pressure/divergence identity failed')
            previous=None
            for step in [59,60]:
                t=np.fromfile(path/f'trace-step{step}.f32',dtype='<f4').reshape(n,n,n,4,4).astype(float)
                require(np.isfinite(t).all(),'Nonfinite trace');source=t[...,3,2];actual=t[...,3,3]
                require(np.array_equal(actual,np.where(solid,0,t[...,0,3])),'Sample/output mismatch')
                require(np.max(abs(interpolate(source,t[...,0,:3])-t[...,1,3]))<5e-7,'Manual/CPU mismatch')
                if previous is not None:require(np.array_equal(source,previous),'Late source chain broken')
                previous=actual
            require(np.array_equal(previous,f['density']),'Final trace mismatch')
            plane=f['u'][:,:,n//2];forward=float(np.maximum(plane,0).sum()/n**2)
            aperture=slot[:,:,n//2];gapflux=float(np.maximum(plane,0)[aperture].sum()/n**2)
            near=fluid&(abs(qx-.515625)<2/n)&(qy>=.1875)&(qy<.8125)
            metric=dict(resolution=n,mode=mode,iterations=budget,
                relative_residual=float(np.linalg.norm(res[fluid])/np.linalg.norm(rhs[fluid])),
                dt_max_divergence=float(np.max(abs(div[fluid]))*dt),
                dt_near_baffle_max_divergence=float(np.max(abs(div[near]))*dt),
                forward_plane_flux=forward,forward_slot_flux=gapflux,
                downstream_mass_fraction=float(f['density'][qx>=.53125].sum()/initial.sum()),
                remaining_mass_fraction=float(f['density'].sum()/initial.sum()),identity_rms=identity)
            states[budget]=f;metrics[budget]=metric
        ref=states[65536];refmetric=metrics[65536]
        norm=abs(ref['density']).sum();initialnorm=initial.sum()
        refdelta=float(abs(states[32768]['density']-ref['density']).sum()/norm)
        fluxrefdelta=abs(metrics[32768]['forward_plane_flux']-refmetric['forward_plane_flux'])/max(refmetric['forward_plane_flux'],1e-30)
        for budget,metric in metrics.items():
            f=states[budget];metric.update(density_l1=float(abs(f['density']-ref['density']).sum()/norm),
                downstream_mass_error_initial=abs(metric['downstream_mass_fraction']-refmetric['downstream_mass_fraction']),
                forward_flux_relative_error=abs(metric['forward_plane_flux']-refmetric['forward_plane_flux'])/max(refmetric['forward_plane_flux'],1e-30),
                slot_flux_relative_error=abs(metric['forward_slot_flux']-refmetric['forward_slot_flux'])/max(refmetric['forward_slot_flux'],1e-30) if mode!=14 else None,
                reference_32768_65536_density_l1=refdelta,reference_32768_65536_flux_error=fluxrefdelta)
            rows.append(metric)
        # Exploratory engineering acceptance: 1% density L1 AND 5% forward flux,
        # including slot flux where applicable. Not a visual tolerance.
        def acceptable(r):return r['density_l1']<=.01 and r['forward_flux_relative_error']<=.05 and (r['slot_flux_relative_error'] is None or r['slot_flux_relative_error']<=.05)
        oracle=next(b for b,r in metrics.items() if acceptable(r))
        for criterion,threshold in [('relative_residual',.01),('relative_residual',.001),('dt_max_divergence',.001),('dt_max_divergence',.0001),('dt_near_baffle_max_divergence',.001)]:
            chosen=next((r for b,r in metrics.items() if b<65536 and r[criterion]<=threshold),None)
            policies.append(dict(resolution=n,mode=mode,criterion=criterion,threshold=threshold,
                selected_iterations=chosen['iterations'] if chosen else None,passes_acceptance=acceptable(chosen) if chosen else False,
                hindsight_first_tested_acceptable_budget=oracle))
        print('case',n,mode,'reference disagreement',refdelta,fluxrefdelta,'first acceptable',oracle,flush=True)
        del states
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n');(out/'policies.json').write_text(json.dumps(policies,indent=2)+'\n')
for p in policies: print(p)

