"""Read-only field diagnosis of an interrupted offline replay; cannot pass a gate."""
import argparse
import json
from pathlib import Path
import numpy as np
from analyze_pressure_triage import divergence,laplace,project,rms,sha
from offline_reference_worker import read_fields


def main():
    parser=argparse.ArgumentParser();parser.add_argument('run',type=Path)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    records=[];hashes={}
    for folder in sorted((args.run/'offline').glob('step-*')):
        if not (folder/'w_after.f32').exists(): continue
        request=json.loads((folder/'request.json').read_text())
        n,dt=request['resolution'],request['dt']
        before=read_fields(folder,n,'before');after=read_fields(folder,n,'after')
        db=np.fromfile(folder/'div_before.f32',dtype='<f4').reshape(n,n,n).astype(float)
        da=np.fromfile(folder/'div_after.f32',dtype='<f4').reshape(n,n,n).astype(float)
        p=np.fromfile(folder/'pressure_uploaded.f32',dtype='<f4').reshape(n,n,n).astype(float)
        assert all(np.isfinite(x).all() for x in (db,da,p))
        replay=project(before,p,dt,arithmetic32=True)
        exact=project(before,p,dt)
        rounding=divergence([x.astype(float)-y for x,y in zip(replay,exact)])
        predicted=db-dt*laplace(p)
        accounted=divergence(before)-dt*laplace(p)+rounding
        records.append(dict(step=request['step'],projection_bitwise=all(np.array_equal(x,y) for x,y in zip(after,replay)),
            relative_divergence=rms(da)/rms(db),scaled_max=dt*float(abs(da).max()),
            ratio_identity_difference=abs(rms(predicted)-rms(da))/rms(db),
            identity_rms=rms(divergence(after)-predicted),
            explicit_rounding_accounted_identity_rms=rms(divergence(after)-accounted),
            projection_rounding_rms=rms(rounding),pre_divergence_rounding_rms=rms(divergence(before)-db)))
        for p in folder.iterdir():
            if p.suffix in ('.f32','.f64','.json') or p.name=='response.ready':
                hashes[str(p.relative_to(args.run))]=sha(p)
    summary=dict(scope='Diagnostic only: interrupted run has no completed normal-return/provenance seal or compact final CSV; cannot pass registered gate',
        analyzer_sha256=sha(Path(__file__)),completed_field_steps=len(records),
        input_sha256=hashes,records=records)
    args.output.write_text(json.dumps(summary,indent=2)+'\n')
    print('Diagnostic completed field steps:',len(records))
    print('First ideal-identity ratio exceedance:',next((r for r in records if r['ratio_identity_difference']>=5e-7),None))


if __name__=='__main__':main()
