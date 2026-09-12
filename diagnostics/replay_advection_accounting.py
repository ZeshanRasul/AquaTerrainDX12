"""Replay archived hardware-SL fields with per-step measured accounting."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import numpy as np


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1];out=args.output.resolve()
    exe=repo/'out/build/x64-Release/AdvectionAccounting.exe'
    monitored=[Path(__file__),repo/'diagnostics/AdvectionAccounting.cpp',repo/'diagnostics/advection_accounting.hlsl',
               repo/'src/Shaders/3d_smoke_compute.hlsl',repo/'experiments/appearance-attribution/ACCOUNTING_CONTROLS.md']
    if not exe.exists() or monitored[1].stat().st_mtime>exe.stat().st_mtime:raise RuntimeError('Missing/stale executable')
    out.mkdir(parents=True,exist_ok=False)
    hashes={str(p.relative_to(repo)):sha(p) for p in monitored}
    for p in monitored:
        dest=out/'sources'/p.relative_to(repo);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(p.read_bytes())
    manifest=dict(complete=False,source_sha256=hashes,executable_sha256=sha(exe),archives={},runs=[])
    rows=[];summaries=[]
    for n in (32,64):
        old=repo/f'diagnostics/runs/plateau-horizon-v2/s120/sharp-r{n}-s0-d1-i8192-f0'
        old2=repo/f'diagnostics/runs/plateau-horizon-v2/s2/sharp-r{n}-s0-d1-i8192-f0'
        meta=json.loads((old/'manifest.json').read_text())
        if meta['validation_failures'] or meta['density_sampling_float'] or meta['advection']!='sl' or meta['advection_steps']!=120:raise RuntimeError('Wrong historical case')
        archive={str(p.relative_to(repo)):sha(p) for p in old.iterdir() if p.is_file()}
        archive[str((old2/'density.f32').relative_to(repo))]=sha(old2/'density.f32')
        manifest['archives'][str(n)]=archive
        words=np.zeros(44,dtype='<u4');f=words.view('<f4');words[:3]=n;f[3]=meta['dt'];f[12:15]=meta['spacing'];f[16:19]=meta['spacing'];f[19]=meta['fluid_density']
        density=(old/'initial_density.f32').read_bytes();initial=np.frombuffer(density,dtype='<f4').astype(np.float64)
        zero=bytes(n**3*4);velocity={name:(old/f'{name}.f32').read_bytes() for name in ('u','v','w')}
        grid_rows=[]
        for step in range(1,121):
            folder=out/f'n{n}-step{step:03d}';inputs=folder/'inputs';inputs.mkdir(parents=True)
            words.tofile(inputs/'constants.bin')
            for name,data in dict(density=density,temperature=zero,zero=zero,hat=zero,bar=zero,**velocity).items():(inputs/f'{name}.f32').write_bytes(data)
            result_dir=folder/'capture'
            command=[str(exe),str(inputs),'0',str(repo/'diagnostics/advection_accounting.hlsl'),str(result_dir),'1']
            process=subprocess.run(command,capture_output=True,text=True,timeout=180)
            (folder/'run.log').write_text(process.stdout+process.stderr)
            if process.returncode:raise RuntimeError(process.stdout+process.stderr)
            actual_bytes=(result_dir/'density-0.f32').read_bytes()
            actual=np.frombuffer(actual_bytes,dtype='<f4').astype(np.float64)
            trace=np.fromfile(result_dir/'trace-0.f32',dtype='<f4').reshape(-1,20).astype(np.float64)
            if not np.isfinite(trace).all() or not np.isfinite(actual).all():raise RuntimeError('Nonfinite')
            for stem in ('density','trace'):
                baseline=(result_dir/f'{stem}-0.f32').read_bytes()
                for trial in (1,2):
                    if baseline!=(result_dir/f'{stem}-{trial}.f32').read_bytes():raise RuntimeError('Repeat mismatch')
            before=np.frombuffer(density,dtype='<f4').astype(np.float64)
            if not np.array_equal(before,trace[:,0]):raise RuntimeError('Observer input mismatch')
            nodes=trace[:,:8];delta=np.diff(nodes,axis=1);closure=actual-nodes[:,-1]
            cell_error=float(np.max(np.abs(closure)/np.maximum(1,np.abs(nodes).max(axis=1))))
            sum_error=float(np.abs(closure).sum()/max(1,np.abs(actual).sum(),np.abs(nodes[:,-1]).sum()))
            if cell_error>5e-7 or sum_error>1e-7:raise RuntimeError('Budget closure failure')
            historical_checks=[]
            if step==2:
                if actual_bytes!=(old2/'density.f32').read_bytes():raise RuntimeError('Historical step-2 mismatch')
                historical_checks.append('step2 bitwise')
            if step in (119,120):
                old_trace=np.fromfile(old/f'trace-step{step}.f32',dtype='<f4').reshape(-1,4,4).astype(np.float64)
                if not (np.array_equal(old_trace[:,3,2],before) and np.array_equal(old_trace[:,3,3],actual) and np.array_equal(old_trace[:,0,:3],trace[:,8:11])):raise RuntimeError('Historical trace mismatch')
                historical_checks.append('input/output/departure bitwise')
            if step==120:
                if actual_bytes!=(old/'density.f32').read_bytes():raise RuntimeError('Historical final mismatch')
                historical_checks.append('final bitwise')
            boundary=trace[:,16]!=0
            row=dict(n=n,step=step,mass=float(actual.sum()/n**3),retained=float(actual.sum()/initial.sum()),
                     stage_mass_deltas=(delta.sum(axis=0)/n**3).tolist(),max_scaled_cell_closure=cell_error,
                     normalized_absolute_closure=sum_error,solid_cells=int(trace[:,11].sum()),
                     top_departure_cells=int(trace[:,12].sum()),negative_before_floor=int(trace[:,15].sum()),
                     outside_centres=int(trace[:,14].sum()),boundary_stencil_cells=int(boundary.sum()),
                     sampling_delta_boundary_destinations=float(delta[boundary,0].sum()/n**3),
                     sampling_delta_interior_destinations=float(delta[~boundary,0].sum()/n**3),historical_checks=historical_checks)
            rows.append(row);grid_rows.append(row)
            files={str(p.relative_to(out)):sha(p) for p in folder.rglob('*') if p.is_file()}
            manifest['runs'].append(dict(n=n,step=step,files=files,command=command));(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
            density=actual_bytes
            if step%20==0:print(f'N={n} step={step} retained={row["retained"]:.9f}',flush=True)
        summaries.append(dict(n=n,initial_mass=float(initial.sum()/n**3),final_mass=grid_rows[-1]['mass'],retained=grid_rows[-1]['retained'],
                              cumulative_stage_mass_deltas=np.sum([r['stage_mass_deltas'] for r in grid_rows],axis=0).tolist(),
                              top_departure_events=sum(r['top_departure_cells'] for r in grid_rows),
                              negative_before_floor_events=sum(r['negative_before_floor'] for r in grid_rows),
                              solid_events=sum(r['solid_cells'] for r in grid_rows),
                              boundary_destination_sampling_delta=sum(r['sampling_delta_boundary_destinations'] for r in grid_rows),
                              max_scaled_cell_closure=max(r['max_scaled_cell_closure'] for r in grid_rows)))
    if hashes!={str(p.relative_to(repo)):sha(p) for p in monitored} or sha(exe)!=manifest['executable_sha256']:raise RuntimeError('Sources changed')
    for archive in manifest['archives'].values():
        for p,digest in archive.items():
            if sha(repo/p)!=digest:raise RuntimeError('Archive changed')
    manifest['complete']=True;(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    summary=dict(passed=True,rows=rows,summary=summaries,scope='Exact replay of historical frozen-velocity hardware-SL references; not coupled smoke or causal clamp attribution')
    (out/'analysis.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summaries,indent=2))


if __name__=='__main__':main()
