"""Validate density-plateau perturbations, including genuine initial-state changes.

No timing claims: a few reset trials check numerical repeatability only.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import numpy as np
from analyze_projection import load_run, require


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plot(records,path):
    # Two panels; each cell reports the measured error at the previously observed
    # plateau budget, not the first plateau found by a new iteration sweep.
    panels=sorted({p['resolution'] for p in records if p['shape']=='sharp' and p['advection']=='sl'})
    width=1180;height=535
    s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" font-family="Segoe UI, sans-serif">',
       '<rect width="100%" height="100%" fill="white"/>',
       '<text x="30" y="30" font-size="21">Does the sharp-density SL plateau survive perturbations?</text>',
       '<text x="30" y="54" font-size="13">Normalized L1 to the matched 8192-iteration reference. ZERO means identical exported float32 arrays.</text>']
    rows=[(dt,steps) for dt in (.5,1.,2.) for steps in (1,2)]
    for panel,n in enumerate(panels):
        xbase=155+panel*570;ybase=123;cellw=91;cellh=47
        s.append(f'<text x="{xbase}" y="86" font-size="17">{n}³ · {int(512*(n/32)**2)} Jacobi iterations</text>')
        for col,shift in enumerate((0,.25,.5,.75)):
            s.append(f'<text x="{xbase+col*cellw+cellw/2}" y="110" font-size="12" text-anchor="middle">shift {shift:g}</text>')
        for row,(dt,steps) in enumerate(rows):
            y=ybase+row*cellh
            s.append(f'<text x="{xbase-10}" y="{y+27}" text-anchor="end" font-size="12">dt ×{dt:g}, step {steps}</text>')
            for col,shift in enumerate((0,.25,.5,.75)):
                found=[p for p in records if (p['shape'],p['advection'],p['resolution'],p['shift_cells'],p['dt_scale'],p['advection_steps'])==('sharp','sl',n,shift,dt,steps)]
                if not found:continue
                p=found[0];e=p['l1_to_reference'];same=p['density_identical_to_reference']
                fill='#d5eee6' if same else '#fff1d2' if e<1e-6 else '#f7c9ab' if e<1e-4 else '#e88e82'
                value='ZERO' if same else f'{e:.2e}'
                x=xbase+col*cellw
                s.extend([f'<rect x="{x}" y="{y}" width="{cellw-3}" height="{cellh-3}" rx="3" fill="{fill}"/>',f'<text x="{x+cellw/2-2}" y="{y+27}" font-size="12" text-anchor="middle">{value}</text>'])
    s.extend(['<text x="30" y="440" font-size="13">Shift is in cells along all three axes. Each timestep has its own pressure and exact-velocity controls.</text>',
              '<text x="30" y="463" font-size="13">Step 2 advects the first result with the same projected velocity; no reinitialization or second projection.</text>',
              '<text x="30" y="486" font-size="13">Green: identical density. Yellow: nonzero L1 below 1e-6. Orange: 1e-6 to 1e-4. Red: at least 1e-4.</text>',
              '<text x="30" y="509" font-size="12">This tests the original sampled plateau budgets; it does not establish a safe pressure stopping rule.</text>','</svg>'])
    path.write_text('\n'.join(s),encoding='utf-8')


def analyze(root):
    provenance=json.loads((root/'provenance.json').read_text(encoding='utf-8-sig'))
    jobs=provenance['jobs'];cases=provenance['cases'];results=[];run_metrics=[];state_hashes={}
    require(len({j['tag'] for j in jobs})==len(jobs),'Duplicate planned runs')
    for c in cases:
        selected=[j for j in jobs if j['case']==c];loaded={}
        for job in selected:
            path=root/job['tag'];m,f,p=load_run(path)
            require((m['shape'],m['resolution'],m['advection'],m['iterations'],m['shift_cells_diagonal'],m['dt_scale'],m['advection_steps'])==
                    (c['shape'],c['resolution'],c['mode'],job['iterations'],c['shift'],c['dt_scale'],c['steps']),f'{path}: configuration mismatch')
            require(m['velocity_evolution']=='project_once_then_hold_fixed','Unexpected velocity evolution')
            require(m['optimized']==provenance['optimized'] and m['total_trials']==provenance['trials'],'Compiler/trial mismatch')
            require(math.isclose(m['dt'],c['dt_scale']/60,rel_tol=1e-14),'Timestep mismatch')
            # Independently verify the prescribed density and the actual shift.
            n=c['resolution'];q=(np.arange(n,dtype=np.float32)+np.float32(.5))/np.float32(n)
            centre=np.array([.43,.61,.5],dtype=np.float32)+np.float32(c['shift'])/np.float32(n)
            dx=q[None,None,:]-centre[0];dy=q[None,:,None]-centre[1];dz=q[:,None,None]-centre[2]
            if c['shape']=='sharp':
                expected=(abs(dx)<np.float32(.12))&(abs(dy)<np.float32(.10))&(abs(dz)<np.float32(.12))
                require(np.array_equal(f['initial_density'],expected),'Shifted box does not match independent formula')
            else:
                expected=np.exp(-(dx*dx+dy*dy+dz*dz)/np.float32(2*.085*.085))
                require(np.max(abs(f['initial_density']-expected))<1e-6,'Shifted Gaussian formula mismatch')
            hashes={key:digest(path/(key+'.f32')) for key in f}
            # Scalar phase and number of advection steps must not affect pressure/velocity.
            key=(c['resolution'],c['dt_scale'],job['iterations'])
            velocity_hash=tuple(hashes[k] for k in ('div_before','pressure','div_after','u','v','w'))
            if key in state_hashes:require(state_hashes[key]==velocity_hash,'Pressure/velocity changed across density/step perturbations')
            else:state_hashes[key]=velocity_hash
            loaded[job['iterations']]=(m,f,p,hashes)
            run_metrics.append(dict(p,shift_cells=c['shift'],dt_scale=c['dt_scale'],advection_steps=c['steps']))
        budget=int(512*(c['resolution']/32)**2)
        require(set(loaded)=={-1,budget,4096,8192},'Missing convergence/control run')
        _,ref,rp,rh=loaded[8192];_,old,_,_=loaded[4096];_,exact,ep,_=loaded[-1];_,candidate,cp,ch=loaded[budget]
        for _,f,_,_ in loaded.values():require(np.array_equal(f['initial_density'],ref['initial_density']),'Initial density changed with pressure budget')
        denom=abs(ref['density']).sum()
        convergence=float(abs(ref['density']-old['density']).sum()/denom)
        reference_exact=float(abs(ref['density']-exact['density']).sum()/abs(exact['density']).sum())
        require(rp['residual_relative']<1e-4 and ep['divergence_rms']<1e-4,'Velocity/reference residual control failed')
        require(convergence<2e-6 and reference_exact<2e-6,'Reference density not converged')
        error=candidate['density']-ref['density']
        velocity_error=float(np.sqrt(sum(np.sum((candidate[k]-exact[k])**2) for k in ('u','v','w'))/sum(np.sum(exact[k]**2) for k in ('u','v','w'))))
        results.append(dict(shape=c['shape'],advection=c['mode'],resolution=c['resolution'],shift_cells=c['shift'],dt_scale=c['dt_scale'],
            advection_steps=c['steps'],iterations=budget,l1_to_reference=float(abs(error).sum()/denom),l2_to_reference=float(np.linalg.norm(error)/np.linalg.norm(ref['density'])),
            density_identical_to_reference=bool(np.array_equal(candidate['density'],ref['density'])),changed_cells=int(np.count_nonzero(error)),max_absolute_error=float(np.max(abs(error))),
            relative_velocity_error=velocity_error,residual_relative=cp['residual_relative'],weighted_divergence_rms=cp['density_weighted_divergence_rms'],
            mass_error_relative=cp['mass_error_relative'],reference_4096_8192_l1=convergence,reference_exact_l1=reference_exact,
            initial_density_sha256=ch['initial_density'],density_sha256=ch['density'],reference_density_sha256=rh['density']))
    for p in results:
        same=lambda x: (x['shape'],x['advection'],x['resolution'],x['shift_cells'],x['dt_scale'])==(p['shape'],p['advection'],p['resolution'],p['shift_cells'],p['dt_scale'])
        if p['advection_steps']==2:
            first=next(x for x in results if same(x) and x['advection_steps']==1)
            require(p['initial_density_sha256']==first['initial_density_sha256'],'Initial density changed between one and two steps')
            require(p['density_sha256']!=first['density_sha256'],'Second step did not transport density')
            p['plateau_broken_by_second_step']=first['density_identical_to_reference'] and not p['density_identical_to_reference']
        else:p['plateau_broken_by_second_step']=False
        base=next(x for x in results if (x['shape'],x['advection'],x['resolution'],x['dt_scale'],x['advection_steps'],x['shift_cells'])==
                  (p['shape'],p['advection'],p['resolution'],p['dt_scale'],p['advection_steps'],0))
        p['initial_field_changed_from_unshifted']=p['initial_density_sha256']!=base['initial_density_sha256']
    (root/'summary.json').write_text(json.dumps(results,indent=2,allow_nan=False)+'\n')
    (root/'run-metrics.json').write_text(json.dumps(run_metrics,indent=2,allow_nan=False)+'\n')
    with (root/'summary.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(results[0]));writer.writeheader();writer.writerows(results)
    (root/'analysis-provenance.json').write_text(json.dumps({'analyzer_sha256':digest(Path(__file__)),'shared_validator_sha256':digest(Path(__file__).with_name('analyze_projection.py')),'numpy':np.__version__},indent=2)+'\n')
    figures=root/'figures';figures.mkdir(exist_ok=True);plot(results,figures/'sharp-sl-plateau-map.svg')
    print(f'PASS: {len(jobs)} runs, {len(results)} perturbation cases, repeated fields and independent controls')
    print('Identical reference density:',sum(p['density_identical_to_reference'] for p in results),'/',len(results))
    print('Broken by second step:',sum(p['plateau_broken_by_second_step'] for p in results))
    for p in results:
        print(p['shape'],p['advection'],p['resolution'],'shift',p['shift_cells'],'dt',p['dt_scale'],'steps',p['advection_steps'],'L1',f"{p['l1_to_reference']:.6g}",'changed',p['changed_cells'])


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args();analyze(args.root)
