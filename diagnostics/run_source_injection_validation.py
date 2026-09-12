"""Run and validate the prospectively registered normalized-source controls."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import numpy as np
from appearance_source import sphere_rate


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def expected(initial, rate, dt, steps):
    value=initial.copy()
    increment=(rate*np.float32(dt)).astype('<f4')
    for _ in range(steps): value=(value+increment).astype('<f4')
    return value


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1];out=args.output.resolve()
    exe=repo/'out/build/x64-Release/SourceInjectionValidation.exe'
    monitored=[Path(__file__),repo/'diagnostics/SourceInjectionValidation.cpp',repo/'diagnostics/appearance_source_injection.hlsl',
               repo/'diagnostics/appearance_source.py',repo/'experiments/appearance-attribution/SOURCE_INJECTION_CONTROLS.md',
               repo/'experiments/appearance-attribution/SPEC.md']
    # HLSL is compiled from the monitored source at runtime; only the C++ host
    # must predate the executable.
    if not exe.exists() or monitored[1].stat().st_mtime>exe.stat().st_mtime:raise RuntimeError('Missing/stale executable')
    out.mkdir(parents=True,exist_ok=False);snapshot=out/'sources';snapshot.mkdir()
    hashes={str(p.relative_to(repo)):sha(p) for p in monitored}
    for p in monitored:
        target=snapshot/p.relative_to(repo);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(p.read_bytes())
    manifest=dict(protocol='source injection v1.0',complete=False,executable_sha256=sha(exe),source_sha256=hashes,
                  dt_float32=float(np.float32(1/60)),quadrature=16,runs=[])
    rows=[];debug_outputs={};overall=True;dt=np.float32(1/60)
    scenes={'A':[(.5,.15,.5)],'B':[(.35,.15,.5),(.65,.15,.5)]}
    for n in (32,64,128):
        for scene,centres in scenes.items():
            rate=np.zeros((n,n,n),dtype='<f4');source_records=[]
            for centre in centres:
                sphere,record=sphere_rate(n,centre,16);rate=(rate+sphere).astype('<f4');source_records.append(record)
            inputs=out/f'inputs-{scene}-n{n}';inputs.mkdir();rate.tofile(inputs/'rate.f32')
            zero=np.zeros_like(rate);zero.tofile(inputs/'zero.f32')
            cases=[('one',zero,zero,1),('accumulated',zero,zero,120)]
            if n==32 and scene=='A':cases.append(('seeded',np.full_like(rate,.125),np.full_like(rate,.25),1))
            for case,initial_density,initial_temperature,steps in cases:
                initial_density.tofile(inputs/f'{case}-density.f32');initial_temperature.tofile(inputs/f'{case}-temperature.f32')
                expected_density=expected(initial_density,rate,dt,steps);expected_temperature=expected(initial_temperature,rate,dt,steps)
                ideal=len(centres)*.002*float(dt)*steps
                for opt in (0,1):
                    name=f'{scene}-n{n}-{case}-o{opt}';result_dir=out/name
                    command=[str(exe),str(n),str(inputs/'rate.f32'),str(inputs/f'{case}-density.f32'),str(inputs/f'{case}-temperature.f32'),
                             str(float(dt)),str(steps),str(repo/'diagnostics/appearance_source_injection.hlsl'),str(result_dir),str(opt)]
                    process=subprocess.run(command,capture_output=True,text=True,timeout=180)
                    (out/f'{name}.log').write_text(process.stdout+process.stderr)
                    if process.returncode:raise RuntimeError(f'{name}: {process.stdout} {process.stderr}')
                    density=np.fromfile(result_dir/'density-0.f32',dtype='<f4').reshape(rate.shape)
                    temperature=np.fromfile(result_dir/'temperature-0.f32',dtype='<f4').reshape(rate.shape)
                    repeats=all((result_dir/f'{field}-0.f32').read_bytes()==(result_dir/f'{field}-{trial}.f32').read_bytes() for field in ('density','temperature') for trial in (1,2))
                    exact_density=bool(np.array_equal(density,expected_density));exact_temperature=bool(np.array_equal(temperature,expected_temperature))
                    finite=bool(np.isfinite(density).all() and np.isfinite(temperature).all())
                    mask=rate==0
                    unchanged=bool(np.array_equal(density[mask],initial_density[mask]) and np.array_equal(temperature[mask],initial_temperature[mask]))
                    density_delta=float((density.astype(np.float64)-initial_density).sum()/n**3)
                    temperature_delta=float((temperature.astype(np.float64)-initial_temperature).sum()/n**3)
                    ideal_relative_density=abs(density_delta-ideal)/ideal
                    ideal_relative_temperature=abs(temperature_delta-ideal)/ideal
                    key=(n,scene,case)
                    output_bytes=(result_dir/'density-0.f32').read_bytes()+(result_dir/'temperature-0.f32').read_bytes()
                    if opt==0:debug_outputs[key]=output_bytes
                    optimization_equal=None if opt==0 else output_bytes==debug_outputs[key]
                    passed=bool(repeats and exact_density and exact_temperature and finite and unchanged and
                                ideal_relative_density<=2e-6 and ideal_relative_temperature<=2e-6 and
                                (optimization_equal is not False))
                    overall &= passed
                    row=dict(name=name,n=n,scene=scene,case=case,steps=steps,opt=opt,source_records=source_records,
                             source_cells=int(np.count_nonzero(rate)),repeats_bitwise=repeats,cpu_density_bitwise=exact_density,
                             cpu_temperature_bitwise=exact_temperature,optimization_bitwise=optimization_equal,finite=finite,
                             outside_source_unchanged=unchanged,density_integrated_delta=density_delta,
                             temperature_integrated_delta=temperature_delta,ideal_real_delta=ideal,
                             density_ideal_relative_error=ideal_relative_density,temperature_ideal_relative_error=ideal_relative_temperature,
                             passed=passed)
                    rows.append(row)
                    files={str(p.relative_to(out)):sha(p) for p in result_dir.iterdir() if p.is_file()}
                    manifest['runs'].append(dict(name=name,command=command,files=files));(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
                    print(name,'PASS' if passed else 'FAIL',flush=True)
    if hashes!={str(p.relative_to(repo)):sha(p) for p in monitored} or sha(exe)!=manifest['executable_sha256']:raise RuntimeError('Sources/executable changed during run')
    manifest['complete']=True;(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    summary=dict(passed=bool(overall),runs=len(rows),worst_ideal_relative_error=max(max(r['density_ideal_relative_error'],r['temperature_ideal_relative_error']) for r in rows),
                 optimization_differences=[r['name'] for r in rows if r['optimization_bitwise'] is False],rows=rows,
                 scope='Normalized GPU source injection only; future coupled binding/schedule still unvalidated')
    (out/'analysis.json').write_text(json.dumps(summary,indent=2));print(json.dumps({k:v for k,v in summary.items() if k!='rows'},indent=2))


if __name__=='__main__':main()
