"""Full-field validation of the short offline reference replay."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np
from analyze_pressure_triage import divergence, laplace, project, rms, sha, controls
from offline_reference_worker import read_fields, solve_eigen


def primary_prediction(before, pressure, stored_divergence, dt):
    """Predict from inputs only: never use the observed GPU output to fit rounding."""
    expected = project(before, pressure, dt, arithmetic32=True)
    unrounded = project(before, pressure, dt)
    pre_correction = divergence(before) - stored_divergence
    projection_rounding = divergence([v.astype(float)-u for v,u in zip(expected,unrounded)])
    ideal = stored_divergence - dt*laplace(pressure.astype(float))
    predicted = ideal + pre_correction + projection_rounding
    return expected, predicted, ideal, pre_correction, projection_rounding


def check_projection_identity(expected, after, predicted, stored_before, compact_ratio):
    assert all(np.array_equal(x,y) for x,y in zip(expected,after)), 'projection'
    identity = rms(divergence(after)-predicted)
    assert identity < 2e-5, ('field_identity', identity)
    difference = abs(rms(predicted)/max(rms(stored_before),1e-30)-compact_ratio)
    assert difference < 5e-7, ('ratio_identity_difference', difference)
    return identity, difference


def validate(run, total_steps=12, emitter_steps=6):
    cfg=json.loads((run/'config.json').read_text())
    shared=json.loads((run/'analysis.json').read_text())
    assert shared['harness_valid']
    mode=cfg['offline_pressure_mode']; n=cfg['resolution']; dt=cfg['dt']
    assert mode in (1,2) and cfg['pressure_iterations']==0
    assert (total_steps,emitter_steps) in ((12,6),(180,120),(360,120))
    assert cfg['total_steps']==total_steps and cfg['emitter_steps']==emitter_steps
    assert shared['source_audit_steps_observed']==[1,emitter_steps,emitter_steps+1]
    level='primary' if mode==1 else 'tight'
    assert shared[level+'_pass'], (str(run),level,shared['failed_'+level+'_steps'])
    prov=json.loads((run/'provenance.json').read_text())
    for name,record in prov['artifacts'].items(): assert sha(run/name)==record['sha256'],name
    rows=list(csv.DictReader((run/'steps.csv').open()))
    folders=sorted((run/'offline').glob('step-*'))
    assert len(folders)==total_steps
    measurements=[]
    for step,(folder,row) in enumerate(zip(folders,rows),1):
        request=json.loads((folder/'request.json').read_text())
        assert request==dict(step=step,resolution=n,mode=mode,dt=dt)
        assert (folder/'response.ready').read_text()==str(step)
        before=read_fields(folder,n,'before'); after=read_fields(folder,n,'after')
        for vel in (before,after):
            for v,axis in zip(vel,(2,1,0)): assert np.all(np.take(v,(0,n),axis=axis)==0)
        db=np.fromfile(folder/'div_before.f32',dtype='<f4').reshape(n,n,n).astype(float)
        da=np.fromfile(folder/'div_after.f32',dtype='<f4').reshape(n,n,n).astype(float)
        p=np.fromfile(folder/'pressure_uploaded.f32',dtype='<f4').reshape(n,n,n)
        p64=np.fromfile(folder/'pressure64.f64',dtype='<f8').reshape(n,n,n)
        assert all(np.isfinite(x).all() for x in (db,da,p,p64))
        assert sha(folder/'pressure.f32')==sha(folder/'pressure_uploaded.f32')
        assert np.array_equal(p,p64.astype(np.float32))
        pre=divergence(before); post=divergence(after)
        assert rms(pre-db)<1e-5 and rms(post-da)<1e-5
        assert abs(rms(da)-float(row['rms_divergence_after']))<=max(1e-10/dt,2e-4*rms(da))
        assert float(np.max(abs(da)))==float(row['max_abs_divergence_after'])
        rhs=(db if mode==1 else pre)/dt
        linear_residual=rms(rhs-rhs.mean()-laplace(p64))/max(rms(rhs),1e-30)
        assert linear_residual<=1e-9
        rounding_metrics={}
        if mode==1:
            expected,predicted,ideal,pre_correction,rounding=primary_prediction(before,p,db,dt)
            rounding_metrics=dict(ideal_identity_rms=rms(post-ideal),
                ideal_ratio_identity_difference=abs(rms(ideal)/max(rms(db),1e-30)-float(row['relative_residual'])),
                pre_divergence_rounding_rms=rms(pre_correction),projection_rounding_rms=rms(rounding))
        else:
            expected=project(before,p64,dt,store32=True)
            for key,v in zip('uvw',after):
                assert np.array_equal(v,np.fromfile(folder/f'{key}_response.f32',dtype='<f4').reshape(v.shape))
            unrounded=project(before,p64,dt)
            storage=divergence([v.astype(float)-u for v,u in zip(expected,unrounded)])
            predicted=pre-dt*laplace(p64)+storage
        try:
            identity,ratio_difference=check_projection_identity(expected,after,predicted,db,float(row['relative_residual']))
        except AssertionError as error:
            raise AssertionError(dict(run=str(run),step=step,detail=error.args)) from error
        measurements.append(dict(step=step,linear_residual=linear_residual,
            divergence_ratio=float(row['relative_residual']),scaled_max=float(row['scaled_max_divergence']),
            identity_rms=identity,ratio_identity_difference=ratio_difference,**rounding_metrics))
    # Startup inputs must match the previously captured physical state exactly.
    triage=Path('experiments/appearance-attribution/runs/pressure-triage-v1')/f'n{n:03d}-{cfg["scene"]}-s001'/'pressure-triage'
    startup=None
    if triage.exists():
        for name in ('div_before','u_before','v_before','w_before'):
            assert sha(folders[0]/f'{name}.f32')==sha(triage/f'{name}.f32')
        startup=True
    return dict(validation_protocol='rounding-aware identity v1.1',run=str(run),resolution=n,scene=cfg['scene'],mode=mode,
                valid=True,startup_matches_triage=startup,steps=measurements)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--partial',action='store_true')
    args=parser.parse_args()
    tested=controls()
    # Independently constructed eigen solver manufactured controls.
    rng=np.random.default_rng(817)
    for n in (4,8,32):
        p=rng.normal(size=(n,n,n)); b=laplace(p); solved,mean=solve_eigen(b)
        assert rms(solved-(p-p.mean()))<1e-10 and abs(mean)<1e-10
    runs=[validate(p) for p in sorted(args.root.glob('n*')) if (p/'provenance.json').exists()]
    if not args.partial:
        assert len(runs)==14
        assert {(r['resolution'],r['scene'],r['mode']) for r in runs}=={(n,s,m) for n in (32,64,128) for s in ('A','B') for m in (1,2)}
        for mode in ('pressure32','velocity64'):
            first=args.root/f'n128-A-{mode}'; repeat=args.root/f'n128-A-{mode}-repeat'
            for file in first.rglob('*'):
                if file.suffix in ('.f32','.f64'):
                    assert sha(file)==sha(repeat/file.relative_to(first)),file
    args.output.write_text(json.dumps(dict(protocol='offline reference v1',
        analyzer_sha256=sha(Path(__file__)),complete_matrix=not args.partial,
        manufactured_controls=tested,runs=runs,scope='Short replay only; no G0/G1 pass or timing claim'),indent=2)+'\n')
    print('Validated',len(runs),'runs;',sum(len(r['steps']) for r in runs),'full-field steps.')
    for r in runs: print(r['resolution'],r['scene'],r['mode'],max(s['divergence_ratio'] for s in r['steps']))


if __name__=='__main__': main()
