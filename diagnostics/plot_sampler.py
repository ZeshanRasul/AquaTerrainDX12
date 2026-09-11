"""Compact scientific SVG from measured basis weights; no external plotting runtime."""
from pathlib import Path
import json
import numpy as np
root=Path('diagnostics/runs/sampler-v2')
groups=np.array(json.loads((root/'queries.json').read_text())['groups'])
v=np.fromfile(root/'32/samples-0.f32',dtype='<f4').reshape(-1,20)
a=v[groups=='axis'][:4097]
a=a[a[:,0]<=.03125]
x=lambda t: 80+float(t)/.03125*700
y=lambda t: 310-float(t)/.03125*220
poly=' '.join(f'{x(r[0]):.3f},{y(r[5]):.3f}' for r in a)
svg=f'''<svg xmlns="http://www.w3.org/2000/svg" width="880" height="560" viewBox="0 0 880 560">
<rect width="880" height="560" fill="white"/>
<g font-family="Arial, sans-serif" fill="#172333">
<text x="40" y="36" font-size="22">Measured sampler weights explain the earlier mismatch</text>
<text x="40" y="64" font-size="14">RTX 5090 · R32_FLOAT · linear clamp · 32³ texture</text>
<path d="M80 90 V310 H780" fill="none" stroke="#77818c"/>
<path d="M80 310 L780 90" stroke="#a6adb5" stroke-width="2"/>
<polyline points="{poly}" fill="none" stroke="#176d9c" stroke-width="2"/>
<text x="80" y="336" font-size="13">0</text><text x="720" y="336" font-size="13">fraction 1/32</text>
<text x="45" y="96" font-size="13">1/32</text><text x="59" y="314" font-size="13">0</text>
<text x="115" y="113" font-size="14" fill="#176d9c">Hardware: steps of 1/256</text>
<text x="115" y="137" font-size="14" fill="#66717b">Gray: ideal linear weight</text>
<text x="40" y="386" font-size="17">At fractions (0.9881506, 0.1764555, 0):</text>
<text x="40" y="416" font-size="16">Measured corner weights: (2, 209, 1, 44) / 256</text>
<text x="40" y="444" font-size="16">Upper-right weight: 0.171875; rounded-fraction product: 0.17372131</text>
<text x="40" y="484" font-size="14">Weights sum to 1, but do not factor into independent x, y, z weights.</text>
<text x="40" y="510" font-size="14">Measured weights predict generic [0, 1] corner data within 2.99e−8.</text>
<text x="40" y="538" font-size="12" fill="#66717b">Single GPU/driver observation; no claim about hardware internals or portability.</text>
</g></svg>'''
out=Path('experiments/sampler-microbenchmark/figures'); out.mkdir(exist_ok=True)
(out/'sampler-response.svg').write_text(svg,encoding='utf-8')
