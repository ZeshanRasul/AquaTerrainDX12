"""Validate returned archive manifests and compare sampler buffers across devices."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest().upper()

def compare(laptop, baseline, package, out):
    expected=json.loads((package/'input-hashes.json').read_text(encoding='utf-8-sig'))
    received=json.loads((laptop/'input-hashes.json').read_text(encoding='utf-8-sig'))
    assert received==expected, 'Input manifest differs from supplied package'
    for entry in expected:
        assert sha(package/entry['path'])==entry['sha256'], 'Package input hash mismatch'
    hashes=json.loads((laptop/'result-hashes.json').read_text(encoding='utf-8-sig'))
    listed=set()
    for entry in hashes:
        p=(laptop/entry['path']).resolve()
        assert p.is_relative_to(laptop.resolve()), 'Invalid result path'
        assert p not in listed, 'Duplicate result hash entry'
        listed.add(p)
        assert sha(p)==entry['sha256'], 'Returned file hash mismatch'
    actual={p.resolve() for p in laptop.rglob('*') if p.is_file() and p.name!='result-hashes.json'}
    assert listed==actual, 'Result manifest coverage mismatch'
    for name in ['queries.json','Run.ps1']:
        assert (laptop/name).read_bytes()==(package/name).read_bytes(), 'Returned input differs'
    rows=[]
    for n in [32,33,64]:
        a=laptop/str(n);b=baseline/str(n)
        for name in ['positions.f32','corners.f32']:
            assert (a/name).read_bytes()==(b/name).read_bytes(), 'Numerical inputs differ'
        buffers=[(a/f'samples-{t}.f32').read_bytes() for t in range(3)]
        assert buffers[0]==buffers[1]==buffers[2], 'Laptop repeats differ'
        x=np.frombuffer(buffers[0],dtype='<f4').reshape(-1,20)
        y=np.fromfile(b/'samples-0.f32',dtype='<f4').reshape(-1,20)
        assert x.shape==y.shape and np.isfinite(x).all(), 'Invalid samples'
        rows.append(dict(resolution=n,queries=len(x),laptop_device=(a/'device.txt').read_text(),
            baseline_device=(b/'device.txt').read_text(),all_three_buffers_bitwise_equal_to_baseline=all(v==(b/'samples-0.f32').read_bytes() for v in buffers),
            changed_queries=int(np.any(x!=y,axis=1).sum()),changed_texture_samples=int(np.count_nonzero(x[:,4:]!=y[:,4:])),
            maximum_absolute_difference=float(np.max(np.abs(x.astype(float)-y))),
            laptop_sample_sha256=sha(a/'samples-0.f32'),baseline_sample_sha256=sha(b/'samples-0.f32')))
    out.mkdir(parents=True,exist_ok=True)
    report=dict(returned_file_hashes_verified=len(hashes),input_manifest_matches_package=True,
                inputs_bitwise_match_baseline=True,rows=rows,script_sha256=sha(Path(__file__)))
    (out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ['laptop','baseline','package','out']: p.add_argument(name,type=Path)
    a=p.parse_args();compare(a.laptop,a.baseline,a.package,a.out)
