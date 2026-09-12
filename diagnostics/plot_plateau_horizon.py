import json
import math
from pathlib import Path
root=Path('experiments/plateau-horizon')
rows=[r for r in json.loads((root/'summary.json').read_text()) if r['horizon']==r['step']]
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="980" height="500" viewBox="0 0 980 500"><rect width="980" height="500" fill="white"/><g font-family="Arial, sans-serif" fill="#213343">',
     '<text x="30" y="35" font-size="22">Pressure sensitivity after repeated density transport</text>',
     '<text x="30" y="61" font-size="14">Frozen projected velocity · sharp box · dt = 1/60 · RTX 5090</text>']
for panel,n in enumerate([32,64]):
    left=75+panel*475;top=115;width=360;height=255
    ymax=max(r['l1']*100 for r in rows if r['resolution']==n)*1.1
    X=lambda s:left+math.log(s/2)/math.log(60)*width
    Y=lambda e:top+height-height*e/ymax
    svg.append(f'<text x="{left}" y="95" font-size="18">{n}³ grid</text>')
    for i in range(5):
        val=ymax*i/4;y=Y(val)
        svg.extend([f'<path d="M{left} {y} h{width}" stroke="#dde3e8"/>',f'<text x="{left-8}" y="{y+4}" text-anchor="end" font-size="11">{val:.3f}%</text>'])
    for s in [2,8,32,120]:svg.append(f'<text x="{X(s)}" y="{top+height+23}" text-anchor="middle" font-size="12">{s}</text>')
    for method,color in [('hardware','#2266aa'),('float','#c05b25')]:
        rr=sorted([r for r in rows if r['resolution']==n and r['sampling']==method],key=lambda r:r['step'])
        pts=' '.join(f'{X(r["step"])},{Y(r["l1"]*100)}' for r in rr)
        svg.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="2"/>')
        for r in rr:svg.append(f'<circle cx="{X(r["step"])}" cy="{Y(r["l1"]*100)}" r="4" fill="{color}"/>')
    svg.append(f'<text x="{left+width/2}" y="423" text-anchor="middle" font-size="13">Advection steps (log spacing)</text>')
svg.extend(['<text x="30" y="455" font-size="13" fill="#2266aa">Blue: hardware</text><text x="175" y="455" font-size="13" fill="#c05b25">Orange: explicit float</text>',
    '<text x="30" y="481" font-size="12">Normalized L1 to each path’s 8192-iteration reference. Separate vertical scales; no visual-error claim.</text></g></svg>'])
(root/'figure.svg').write_text('\n'.join(svg),encoding='utf-8')
