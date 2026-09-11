"""Standalone SVG table of the principal transport-probe measurements."""
import json
from pathlib import Path
import sys
root=Path(sys.argv[1]);data=json.loads((root/'summary.json').read_text())
rows=[r for r in data if r['shape']=='sharp' and r['shift_cells']==0 and r['dt_scale']==1]
rows.sort(key=lambda r:(r['resolution'],r['step']))
w,h=1120,475
s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" font-family="Segoe UI, sans-serif">',
   '<rect width="100%" height="100%" fill="white"/>',
   '<text x="30" y="32" font-size="23">Changed departures can produce identical sampled density</text>',
   '<text x="30" y="58" font-size="13">Original sharp field, dt = 1/60. Candidate projection versus the matched 8192-iteration reference.</text>']
xs=[30,180,385,620,820]
headers=['Grid / step','Changed departures','Changed hardware outputs','Hardware L1','Float-weight L1']
for x,label in zip(xs,headers):s.append(f'<text x="{x}" y="100" font-size="14" font-weight="600">{label}</text>')
for index,r in enumerate(rows):
    y=120+index*53
    s.append(f'<rect x="20" y="{y-4}" width="1070" height="49" rx="3" fill="{"#eef6f4" if r["hardware_changed_cells"]==0 else "#fff0d8"}"/>')
    vals=[f'{r["resolution"]}³ / {r["step"]}',f'{r["departure_changed_cells"]:,} / {r["cells"]:,}',
          f'{r["hardware_changed_cells"]:,}', 'ZERO' if r['hardware_l1']==0 else f'{r["hardware_l1"]:.3e}',f'{r["float_sample_delta_l1"]:.3e}']
    for x,v in zip(xs,vals):s.append(f'<text x="{x}" y="{y+25}" font-size="16">{v}</text>')
s.extend(['<text x="30" y="365" font-size="14">Within each run, departures are identical between steps. Candidate/reference input densities also match in these cases.</text>',
          '<text x="30" y="391" font-size="14">At 64³, the first update changes the source field; the same two departure maps then produce 80 different output cells.</text>',
          '<text x="30" y="423" font-size="12">Float-weight L1 is a diagnostic interpolation comparison at the recorded departures, not a continuum accuracy measure.</text>',
          '<text x="30" y="447" font-size="12">All hardware probe samples match actual production outputs exactly. Probe-enabled and disabled simulation fields match bit for bit.</text>',
          '</svg>'])
out=root/'figures';out.mkdir(exist_ok=True);(out/'sampling-response.svg').write_text('\n'.join(s),encoding='utf-8')
