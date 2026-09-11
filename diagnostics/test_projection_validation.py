"""Negative tests using a real, completed pilot run as a fixture."""
import csv
import json
from pathlib import Path
import shutil
import sys
import tempfile
import numpy as np
from analyze_projection import load_run

source=Path(sys.argv[1])
load_run(source)
def rejected(name, mutate):
    with tempfile.TemporaryDirectory(prefix='projection-validation-') as temp:
        target=Path(temp)/'run';shutil.copytree(source,target)
        mutate(target)
        try:load_run(target)
        except (ValueError,FileNotFoundError):print('PASS rejects',name)
        else:raise AssertionError('Accepted invalid data: '+name)

def rewrite_rows(p, edit):
    f=p/'trials.csv'
    with f.open() as stream:rows=list(csv.DictReader(stream))
    fields=list(rows[0]);edit(rows)
    with f.open('w',newline='') as stream:
        w=csv.DictWriter(stream,fieldnames=fields);w.writeheader();w.writerows(rows)

def change_field(p,name,index,value):
    f=p/(name+'.f32');a=np.fromfile(f,dtype='<f4');a[index]=value;a.tofile(f)

rejected('missing row',lambda p:rewrite_rows(p,lambda r:r.pop()))
rejected('duplicate trial',lambda p:rewrite_rows(p,lambda r:r[1].update(trial='1')))
rejected('nonfinite timing',lambda p:rewrite_rows(p,lambda r:r[0].update(total_ms='nan')))
rejected('nonidentical repetition',lambda p:rewrite_rows(p,lambda r:r[0].update(repeat_identical='0')))
rejected('closed-wall leak',lambda p:change_field(p,'v',0,1.0))
rejected('nonfinite density',lambda p:change_field(p,'density',0,float('nan')))
rejected('incorrect pressure field',lambda p:change_field(p,'pressure',100,10.0))
rejected('missing field',lambda p:(p/'density.f32').unlink())
