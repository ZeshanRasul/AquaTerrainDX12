"""Full-field optimized/production comparison for the projection experiment."""
import json
from pathlib import Path
import sys
import numpy as np
from analyze_projection import load_run, require

optimized,production=map(Path,sys.argv[1:3])
configuration=json.loads((production/'provenance.json').read_text(encoding='utf-8-sig'))
records=[]
for shape in configuration['shapes']:
    for n in configuration['resolutions']:
        for method in configuration['modes']:
            for iterations in configuration['iterations']:
                tag=f'{shape}-r{n}-{method}-i{iterations}-rep1'
                om,of,op=load_run(optimized/tag)
                pm,pf,pp=load_run(production/tag)
                require(om['optimized'] and not pm['optimized'], 'Wrong compiler variants')
                for key in ('shape','resolution','advection','iterations','spacing','dt','fluid_density','limiter','closed_domain'):
                    require(om[key]==pm[key], 'Physical configuration mismatch: '+key)
                errors={key:float(np.max(np.abs(of[key]-pf[key]))) for key in of}
                l1=float(np.abs(of['density']-pf['density']).sum()/np.abs(pf['density']).sum())
                velocity=float(np.sqrt(sum(np.sum((of[k]-pf[k])**2) for k in ('u','v','w'))/sum(np.sum(pf[k]**2) for k in ('u','v','w'))))
                require(np.array_equal(of['initial_density'],pf['initial_density']), 'Initializer changed across build modes')
                require(l1<1e-6 and velocity<1e-5 and errors['div_after']<2e-4, 'Compiler variants disagree: '+tag)
                records.append(dict(config=tag,all_fields_identical=all(v==0 for v in errors.values()),max_absolute_by_field=errors,density_l1_relative=l1,velocity_l2_relative=velocity))
(production/'optimization_comparison.json').write_text(json.dumps(records,indent=2,allow_nan=False)+'\n')
print(f'PASS: {len(records)} compiler comparisons; {sum(r["all_fields_identical"] for r in records)} bit-identical full-field comparisons')
