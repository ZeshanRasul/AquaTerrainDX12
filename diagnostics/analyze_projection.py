"""Validate independent one-step projection trials and plot sensitivity vs GPU cost.

Requires NumPy. No density result is treated as an exact continuum solution.
"""
import argparse
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path
import numpy as np


def require(condition, message):
    if not condition:
        raise ValueError(message)


def rms(a):
    return float(np.sqrt(np.mean(a*a)))


def load_run(path):
    m = json.loads((path/'manifest.json').read_text())
    n = m['resolution']
    require(m['validation_failures'] == 0, f'{path}: runtime validation failed')
    require(m['recorded_trials'] == m['total_trials'], f'{path}: incomplete trials')
    with (path/'trials.csv').open() as f:
        rows = list(csv.DictReader(f))
    require(len(rows) == m['total_trials'], f'{path}: missing CSV rows')
    for i, row in enumerate(rows, 1):
        require(int(row['trial']) == i, f'{path}: out-of-order trial')
        require(all(math.isfinite(float(v)) for v in row.values()), f'{path}: nonfinite metric')
        require(int(row['nonfinite']) == 0 and int(row['repeat_identical']) == 1, f'{path}: nondeterministic/nonfinite field')
        require(all(float(row[k]) > 0 for k in ('projection_ms','advection_ms','total_ms')), f'{path}: invalid timer')
    fields = {}
    for key in ('initial_density','div_before','pressure','div_after','u','v','w','density'):
        shape = (n,n,n+1) if key == 'u' else (n,n+1,n) if key == 'v' else (n+1,n,n) if key == 'w' else (n,n,n)
        a = np.fromfile(path/(key+'.f32'), dtype='<f4')
        require(a.size == math.prod(shape) and np.isfinite(a).all(), f'{path}: invalid {key} field')
        fields[key] = a.astype(np.float64).reshape(shape)
    h = m['spacing']
    u,v,w = (fields[k] for k in ('u','v','w'))
    wall = max(float(np.max(np.abs(a))) for a in (u[:,:,0],u[:,:,-1],v[:,0,:],v[:,-1,:],w[0,:,:],w[-1,:,:]))
    require(wall == 0, f'{path}: closed-wall leakage {wall}')
    div = np.diff(u,axis=2)/h[0]+np.diff(v,axis=1)/h[1]+np.diff(w,axis=0)/h[2]
    div_disagreement = rms(div-fields['div_after'])
    require(div_disagreement < 1e-5, f'{path}: CPU/GPU divergence mismatch {div_disagreement}')
    p = fields['pressure']
    lap = np.zeros_like(p)
    for axis, spacing in zip((2,1,0),h):
        difference = np.diff(p,axis=axis)/(spacing*spacing)
        lo=[slice(None)]*3;hi=lo.copy();lo[axis]=slice(None,-1);hi[axis]=slice(1,None)
        lap[tuple(lo)]+=difference;lap[tuple(hi)]-=difference
    rhs = fields['div_before']*m['fluid_density']/m['dt']
    residual = rhs-lap
    consistency = rms(div-residual*m['dt']/m['fluid_density'])
    require(consistency < 2e-5, f'{path}: pressure/divergence identity mismatch {consistency}')
    d0 = fields['initial_density'];d=fields['density']
    require(d0.sum()>0 and d.min()>=-1e-6, f'{path}: invalid density')
    measured = rows[m['warmup_trials']:]
    require(len(measured)>0, f'{path}: no measured trials')
    metrics = dict(shape=m['shape'],advection=m['advection'],resolution=n,iterations=m['iterations'],
                   optimized=m['optimized'],path=path.name,measured_trials=len(measured),
                   divergence_rms=rms(div),divergence_max=float(np.max(np.abs(div))),
                   density_weighted_divergence_rms=float(np.sqrt(np.sum(d0*div*div)/d0.sum())),
                   residual_relative=rms(residual)/max(rms(rhs),1e-30),
                   residual_rms=rms(residual),divergence_before_rms=rms(fields['div_before']),
                   mass_error_relative=float((d.sum()-d0.sum())/d0.sum()),density_min=float(d.min()),density_max=float(d.max()),
                   wall_velocity_max=wall,pressure_divergence_identity_rms=consistency,
                   cpu_gpu_divergence_disagreement_rms=div_disagreement)
    for key in ('projection_ms','advection_ms','total_ms'):
        values=[float(row[key]) for row in measured]
        metrics['median_'+key]=float(np.median(values))
        if key=='total_ms':metrics.update(cost_q25_ms=float(np.quantile(values,.25)),cost_q75_ms=float(np.quantile(values,.75)))
    return m,fields,metrics


