"""Centre slices of the measured kinematic wall test, fixed density scale 0..1."""
from pathlib import Path
import numpy as np
root=Path('diagnostics/runs/coarse-obstacles-v1');out=Path('experiments/coarse-obstacles')
n=32;cell=6
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="990" height="335" viewBox="0 0 990 335"><rect width="990" height="335" fill="white"/><g font-family="Arial, sans-serif" fill="#203344">',
    '<text x="25" y="30" font-size="21">Coarse-wall baseline: density disappears before it crosses</text>',
    '<text x="25" y="53" font-size="13">32³ · Courant number 0.5 · 24 steps · prescribed, non-divergence-free velocity</text>']
for panel,(mode,label) in enumerate([(10,'No wall'),(11,'One-cell wall'),(12,'Slanted one-cell wall'),(13,'Quarter-cell wall')]):
    left=25+panel*245;top=91
    d=np.fromfile(root/f'r32-wall{mode}-cfl0.5/density.f32',dtype='<f4').reshape(n,n,n)[n//2]
    svg.append(f'<text x="{left}" y="79" font-size="14">{label}</text>')
    for j in range(n):
        for i in range(n):
            centre=.5 if mode==13 else .5+.5/n
            if mode==12:centre+=.25*((j+.5)/n-.5)
            solid=mode!=10 and abs((i+.5)/n-centre)<=((.25 if mode==13 else 1)/n)/2
            grey=round(255*(1-float(d[j,i])))
            color='#cc7733' if solid else f'rgb({grey},{grey},{grey})'
            svg.append(f'<rect x="{left+i*cell}" y="{top+(n-1-j)*cell}" width="{cell}" height="{cell}" fill="{color}"/>')
    svg.append(f'<rect x="{left}" y="{top}" width="192" height="192" fill="none" stroke="#82909a"/>')
svg.extend(['<text x="25" y="310" font-size="13">Black: density 1; white: 0. Orange: solid cells. Thin wall has no voxel representation at this phase.</text>',
    '<text x="25" y="330" font-size="12">This is a transport stress test, not a projected smoke plume or a production mass-loss measurement.</text></g></svg>'])
(out/'slices.svg').write_text('\n'.join(svg),encoding='utf-8')
