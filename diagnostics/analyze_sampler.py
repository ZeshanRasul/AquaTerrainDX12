"""Prepare deterministic sampler queries and score predeclared models on held-out points."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

def prepare(root):
    if root.exists():
        raise ValueError('Use a new output directory')
    root.mkdir(parents=True)
    points, groups = [], []
    def add(group, p):
        points.append(p); groups.append(group)
    for i in range(8):
        add('corner', [(i >> a) & 1 for a in range(3)])
    add('sentinel', [.9881506, .1764555, 0])
    for a in range(3):
        for t in np.linspace(0, 1, 4097):
            p = [0., 0., 0.]; p[a] = t; add('axis', p)
        for k in range(256):
            for delta in [-2, -1, 0, 1, 2]:
                p = [0., 0., 0.]; p[a] = (k+.5)/256 + delta/65536
                add('threshold', p)
        for x in np.linspace(0, 1, 33):
            for y in np.linspace(0, 1, 33):
                p = [.375]*3; p[(a+1)%3] = x; p[(a+2)%3] = y
                add('plane', p)
    rng = np.random.default_rng(20260911)
    for p in rng.random((4096, 3)):
        add('heldout', p.tolist())
    for n in [32, 33, 64]:
        folder = root / str(n); folder.mkdir()
        base = np.array([n//3, n//2-1, 3*n//8], dtype=np.float32)
        positions = np.zeros((len(points), 4), dtype='<f4')
        positions[:, :3] = (base + np.float32(.5) + np.asarray(points, dtype=np.float32))/np.float32(n)
        positions.tofile(folder/'positions.f32')
    (root/'queries.json').write_text(json.dumps({'groups': groups, 'intended': points, 'seed': 20260911}))

def weights(f):
    return np.stack([np.prod(np.where(np.array([(i>>a)&1 for a in range(3)]), f, 1-f), axis=1) for i in range(8)], axis=1)

def analyze(root, out):
    out.mkdir(parents=True, exist_ok=True)
    groups = np.array(json.loads((root/'queries.json').read_text())['groups'])
    rows = []
    for n in [32, 33, 64]:
        folder = root/str(n)
        raw = [(folder/f'samples-{i}.f32').read_bytes() for i in range(3)]
        assert raw[0] == raw[1] == raw[2], 'Repeat mismatch'
        values = np.frombuffer(raw[0], dtype='<f4').reshape(-1, 20).astype(float)
        assert len(values) == len(groups) and np.isfinite(values).all()
        f, samples = values[:, :3], values[:, 4:]
        positions = np.fromfile(folder/'positions.f32', dtype='<f4').reshape(-1, 4)
        base = np.array([n//3,n//2-1,3*n//8], dtype=np.float32)
        reconstructed = (positions[:,:3]*np.float32(n)-np.float32(.5))-base
        assert np.array_equal(f,reconstructed), 'Coordinate reconstruction mismatch'
        c = np.fromfile(folder/'corners.f32', dtype='<f4').reshape(16, 8).astype(float)
        assert np.array_equal(samples[groups=='corner'], c.T), 'Exact corner mismatch'
        assert np.all(samples[:, 8] == 1), 'Constant preservation failed'
        basis = samples[:, :8]
        masks = {g: groups==g for g in ['axis', 'threshold', 'plane', 'heldout', 'sentinel']}
        models = {'float': weights(f)}
        for bits in range(4, 17):
            scale = 2**bits
            for mode, fn in [('nearest_even', np.rint), ('nearest_up', lambda x: np.floor(x+.5)), ('floor', np.floor), ('ceil', np.ceil)]:
                models[f'{mode}_{bits}'] = weights(fn(f*scale)/scale)
        # Select using axis data only; all multidimensional/random data are held out.
        scores = {name: float(np.max(np.abs(w[masks['axis']]-basis[masks['axis']]))) for name, w in models.items()}
        best = min(scores, key=scores.get)
        marginal = np.stack([basis[:,[i for i in range(8) if i & (1<<a)]].sum(axis=1) for a in range(3)],axis=1)
        details = {}
        for name in ['float', 'nearest_even_8', best]:
            w = models[name]
            details[name] = {g: {'basis_max_abs': float(np.max(np.abs(w[m]-basis[m]))),
                                  'values_max_abs': float(np.max(np.abs(w[m]@c.T-samples[m])))} for g,m in masks.items()}
        row = {'resolution': n, 'queries': len(values), 'repeats_bitwise': True,
               'selected_on_axis': best, 'axis_model_scores': scores, 'models': details,
               'partition_max_abs': float(np.max(np.abs(basis.sum(axis=1)-1))),
               'basis_256_lattice_max_abs': float(np.max(np.abs(basis*256-np.rint(basis*256)))),
               'separability_max_abs': float(np.max(np.abs(weights(marginal)-basis))),
               'basis_reconstruction_max_abs_by_texture': np.max(np.abs(basis@c.T-samples),axis=0).tolist(),
               'scale_256_max_abs': float(np.max(np.abs(samples[:,13]-samples[:,11]*256))),
               'offset_8_max_abs': float(np.max(np.abs(samples[:,14]-(samples[:,11]+8)))),
               'sentinel': values[masks['sentinel']].tolist(),
               'device': (folder/'device.txt').read_text(),
               'sample_sha256': hashlib.sha256(raw[0]).hexdigest()}
        rows.append(row)
    (out/'summary.json').write_text(json.dumps(rows, indent=2)+'\n')
    print(json.dumps([{k:r[k] for k in ['resolution','queries','selected_on_axis','partition_max_abs','scale_256_max_abs','offset_8_max_abs','basis_reconstruction_max_abs_by_texture']} for r in rows],indent=2))

if __name__ == '__main__':
    p=argparse.ArgumentParser(); p.add_argument('mode', choices=['prepare','analyze']); p.add_argument('root',type=Path); p.add_argument('--out',type=Path)
    args=p.parse_args()
    if args.mode=='prepare': prepare(args.root)
    else: analyze(args.root,args.out)
