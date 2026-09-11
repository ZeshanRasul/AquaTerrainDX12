#!/usr/bin/env python3
"""Error-vs-cost and error-vs-resolution plots for the advection resolution sweep.

Reads <run_root>/summary.json (written by Analyze-SmokeResolutionSweep.ps1) and
writes, per case, a log-log error-vs-cost SVG (accuracy against isolated advection
time, one point per resolution) to <run_root>/figures/. Pure standard library.

    python diagnostics/plot_resolution_sweep.py <run_root>
"""
import json, math, os, sys

C = {"sl": "#2563eb", "maccormack": "#dc2626"}

def logaxis(lo, hi, v):
    return (math.log10(max(v, 10.0 ** lo)) - lo) / (hi - lo)

def plot(case, series, xlabel, xkey, ykey, out, xdec, ydec):
    W, H, ML, MR, MT, MB = 760, 440, 82, 150, 48, 56
    PW, PH = W - ML - MR, H - MT - MB
    xs = [p[xkey] for s in series.values() for p in s]
    ys = [p[ykey] for s in series.values() for p in s]
    xlo, xhi = xdec; ylo, yhi = ydec
    def X(v): return ML + PW * logaxis(xlo, xhi, v)
    def Y(v): return MT + PH - PH * logaxis(ylo, yhi, v)
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" font-family="system-ui,Segoe UI,sans-serif">']
    s.append(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
    s.append(f'<text x="{ML}" y="26" font-size="16" font-weight="700" fill="#334155">Advection error vs cost: {case}</text>')
    s.append(f'<rect x="{ML}" y="{MT}" width="{PW}" height="{PH}" fill="none" stroke="#334155"/>')
    s.append(f'<text x="{ML+PW/2}" y="{H-14}" font-size="12" fill="#334155" text-anchor="middle">{xlabel}</text>')
    s.append(f'<text transform="translate(20,{MT+PH/2}) rotate(-90)" font-size="12" fill="#334155" text-anchor="middle">final normalized L1 error (log₁₀)</text>')
    for e in range(ylo, yhi + 1):
        yy = Y(10.0 ** e)
        s.append(f'<line x1="{ML}" y1="{yy:.1f}" x2="{ML+PW}" y2="{yy:.1f}" stroke="#e2e8f0"/>')
        s.append(f'<text x="{ML-8}" y="{yy+4:.1f}" font-size="10" fill="#334155" text-anchor="end">10^{e}</text>')
    for e in range(xlo, xhi + 1):
        xx = X(10.0 ** e)
        s.append(f'<line x1="{xx:.1f}" y1="{MT}" x2="{xx:.1f}" y2="{MT+PH}" stroke="#f1f5f9"/>')
        s.append(f'<text x="{xx:.1f}" y="{MT+PH+16}" font-size="10" fill="#334155" text-anchor="middle">10^{e}</text>')
    ly = MT + 12
    for mode, pts in sorted(series.items()):
        col = C.get(mode, "#334155")
        pts = sorted(pts, key=lambda p: p[xkey])
        line = " ".join(f"{X(p[xkey]):.1f},{Y(p[ykey]):.1f}" for p in pts)
        s.append(f'<polyline points="{line}" fill="none" stroke="{col}" stroke-width="2"/>')
        for p in pts:
            s.append(f'<circle cx="{X(p[xkey]):.1f}" cy="{Y(p[ykey]):.1f}" r="3.5" fill="{col}"/>')
            s.append(f'<text x="{X(p[xkey])+6:.1f}" y="{Y(p[ykey])-6:.1f}" font-size="9" fill="#64748b">{p["resolution"]}³</text>')
        s.append(f'<line x1="{ML+PW+18}" y1="{ly}" x2="{ML+PW+40}" y2="{ly}" stroke="{col}" stroke-width="2.5"/>')
        s.append(f'<text x="{ML+PW+46}" y="{ly+4}" font-size="12" fill="#334155">{mode}</text>')
        ly += 22
    s.append(f'<text x="{ML+PW+18}" y="{ly+10}" font-size="10" fill="#64748b">lower-left = better</text>')
    s.append(f'<text x="{ML+PW+18}" y="{ly+24}" font-size="10" fill="#64748b">labels: grid resolution</text>')
    s.append("</svg>")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(s))

def main(root):
    rows = json.load(open(os.path.join(root, "summary.json")))
    if isinstance(rows, dict):
        rows = [rows]
    figs = os.path.join(root, "figures")
    os.makedirs(figs, exist_ok=True)
    by_case = {}
    for r in rows:
        by_case.setdefault(r["case"], {}).setdefault(r["advection"], []).append(r)
    # Fixed decades keep SL/MacCormack comparable; widen if a value falls outside.
    for case, series in by_case.items():
        costs = [p["median_advection_ms"] for s in series.values() for p in s]
        errs = [p["final_l1_normalized"] for s in series.values() for p in s]
        xlo = int(math.floor(math.log10(max(min(costs), 1e-6))))
        xhi = int(math.ceil(math.log10(max(max(costs), 1e-6))))
        ylo = int(math.floor(math.log10(max(min(errs), 1e-6))))
        yhi = int(math.ceil(math.log10(max(max(errs), 1e-6))))
        plot(case, series, "isolated advection time / step (ms, log₁₀, optimized)",
             "median_advection_ms", "final_l1_normalized",
             os.path.join(figs, f"{case}-error-vs-cost.svg"), (xlo, xhi), (ylo, yhi))
        print(f"wrote figures/{case}-error-vs-cost.svg")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "diagnostics/runs/resolution-sweep")
