"""Deterministic positive/negative controls for the production offline validator."""
import json
from pathlib import Path
import numpy as np
from analyze_offline_reference import primary_prediction, check_projection_identity
from analyze_pressure_triage import divergence, rms, sha


def rejected(call, criterion):
    try:
        call()
    except AssertionError as error:
        assert error.args[0] == criterion or error.args[0][0] == criterion, error.args
        return
    raise AssertionError('Corruption accepted: '+criterion)


def main():
    rng=np.random.default_rng(120926)
    records=[]
    for n in (4,8,32):
        dt=float(np.float32(1/60))
        before=[rng.normal(size=shape).astype(np.float32) for shape in
                ((n,n,n+1),(n,n+1,n),(n+1,n,n))]
        for v,axis in zip(before,(2,1,0)):
            index=[slice(None)]*3
            for edge in (0,n):
                index[axis]=edge;v[tuple(index)]=0
        p=rng.normal(size=(n,n,n)).astype(np.float32)
        db=divergence(before).astype(np.float32).astype(float)
        expected,predicted,ideal,pre,rounding=primary_prediction(before,p,db,dt)
        ratio=rms(divergence(expected))/rms(db)
        identity,difference=check_projection_identity(expected,expected,predicted,db,ratio)
        assert identity<1e-11
        damaged=[v.copy() for v in expected];damaged[0][1,1,1]+=np.float32(.01)
        rejected(lambda:check_projection_identity(expected,damaged,predicted,db,ratio),'projection')
        wrong_p=p.copy();wrong_p[1,1,1]+=np.float32(.01)
        wrong_expected,wrong_prediction,*_=primary_prediction(before,wrong_p,db,dt)
        rejected(lambda:check_projection_identity(wrong_expected,expected,wrong_prediction,db,ratio),'projection')
        rejected(lambda:check_projection_identity(expected,expected,predicted+.01,db,ratio),'field_identity')
        rejected(lambda:check_projection_identity(expected,expected,predicted,db,ratio+.001),'ratio_identity_difference')
        # Zero pressure satisfies the identity but leaves a divergent velocity.
        unchanged,underpredicted,*_=primary_prediction(before,np.zeros_like(p),db,dt)
        under_ratio=rms(divergence(unchanged))/rms(db)
        check_projection_identity(unchanged,unchanged,underpredicted,db,under_ratio)
        scaled=dt*float(abs(divergence(unchanged)).max())
        assert under_ratio>1e-4 and scaled>1e-5
        records.append(dict(n=n,identity_rms=identity,ratio_difference=difference,
            ideal_identity_rms=rms(divergence(expected)-ideal),
            corruptions_rejected=4,underprojected_ratio=under_ratio,
            underprojected_scaled_max=scaled))
    print(json.dumps(dict(script_sha256=sha(Path(__file__)),controls=records),indent=2))


if __name__=='__main__':main()
