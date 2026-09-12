"""Validate read-only transport probes and compare candidate/reference sampling.

The rounded-256 interpolation is a comparison model, not an assertion about the
device's internal sampler implementation. No instrumented timings are reported.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import numpy as np
from analyze_projection import load_run, require


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def interpolate(source, departure, rounded=False):
    p=departure.astype(np.float32)-np.float32(.5);b=np.floor(p).astype(np.int64)
    f=(p-b.astype(np.float32)).astype(np.float64)
    if rounded:f=np.round(f*256)/256
    n=source.shape[0];v=[]
    for i in range(8):
        c=np.clip(b+np.array([i&1,(i>>1)&1,(i>>2)&1]),0,n-1)
        v.append(source[c[...,2],c[...,1],c[...,0]])
    def lerp(a,b,f):return a+(b-a)*f
    return lerp(lerp(lerp(v[0],v[1],f[...,0]),lerp(v[2],v[3],f[...,0]),f[...,1]),
                lerp(lerp(v[4],v[5],f[...,0]),lerp(v[6],v[7],f[...,0]),f[...,1]),f[...,2])


def quantized(departure):
    p=departure.astype(np.float32)-np.float32(.5);b=np.floor(p)
    return b+np.round((p-b)*256)/256


def traces(path,manifest,fields,steps=(1,2)):
    n=manifest['resolution'];previous=fields['initial_density'];out=[]
    for step in steps:
        a=np.fromfile(path/f'trace-step{step}.f32',dtype='<f4')
        require(a.size==n**3*16 and np.isfinite(a).all(),f'{path}: invalid trace size/nonfinite')
        t=a.astype(np.float64).reshape(n,n,n,4,4)
        dep=t[...,0,:3];hw=t[...,0,3];full=t[...,1,3];frac=t[...,2,:3];qsample=t[...,2,3]
        lo=t[...,3,0];hi=t[...,3,1];source=t[...,3,2];actual=t[...,3,3]
        if step==1 or out:
            require(np.array_equal(source,previous),f'{path}: probe input is not actual previous density')
        expected=full if manifest.get('density_sampling_float',False) else hw
        require(np.array_equal(expected,actual),f'{path}: recomputed selected sample does not reproduce actual output')
        position=dep.astype(np.float32)-np.float32(.5)
        require(np.array_equal(frac,position-np.floor(position)),f'{path}: fraction/departure mismatch')
        cpu=interpolate(source,dep);cpuq=interpolate(source,dep,True)
        require(np.max(abs(cpu-full))<5e-7,f'{path}: CPU/manual GPU interpolation disagree')
        require(np.max(abs(cpuq-qsample))<5e-7,f'{path}: rounded interpolation model implementation disagrees')
        # Verify bounds from the exact same clamped source stencil.
        b=np.floor(position).astype(np.int64);low=np.full_like(source,np.inf);high=-low
        for i in range(8):
            c=np.clip(b+np.array([i&1,(i>>1)&1,(i>>2)&1]),0,n-1)
            v=source[c[...,2],c[...,1],c[...,0]];low=np.minimum(low,v);high=np.maximum(high,v)
        require(np.array_equal(lo,low) and np.array_equal(hi,high),f'{path}: incorrect stencil bounds')
        previous=actual;out.append(t)
    require(np.array_equal(previous,fields['density']),f'{path}: final output differs from density readback')
    return out


def analyze(root):
    provenance=json.loads((root/'provenance.json').read_text(encoding='utf-8-sig'))
    records=[];validation=[]
    for case in provenance['cases']:
        jobs=[j for j in provenance['jobs'] if j['case']==case];runs={}
        for j in jobs:
            path=root/j['tag'];m,f,metrics=load_run(path)
            require(m['transport_probe']==bool(j['probe']) and m['timings_include_probes']==bool(j['probe']),'Probe mode mismatch')
            require(m['advection']=='sl' and m['advection_steps']==2 and m['recorded_trials']==3 and m['optimized'],'Unexpected run mode')
            require((m['resolution'],m['shape'],m['shift_cells_diagonal'],m['dt_scale'],m['iterations'])==
                    (case['resolution'],case['shape'],case['shift'],case['dt_scale'],j['iterations']),'Run/config mismatch')
            t=traces(path,m,f) if j['probe'] else None
            runs[j['iterations'],j['probe']]=(path,m,f,metrics,t)
        budget=int(512*(case['resolution']/32)**2)
        require(set(runs)=={(i,p) for i in (-1,budget,4096,8192) for p in (0,1)},'Missing control run')
        for i in (-1,budget,4096,8192):
            a=runs[i,0];b=runs[i,1]
            for key in a[2]:require(np.array_equal(a[2][key],b[2][key]),f'Probe changed {key} at {b[0]}')
            validation.append(dict(run=b[0].name,all_eight_fields_identical_with_probes=True,
                hardware_samples_match_actual_output_both_steps=True,manual_models_match_cpu=True))
        reference=runs[8192,1];candidate=runs[budget,1];exact=runs[-1,1];earlier=runs[4096,1]
        require(reference[3]['residual_relative']<1e-4,'Reference pressure residual too large')
        for step in (0,1):
            c=candidate[4][step];r=reference[4][step];e=exact[4][step];old=earlier[4][step]
            norm=np.sum(abs(r[...,3,3]));initial_norm=np.sum(abs(r[...,3,2]))
            convergence=float(np.sum(abs(old[...,3,3]-r[...,3,3]))/norm)
            independent=float(np.sum(abs(e[...,3,3]-r[...,3,3]))/norm)
            require(convergence<2e-6 and independent<2e-6,'Density reference convergence failed')
            dc=c[...,0,:3];dr=r[...,0,:3];dep_changed=np.any(dc!=dr,axis=-1)
            midpoint_changed=np.any(c[...,1,:3]!=r[...,1,:3],axis=-1)
            hw_changed=c[...,0,3]!=r[...,0,3];qc=quantized(dc);qr=quantized(dr)
            qchanged=np.any(qc!=qr,axis=-1)
            same_source=np.array_equal(c[...,3,2],r[...,3,2])
            common_float=interpolate(r[...,3,2],dc)-interpolate(r[...,3,2],dr)
            common_q=interpolate(r[...,3,2],dc,True)-interpolate(r[...,3,2],dr,True)
            flat=(c[...,3,0]==c[...,3,1])&(r[...,3,0]==r[...,3,1])&(c[...,3,0]==r[...,3,0])
            # Attribution masks below apply to a common source; step 2 may also
            # contain a different input field, which is reported explicitly.
            row=dict(shape=case['shape'],resolution=case['resolution'],shift_cells=case['shift'],dt_scale=case['dt_scale'],step=step+1,
                iterations=budget,cells=case['resolution']**3,input_fields_identical=bool(same_source),
                midpoint_changed_cells=int(midpoint_changed.sum()),departure_changed_cells=int(dep_changed.sum()),
                departure_delta_max_cells=float(np.max(abs(dc-dr))),departure_delta_rms_cells=float(np.sqrt(np.mean((dc-dr)**2))),
                hardware_changed_cells=int(hw_changed.sum()),hardware_l1=float(np.sum(abs(c[...,0,3]-r[...,0,3]))/norm),
                hardware_max_abs=float(np.max(abs(c[...,0,3]-r[...,0,3]))),
                float_sample_delta_l1=float(np.sum(abs(c[...,1,3]-r[...,1,3]))/norm),
                common_source_float_delta_l1=float(np.sum(abs(common_float))/initial_norm),
                common_source_rounded_delta_l1=float(np.sum(abs(common_q))/initial_norm),
                common_source_float_responding_cells=int(np.count_nonzero(abs(common_float)>1e-12)),
                rounded_coordinate_changed_cells=int(qchanged.sum()),
                changed_departure_same_rounded_coordinate_cells=int((dep_changed&~qchanged).sum()),
                changed_departure_constant_stencil_cells=int((dep_changed&flat).sum()),
                changed_departure_equal_hw_cells=int((dep_changed&~hw_changed).sum()),
                float_response_equal_hw_cells=int(((abs(common_float)>1e-12)&~hw_changed).sum()),
                max_hw_vs_rounded_model=float(max(np.max(abs(c[...,0,3]-c[...,2,3])),np.max(abs(r[...,0,3]-r[...,2,3])))),
                max_hw_vs_float_sample=float(max(np.max(abs(c[...,0,3]-c[...,1,3])),np.max(abs(r[...,0,3]-r[...,1,3])))),
                reference_4096_8192_l1=convergence,reference_exact_l1=independent)
            records.append(row)
    (root/'summary.json').write_text(json.dumps(records,indent=2,allow_nan=False)+'\n')
    (root/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
    with (root/'summary.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(records[0]));w.writeheader();w.writerows(records)
    (root/'analysis-provenance.json').write_text(json.dumps({'analyzer_sha256':sha(Path(__file__)),'numpy':np.__version__},indent=2)+'\n')
    print(f'PASS {len(validation)} probe/control pairs; sample/output and input chaining exact; interpolation independently checked')
    for r in records:print(r['shape'],r['resolution'],r['shift_cells'],r['dt_scale'],'step',r['step'],'departure changed',r['departure_changed_cells'],
        'hardware changed',r['hardware_changed_cells'],'hardware L1',f"{r['hardware_l1']:.6g}",'float L1',f"{r['float_sample_delta_l1']:.6g}",
        'rounded max mismatch',f"{r['max_hw_vs_rounded_model']:.3g}")


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args();analyze(args.root)
