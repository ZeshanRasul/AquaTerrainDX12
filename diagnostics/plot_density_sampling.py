"""Static comparison table of actual one/two-step density updates."""
import json
from pathlib import Path
root=Path('experiments/density-sampling')
rows=json.loads((root/'summary.json').read_text())
keys=list(dict.fromkeys((r['shape'],r['resolution'],r['shift'],r['dt_scale']) for r in rows))
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="660" viewBox="0 0 1000 660">',
     '<rect width="1000" height="660" fill="white"/><g font-family="Arial, sans-serif" fill="#192d3b">',
     '<text x="35" y="38" font-size="23">Density interpolation changes the apparent projection plateau</text>',
     '<text x="35" y="67" font-size="14">Normalized L1 to each sampling path’s 8192-iteration reference · RTX 5090</text>',
     '<text x="505" y="103" font-size="16">Step 1</text><text x="790" y="103" font-size="16">Step 2</text>']
for x,label in [(35,'Shape / grid'),(205,'Shift'),(300,'dt scale'),(430,'Hardware'),(575,'Float'),(720,'Hardware'),(865,'Float')]:
    svg.append(f'<text x="{x}" y="132" font-size="14">{label}</text>')
for i,key in enumerate(keys):
    y=168+i*37
    if i%2==0: svg.append(f'<rect x="25" y="{y-22}" width="940" height="35" fill="#f1f5f8"/>')
    for x,label in [(35,f'{key[0]} / {key[1]}³'),(205,f'{key[2]:g}'),(300,f'{key[3]:g}')]:
        svg.append(f'<text x="{x}" y="{y}" font-size="14">{label}</text>')
    for x,step,method in [(430,1,'hardware'),(575,1,'float'),(720,2,'hardware'),(865,2,'float')]:
        r=next(r for r in rows if (r['shape'],r['resolution'],r['shift'],r['dt_scale'])==key and r['step']==step and r['sampling']==method)
        value=r['normalized_l1']; label='0 (exact)' if value==0 else f'{value:.3e}'
        color='#a23b16' if value==0 else '#192d3b'
        svg.append(f'<text x="{x}" y="{y}" font-size="14" fill="{color}">{label}</text>')
svg.extend(['<text x="35" y="556" font-size="15">Projection, initial density and departure coordinates are bitwise identical across paths.</text>',
    '<text x="35" y="585" font-size="15">Float interpolation exposes remaining sensitivity in every tested configuration.</text>',
    '<text x="35" y="620" font-size="13">Sensitivity to pressure budget, not total advection error. No performance or conservation claim.</text>',
    '</g></svg>'])
out=root/'figures';out.mkdir(exist_ok=True)
(out/'density-sampling.svg').write_text('\n'.join(svg),encoding='utf-8')