def plot(points, path, shape, n):
    # Static standalone vector figure; exact-velocity controls/reference solves
    # are validation anchors and excluded from the measured-cost frontier.
    points=[p for p in points if p['iterations']>=0]
    W,H=920,540;left,right,top,bottom=90,190,65,90;pw=W-left-right;ph=H-top-bottom
    xs=[p['median_total_ms'] for p in points];ys=[max(p['l1_to_converged_same_method'],1e-8) for p in points]
    xmin=math.floor(math.log10(min(xs)));xmax=math.ceil(math.log10(max(xs)))
    ymin=min(-4,math.floor(math.log10(min(ys))));ymax=max(-1,math.ceil(math.log10(max(ys))))
    def X(x):return left+(math.log10(x)-xmin)/(xmax-xmin)*pw
    def Y(y):return top+ph-(math.log10(max(y,1e-8))-ymin)/(ymax-ymin)*ph
    out=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI, sans-serif">', '<rect width="100%" height="100%" fill="white"/>',f'<text x="{left}" y="28" font-size="20">Projection sensitivity: {shape}, {n}³</text>',f'<text x="{left}" y="49" font-size="12">Independent one-step trials · clamp MacCormack · optimized shaders · labels: Jacobi iterations</text>']
    for e in range(xmin,xmax+1):
        x=X(10**e);out += [f'<path d="M{x},{top} V{top+ph}" stroke="#ddd"/>',f'<text x="{x}" y="{top+ph+22}" text-anchor="middle" font-size="12">{10**e:g}</text>']
    for e in range(ymin,ymax+1):
        y=Y(10**e);out += [f'<path d="M{left},{y} H{left+pw}" stroke="#ddd"/>',f'<text x="{left-12}" y="{y+4}" text-anchor="end" font-size="12">1e{e}</text>']
    for index,(method,color) in enumerate((('sl','#2563eb'),('maccormack','#dc2626'))):
        series=sorted([p for p in points if p['advection']==method],key=lambda p:p['iterations'])
        coords=' '.join(f"{X(p['median_total_ms'])},{Y(p['l1_to_converged_same_method'])}" for p in series)
        out.append(f'<polyline points="{coords}" stroke="{color}" fill="none" stroke-width="2"/>')
        for p in series:
            x,y=X(p['median_total_ms']),Y(p['l1_to_converged_same_method'])
            out.extend([f'<path d="M{X(p["cost_q25_ms"])},{y} H{X(p["cost_q75_ms"])}" stroke="{color}" stroke-width="2"/>',f'<circle cx="{x}" cy="{y}" r="4" fill="{color}"/>',f'<text x="{x+5}" y="{y+(-7 if index==0 else 15)}" font-size="10" fill="{color}">{p["iterations"]}</text>'])
        out.append(f'<text x="{left+pw+18}" y="{top+25+25*index}" fill="{color}" font-size="15">{method}</text>')
    out.extend([f'<text x="{left+pw/2}" y="{H-43}" text-anchor="middle" font-size="14">Projection + advection GPU time (ms, log scale)</text>',f'<text transform="translate(22,{top+ph/2}) rotate(-90)" text-anchor="middle" font-size="14">Normalized L1 to same method at 8192 iterations</text>',f'<text x="{left}" y="{H-18}" font-size="11">Bars: IQR of launch medians. Zero errors displayed at 1e-8. Projection sensitivity, not total advection accuracy.</text>','</svg>'])
    path.write_text('\n'.join(out),encoding='utf-8')


