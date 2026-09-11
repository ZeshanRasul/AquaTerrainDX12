"""Ensure corrupt experimental inputs are rejected, without modifying source runs."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import numpy as np
from analyze_sampler import analyze

source = Path('diagnostics/runs/sampler-v2')
checks = []
for case in ['repeat','nonfinite','corner','coordinate','constant','truncated']:
    with tempfile.TemporaryDirectory(dir=source.parent) as tmp:
        root=Path(tmp)
        shutil.copy2(source/'queries.json',root/'queries.json')
        for n in [32,33,64]:
            shutil.copytree(source/str(n),root/str(n))
        paths=[root/'32'/f'samples-{i}.f32' for i in range(3)]
        values=np.fromfile(paths[0],dtype='<f4').reshape(-1,20)
        if case=='repeat': values[10,4]+=1
        if case=='nonfinite': values[10,4]=np.nan
        if case=='corner': values[0,4]=0
        if case=='coordinate': values[10,0]+=.25
        if case=='constant': values[10,12]=0
        if case=='truncated': values=values[:-1]
        for path in paths[:1] if case=='repeat' else paths:
            values.tofile(path)
        try:
            with contextlib.redirect_stdout(io.StringIO()): analyze(root,root/'report')
        except (AssertionError,ValueError): checks.append({'case':case,'rejected':True})
        else: raise AssertionError(f'{case} was accepted')
Path('experiments/sampler-microbenchmark/validation.json').write_text(json.dumps(checks,indent=2)+'\n')
print(f'{len(checks)} corrupted-input checks rejected as expected')
