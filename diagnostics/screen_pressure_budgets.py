"""Retrospective decision screen; not an online controller or performance claim."""
import hashlib
import json
from pathlib import Path
import numpy as np

root=Path('diagnostics/runs/projection-v1')
out=Path('experiments/pressure-budget-screen');out.mkdir(parents=True,exist_ok=True)
points=json.loads((root/'aggregate.json').read_text())
groups={}
for p in points:
    if 0<=p['iterations']<=2048:
        groups.setdefault((p['shape'],p['advection'],p['resolution']),[]).append(p)
for g in groups.values():g.sort(key=lambda p:p['iterations'])
assert len(groups)==8 and all([p['iterations'] for p in g]==[0,8,32,128,512,2048] for g in groups.values())
assert all(p['reference_4096_8192_l1']<2e-6 and p['reference_exact_velocity_l1']<2e-6 for p in points)
metrics={
    'relative_pressure_residual':lambda p:p['residual_relative'],
    'relative_divergence_rms':lambda p:p['divergence_rms']/p['divergence_before_rms'],
    'dt_max_divergence':lambda p:p['divergence_max']/60,
    'dt_density_weighted_divergence':lambda p:p['density_weighted_divergence_rms']/60,
}
results=[]
# Explicit exploratory tolerances, not calibrated perceptual thresholds.
for tolerance in [.01,.001,.0001]:
    training={k:g for k,g in groups.items() if k[2]==32}
    for name,metric in metrics.items():
        candidates=[]
        for threshold in np.logspace(-7,0,281):
            chosen=[next((p for p in g if metric(p)<=threshold),None) for g in training.values()]
            if all(p is not None and p['l1_to_converged_same_method']<=tolerance for p in chosen):
                candidates.append((sum(p['iterations'] for p in chosen),-threshold,threshold))
        assert candidates,'No calibration candidate'
        _,_,threshold=min(candidates)
        for key,g in groups.items():
            selected=next((p for p in g if metric(p)<=threshold),None)
            oracle=next((p for p in g if p['l1_to_converged_same_method']<=tolerance),None)
            results.append(dict(tolerance=tolerance,criterion=name,threshold=float(threshold),shape=key[0],advection=key[1],resolution=key[2],
                role='calibration' if key[2]==32 else 'resolution_transfer',
                selected_iterations=selected['iterations'] if selected else None,
                observed_l1=selected['l1_to_converged_same_method'] if selected else None,
                passed=selected is not None and selected['l1_to_converged_same_method']<=tolerance,
                hindsight_minimum_tested_iterations=oracle['iterations'] if oracle else None))
report=dict(description='Offline threshold calibration at 32 and resolution transfer to 64 on the same forcing family.',
    limitations=['No new GPU runs','Not a held-out flow family','Sparse candidate budgets','No monitoring overhead measured','Tolerances are exploratory, not visual thresholds'],
    max_residual_relative_divergence_disagreement=max(abs(metrics['relative_pressure_residual'](p)-metrics['relative_divergence_rms'](p)) for g in groups.values() for p in g),
    results=results,
    inputs={str(root/'aggregate.json'):hashlib.sha256((root/'aggregate.json').read_bytes()).hexdigest()},
    script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
(out/'screen.json').write_text(json.dumps(report,indent=2)+'\n')
print('Relative residual/divergence max disagreement:',report['max_residual_relative_divergence_disagreement'])
for tol in [.01,.001,.0001]:
    for name in metrics:
        r=[r for r in results if r['tolerance']==tol and r['criterion']==name and r['role']=='resolution_transfer']
        print(tol,name,'passes',sum(x['passed'] for x in r),'of',len(r),'budgets',[x['selected_iterations'] for x in r], 'oracle',[x['hindsight_minimum_tested_iterations'] for x in r])
