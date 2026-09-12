"""Known-control validation for the independent production-advection observer."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path
import numpy as np


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--suite',choices=('base','edge'),default='base'); args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]; out=args.output.resolve()
    exe=repo/'out/build/x64-Release/AdvectionAccounting.exe'
    source_paths=[Path(__file__),repo/'diagnostics/AdvectionAccounting.cpp',repo/'diagnostics/advection_accounting.hlsl',
                  repo/'src/Shaders/3d_smoke_compute.hlsl',repo/'experiments/appearance-attribution/ACCOUNTING_CONTROLS.md']
    if not exe.exists() or source_paths[1].stat().st_mtime>exe.stat().st_mtime: raise RuntimeError('Missing/stale executable')
    out.mkdir(parents=True,exist_ok=False)
    hashes={str(p.relative_to(repo)):sha(p) for p in source_paths}
    for p in source_paths:
        target=out/'sources'/p.relative_to(repo); target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(p.read_bytes())
    manifest=dict(complete=False,suite=args.suite,executable_sha256=sha(exe),source_sha256=hashes,runs=[])
    rows=[]; optimized_outputs={}; overall=True

    def launch(folder,n,label,method,opt,expected=None):
        nonlocal overall
        name=f'{label}-n{n}-m{method}-o{opt}'; result_dir=out/name
        command=[str(exe),str(folder),str(method),str(repo/'diagnostics/advection_accounting.hlsl'),str(result_dir),str(opt)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=180)
        (out/f'{name}.log').write_text(result.stdout+result.stderr)
        if result.returncode: raise RuntimeError(f'{name}: {result.stdout} {result.stderr}')
        output=np.fromfile(result_dir/'density-0.f32',dtype='<f4').astype(np.float64)
        trace=np.fromfile(result_dir/'trace-0.f32',dtype='<f4').reshape(-1,20).astype(np.float64)
        repeated=all((result_dir/f'{stem}-0.f32').read_bytes()==(result_dir/f'{stem}-{i}.f32').read_bytes() for stem in ('density','trace') for i in (1,2))
        finite=bool(np.isfinite(output).all() and np.isfinite(trace).all())
        nodes=trace[:,:8]; delta=np.diff(nodes,axis=1); closure=output-nodes[:,-1]
        scale=np.maximum(1,np.abs(nodes).max(axis=1)); sum_scale=max(1,np.abs(output).sum(),np.abs(nodes[:,-1]).sum())
        cell_error=float(np.max(np.abs(closure)/scale)); budget_error=float(np.abs(closure).sum()/sum_scale)
        passed=repeated and finite and cell_error<=5e-7 and budget_error<=1e-7
        expected_error=None
        if expected is not None:
            expected_error=float(np.max(np.abs(output-np.asarray(expected).reshape(-1))))
            passed &= expected_error<=5e-7
        output_bytes=(result_dir/'density-0.f32').read_bytes()
        key=(n,label,method)
        if opt==0: optimized_outputs[key]=output_bytes
        optimized_equal=None if opt==0 else optimized_outputs[key]==output_bytes
        # Optimization differences are reported; no timing/optimization claim is
        # permitted unless equality holds. Both runs must satisfy closure/controls.
        row=dict(name=name,n=n,method=method,opt=opt,repeated=repeated,finite=finite,
                 observed_final_bitwise_equal=bool(np.array_equal(output,nodes[:,-1])),
                 max_scaled_cell_closure=cell_error,normalized_absolute_closure=budget_error,
                 signed_closure_mass=float(closure.sum()/n**3),stage_mass_deltas=(delta.sum(axis=0)/n**3).tolist(),
                 initial_mass=float(nodes[:,0].sum()/n**3),final_mass=float(output.sum()/n**3),
                 solid_cells=int(trace[:,11].sum()),top_departure_cells=int(trace[:,12].sum()),
                 outside_domain_cells=int(trace[:,13].sum()),outside_centres_cells=int(trace[:,14].sum()),
                 negative_before_floor_cells=int(trace[:,15].sum()),boundary_stencil_cells=int(trace[:,16].sum()),
                 limiter_raise_cells=int(trace[:,17].sum()),limiter_lower_cells=int(trace[:,18].sum()),
                 expected_max_error=expected_error,optimized_output_bitwise_equal=optimized_equal,passed=bool(passed))
        if label=='top' and row['top_departure_cells']!=n*n*(n//16): row['passed']=False
        if label in ('periodic','periodic_y') and method==0 and abs(row['final_mass']-row['initial_mass'])>1e-7*abs(row['initial_mass']): row['passed']=False
        if label.startswith('periodic') and row['top_departure_cells']!=0: row['passed']=False
        if label=='floor' and abs(row['stage_mass_deltas'][6]-.25)>1e-7: row['passed']=False
        if label=='clamp_upper' and abs(row['stage_mass_deltas'][4]+2)>1e-7: row['passed']=False
        if label=='clamp_lower' and abs(row['stage_mass_deltas'][4]-2)>1e-7: row['passed']=False
        overall &= row['passed']; rows.append(row)
        files={str(p.relative_to(out)):sha(p) for p in result_dir.iterdir() if p.is_file()}
        files.update({str(p.relative_to(out)):sha(p) for p in folder.iterdir() if p.is_file()})
        manifest['runs'].append(dict(name=name,command=command,files=files))
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
        print(name,'PASS' if row['passed'] else 'FAIL',flush=True)
        return output.astype('<f4').reshape(n,n,n)

    for n in (32,64):
        z,y,x=np.mgrid[:n,:n,:n]; radius2=sum(((a+.5)/n-.5)**2 for a in (x,y,z))
        gaussian=np.exp(-radius2/(2*.15**2)).astype('<f4')
        for opt in (0,1):
            labels=('identity','periodic','damping','floor','top','solid','clamp_upper','clamp_lower') if args.suite=='base' else ('periodic_y','damping','floor','top','solid')
            for label in labels:
                folder=out/f'input-{label}-n{n}-o{opt}';folder.mkdir()
                words=np.zeros(44,dtype='<u4'); floats=words.view('<f4');words[:3]=n;floats[3]=1
                floats[12:15]=1/n;floats[16:19]=1/n;floats[19]=1
                density=np.ones((n,n,n),dtype='<f4');zero=np.zeros_like(density);hat=density.copy();bar=density.copy()
                u=np.zeros((n,n,n+1),dtype='<f4');v=np.zeros((n,n+1,n),dtype='<f4');w=np.zeros((n+1,n,n),dtype='<f4')
                expected=density.copy(); method=0
                if label=='periodic':density=gaussian.copy();words[37]=1;u.fill(.5/n);expected=None
                if label=='periodic_y':density=gaussian.copy();words[37]=1;v.fill(-.5/n);expected=None
                if label=='damping':floats[21]=.2;expected.fill(np.exp(-np.float32(.2)))
                if label=='floor':density.fill(-.25);expected.fill(0)
                if label=='top':v.fill(-.0625);words[27]=1;expected[:,n-n//16:,:]=0
                if label=='solid':floats[28:31]=.5;floats[31]=.2;words[32]=1;expected[radius2<=.2**2]=0
                if label.startswith('clamp_'):method=2;bar.fill(-3 if label=='clamp_upper' else 5)
                words.tofile(folder/'constants.bin')
                for name,a in dict(density=density,temperature=zero,u=u,v=v,w=w,zero=zero,hat=hat,bar=bar).items():a.tofile(folder/f'{name}.f32')
                launch(folder,n,label,method,opt,expected)
                if args.suite=='edge' and label in ('damping','floor','top','solid'):
                    combine=out/f'input-{label}-final-n{n}-o{opt}';combine.mkdir()
                    for p in folder.iterdir(): (combine/p.name).write_bytes(p.read_bytes())
                    density.tofile(combine/'hat.f32');density.tofile(combine/'bar.f32')
                    launch(combine,n,label+'_final',2,opt,expected)
                if label=='top':launch(folder,n,'top_raw',1,opt,np.ones_like(density))
                if label in ('identity','periodic','periodic_y'):
                    forward=launch(folder,n,label+'_forward',1,opt,density if label=='identity' else None)
                    reverse_folder=out/f'input-{label}-reverse-n{n}-o{opt}';reverse_folder.mkdir()
                    for p in folder.iterdir(): (reverse_folder/p.name).write_bytes(p.read_bytes())
                    forward.tofile(reverse_folder/'density.f32');reverse_words=words.copy();reverse_words.view('<f4')[3]=-1;reverse_words.tofile(reverse_folder/'constants.bin')
                    reverse=launch(reverse_folder,n,label+'_reverse',1,opt,density if label=='identity' else None)
                    combine=out/f'input-{label}-combine-n{n}-o{opt}';combine.mkdir()
                    for p in folder.iterdir(): (combine/p.name).write_bytes(p.read_bytes())
                    forward.tofile(combine/'hat.f32');reverse.tofile(combine/'bar.f32')
                    launch(combine,n,label+'_combine',2,opt,density if label=='identity' else None)
    if hashes!={str(p.relative_to(repo)):sha(p) for p in source_paths} or sha(exe)!=manifest['executable_sha256']:raise RuntimeError('Source/build changed')
    manifest['complete']=True;(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    summary=dict(passed=bool(overall),runs=len(rows),rows=rows,
                 max_scaled_cell_closure=max(r['max_scaled_cell_closure'] for r in rows),
                 max_normalized_absolute_closure=max(r['normalized_absolute_closure'] for r in rows),
                 optimization_differences=[r['name'] for r in rows if r['optimized_output_bitwise_equal'] is False],
                 scope='Accounting controls only; historical loss and coupled G1 untested')
    (out/'analysis.json').write_text(json.dumps(summary,indent=2))
    print(json.dumps({k:v for k,v in summary.items() if k!='rows'},indent=2))


if __name__=='__main__':main()
