"""Compare already independently validated density-sampling runs across devices."""
import hashlib
import json
from pathlib import Path
import numpy as np

def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest().upper()
left=Path('diagnostics/runs/density-sampling-v1')
right=Path('diagnostics/runs/density-sampling-rtx3070/density-sampling-v1')
out=Path('experiments/density-sampling-rtx3070')
def read(p): return json.loads(p.read_text(encoding='utf-8-sig'))
a,b=read(left/'provenance.json'),read(right/'provenance.json')
assert a['jobs']==b['jobs'] and a['cases']==b['cases'], 'Different configurations'
source_checks=[]
for name,value in b['source_sha256'].items():
    assert sha(right/'source-snapshot'/name)==value.upper(), f'Bad snapshot hash: {name}'
    source_checks.append(dict(path=name,same_as_5090=value.upper()==a['source_sha256'][name].upper(),
        normalized_text_equal=(right/'source-snapshot'/name).read_text(encoding='utf-8-sig')==
                              (left/'source-snapshot'/name).read_text(encoding='utf-8-sig')))
records=[];devices=set()
for job in b['jobs']:
    tag=job['tag'];m=read(right/tag/'manifest.json');devices.add((m['gpu'],m['driver_version_raw']))
    files=[]
    for name in ['initial_density','div_before','pressure','div_after','u','v','w','density','trace-step1','trace-step2']:
        p=left/tag/(name+'.f32');q=right/tag/(name+'.f32')
        x=np.fromfile(p,dtype='<f4');y=np.fromfile(q,dtype='<f4')
        assert x.shape==y.shape and np.isfinite(y).all()
        files.append(dict(field=name,bitwise_equal=sha(p)==sha(q),changed_values=int(np.count_nonzero(x!=y)),
                          max_abs=float(np.max(abs(x.astype(float)-y.astype(float))))))
    records.append(dict(tag=tag,sampling=job['sampling'],files=files))
summary=read(out/'summary.json');baseline=read(Path('experiments/density-sampling/summary.json'))
key=lambda r:tuple(r[k] for k in ['shape','resolution','shift','dt_scale','step','sampling'])
base={key(r):r for r in baseline}; comparisons=[]
for r in summary:
    prior=base[key(r)]
    comparisons.append(dict(config=key(r),laptop_l1=r['normalized_l1'],desktop_l1=prior['normalized_l1'],
        absolute_l1_difference=abs(r['normalized_l1']-prior['normalized_l1']),
        laptop_changed_cells=r['changed_cells'],desktop_changed_cells=prior['changed_cells']))
report=dict(devices=list(devices),source_checks=source_checks,
    executable_hash_matches=a['executable_sha256'].upper()==b['executable_sha256'].upper(),
    hardware_exact_zero_comparisons=sum(r['sampling']=='hardware' and r['normalized_l1']==0 for r in summary),
    float_exact_zero_comparisons=sum(r['sampling']=='float' and r['normalized_l1']==0 for r in summary),
    max_reference_refinement_l1=max(r['reference_4096_8192_l1'] for r in summary),
    max_independent_reference_l1=max(r['reference_independent_l1'] for r in summary),
    per_run=records,summary_comparisons=comparisons)
(out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ['per_run','summary_comparisons']},indent=2))
for name in ['initial_density','pressure','u','v','w','density','trace-step1','trace-step2']:
    fields=[f for r in records for f in r['files'] if f['field']==name]
    print(name,'bitwise matches',sum(f['bitwise_equal'] for f in fields),'of',len(fields),'max abs',max(f['max_abs'] for f in fields))
print('Maximum L1 metric difference',max(r['absolute_l1_difference'] for r in comparisons))