def analyze(root, partial=False):
    provenance=json.loads((root/'provenance.json').read_text(encoding='utf-8-sig'))
    expected=set(itertools.product(provenance['shapes'],provenance['resolutions'],provenance['modes'],provenance['iterations'],range(1,provenance['repeats']+1)))
    runs={};reports=[]
    for shape,n,method,iterations,rep in sorted(expected):
        path=root/f'{shape}-r{n}-{method}-i{iterations}-rep{rep}'
        m,f,p=load_run(path)
        require((m['shape'],m['resolution'],m['advection'],m['iterations'])==(shape,n,method,iterations),f'{path}: manifest/config mismatch')
        require(m['optimized']==provenance['optimized'],f'{path}: optimization mismatch')
        require(m['total_trials']==provenance['trials'],f'{path}: wrong trial count')
        runs[shape,n,method,iterations,rep]=(m,f,p)
    for shape,n,method,rep in itertools.product(provenance['shapes'],provenance['resolutions'],provenance['modes'],range(1,provenance['repeats']+1)):
        group={i:runs[shape,n,method,i,rep] for i in provenance['iterations']}
        baseline=next(iter(group.values()))[1]['initial_density']
        for m,f,p in group.values():
            require(np.array_equal(f['initial_density'],baseline),'Initial density changed across iteration budgets')
        if partial:
            reports.extend(p for m,f,p in group.values());continue
        require(all(i in group for i in (-1,4096,8192)), 'Need exact, 4096 and 8192 controls')
        ref=group[8192][1];exact=group[-1][1];previous=group[4096][1]
        ref_norm=float(np.abs(ref['density']).sum())
        reference_delta=float(np.abs(ref['density']-previous['density']).sum()/ref_norm)
        reference_exact=float(np.abs(ref['density']-exact['density']).sum()/np.abs(exact['density']).sum())
        require(group[8192][2]['residual_relative']<1e-4,'Reference pressure residual is not converged')
        require(reference_delta<2e-6 and reference_exact<2e-6,f'Reference density not converged: {reference_delta}, exact {reference_exact}')
        require(group[-1][2]['divergence_rms']<1e-4,'Discrete curl control is not divergence-free')
        for m,f,p in group.values():
            if m['iterations']>=0:require(np.array_equal(f['div_before'],ref['div_before']),'Provisional velocity divergence changed')
            p['l1_to_converged_same_method']=float(np.abs(f['density']-ref['density']).sum()/ref_norm)
            p['l2_to_converged_same_method']=float(np.linalg.norm(f['density']-ref['density'])/np.linalg.norm(ref['density']))
            p['l1_to_exact_velocity_same_method']=float(np.abs(f['density']-exact['density']).sum()/np.abs(exact['density']).sum())
            p['velocity_relative_to_exact']=float(np.sqrt(sum(np.sum((f[k]-exact[k])**2) for k in ('u','v','w'))/sum(np.sum(exact[k]**2) for k in ('u','v','w'))))
            p['reference_4096_8192_l1']=reference_delta;p['reference_exact_velocity_l1']=reference_exact
            reports.append(p)
    # Different advection methods and density shapes must see identical velocity.
    for n,i,rep in itertools.product(provenance['resolutions'],provenance['iterations'],range(1,provenance['repeats']+1)):
        states=[f for (s,r,m,it,rp),(_,f,_) in runs.items() if (r,it,rp)==(n,i,rep)]
        for f in states[1:]:
            for key in ('u','v','w','div_before','pressure'):require(np.array_equal(f[key],states[0][key]),f'{key} changed between shapes/methods')
    (root/'summary.json').write_text(json.dumps(reports,indent=2,allow_nan=False)+'\n')
    if reports:
        with (root/'summary.csv').open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(reports[0]));w.writeheader();w.writerows(reports)
    aggregates=[]
    for shape,n,method,i in itertools.product(provenance['shapes'],provenance['resolutions'],provenance['modes'],provenance['iterations']):
        matching=[p for p in reports if (p['shape'],p['resolution'],p['advection'],p['iterations'])==(shape,n,method,i)]
        a=matching[0].copy();a.pop('path');a['launches']=len(matching)
        for key in ('projection_ms','advection_ms','total_ms'):
            values=[p['median_'+key] for p in matching]
            a['median_'+key]=float(np.median(values))
            if key=='total_ms':a.update(cost_q25_ms=float(np.quantile(values,.25)),cost_q75_ms=float(np.quantile(values,.75)))
        for rep in range(2,provenance['repeats']+1):
            require(runs[shape,n,method,i,rep][0]['field_hashes']==runs[shape,n,method,i,1][0]['field_hashes'],'Fields differ across process launches')
        aggregates.append(a)
    (root/'aggregate.json').write_text(json.dumps(aggregates,indent=2,allow_nan=False)+'\n')
    (root/'analysis_provenance.json').write_text(json.dumps({'analyzer_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'numpy':np.__version__,'partial':partial},indent=2)+'\n')
    if not partial:
        figures=root/'figures';figures.mkdir(exist_ok=True)
        for shape,n in itertools.product(provenance['shapes'],provenance['resolutions']):
            points=[p for p in aggregates if p['shape']==shape and p['resolution']==n and p['iterations']<4096 and p['optimized']]
            if points:plot(points,figures/f'{shape}-r{n}-error-vs-cost.svg',shape,n)
    print(f'PASS: {len(runs)} runs; fields, repeats, closed walls, pressure/divergence identity'+('' if partial else ', and convergence controls'))
    for p in aggregates:
        print(p['shape'],p['resolution'],p['advection'],p['iterations'],f"cost={p['median_total_ms']:.6g}",f"residual={p['residual_relative']:.3g}",f"L1={p.get('l1_to_converged_same_method',float('nan')):.6g}")


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);parser.add_argument('--partial',action='store_true')
    args=parser.parse_args();analyze(args.root,args.partial)
