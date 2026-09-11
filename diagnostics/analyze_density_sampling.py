"""Validate the density-only sampling ablation; no timing claims."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np
from analyze_projection import load_run, require
from analyze_transport_probe import traces, sha

def analyze(root, out, history=None):
    provenance=json.loads((root/'provenance.json').read_text(encoding='utf-8-sig'))
    rows=[]; checks=[]
    for case in provenance['cases']:
        runs={}; budget=int(512*(case['resolution']/32)**2)
        for job in [j for j in provenance['jobs'] if j['case']==case]:
            path=root/job['tag']; m,f,metrics=load_run(path)
            require(m['transport_probe'] and m['timings_include_probes'],'Missing probes')
            require(m['density_sampling_float']==bool(job['sampling']),'Wrong sampling mode')
            require(m['advection']=='sl' and m['advection_steps']==2 and m['recorded_trials']==3 and m['optimized'],'Unexpected run mode')
            require((m['resolution'],m['shape'],m['shift_cells_diagonal'],m['dt_scale'],m['iterations'])==
                    (case['resolution'],case['shape'],case['shift'],case['dt_scale'],job['iterations']),'Wrong config')
            runs[job['iterations'],job['sampling']]=(m,f,metrics,traces(path,m,f))
            if history is not None and job['sampling']==0:
                prior=history/job['tag'].replace('-f0','-p1')
                for key in f:
                    require((path/(key+'.f32')).read_bytes()==(prior/(key+'.f32')).read_bytes(),f'Hardware regression: {job["tag"]} {key}')
                for step in (1,2):
                    require((path/f'trace-step{step}.f32').read_bytes()==(prior/f'trace-step{step}.f32').read_bytes(),'Hardware trace regression')
        require(set(runs)=={(i,s) for i in (-1,budget,4096,8192) for s in (0,1)},'Missing run')
        for i in (-1,budget,4096,8192):
            hw,fp=runs[i,0],runs[i,1]
            for key in hw[1]:
                if key!='density': require(np.array_equal(hw[1][key],fp[1][key]),f'Sampling changed {key}')
            for step in (0,1):
                for rec in (0,1):
                    require(np.array_equal(hw[3][step][...,rec,:3],fp[3][step][...,rec,:3]),'Sampling changed departure/midpoint')
                require(np.array_equal(hw[3][0][...,0,:3],hw[3][step][...,0,:3]),'Velocity not frozen')
            checks.append(dict(case=case,iterations=i,projection_and_departures_bitwise=True,selected_sample_matches_output=True,history_bitwise=history is not None))
        for sampling in (0,1):
            candidate=runs[budget,sampling];ref=runs[8192,sampling];earlier=runs[4096,sampling];exact=runs[-1,sampling]
            require(ref[2]['residual_relative']<1e-4,'Reference residual too large')
            for step in (0,1):
                d=candidate[3][step][...,3,3];r=ref[3][step][...,3,3]
                norm=np.sum(abs(r)); initial=candidate[1]['initial_density'].sum()
                convergence=float(np.sum(abs(earlier[3][step][...,3,3]-r))/norm)
                independent=float(np.sum(abs(exact[3][step][...,3,3]-r))/norm)
                require(convergence<2e-6 and independent<2e-6,'Reference not converged')
                delta=d-r
                rows.append(dict(shape=case['shape'],resolution=case['resolution'],shift=case['shift'],dt_scale=case['dt_scale'],step=step+1,
                    sampling='float' if sampling else 'hardware',budget=budget,changed_cells=int(np.count_nonzero(delta)),
                    normalized_l1=float(np.sum(abs(delta))/norm),max_abs=float(np.max(abs(delta))),
                    mass_error_relative=float((d.sum()-initial)/initial),density_min=float(d.min()),density_max=float(d.max()),
                    reference_4096_8192_l1=convergence,reference_independent_l1=independent))
    out.mkdir(parents=True,exist_ok=True)
    (out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
    (out/'validation.json').write_text(json.dumps(checks,indent=2)+'\n')
    with (out/'summary.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    (out/'analysis-provenance.json').write_text(json.dumps({'analyzer_sha256':sha(Path(__file__)),
        'trace_analyzer_sha256':sha(Path(__file__).with_name('analyze_transport_probe.py')),
        'projection_analyzer_sha256':sha(Path(__file__).with_name('analyze_projection.py')),'numpy':np.__version__},indent=2)+'\n')
    print(f'PASS {len(checks)} paired configurations; projection/departures unchanged; actual updates and input chaining validated')
    for r in rows: print(r['shape'],r['resolution'],r['shift'],r['dt_scale'],r['step'],r['sampling'],r['changed_cells'],r['normalized_l1'])

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--out',type=Path,required=True);p.add_argument('--history',type=Path)
    a=p.parse_args();analyze(a.root,a.out,a.history)
