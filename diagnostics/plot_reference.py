#!/usr/bin/env python3
"""Error-vs-step plots for the passive-advection reference harness.

Reads <run_root>/<case-mode>/steps.csv, groups by case, overlays SL vs
MacCormack, and writes one SVG per case to <run_root>/figures/. Pure standard
library (no matplotlib), so it runs anywhere Python 3 is installed.

    python diagnostics/plot_reference.py <run_root>
"""
import csv, json, math, os, sys, glob

def load(run_dir):
    steps, l1 = [], []
    with open(os.path.join(run_dir, "steps.csv"), newline="") as f:
        for row in csv.DictReader(f):
            steps.append(int(row["step_index"]) if "step_index" in row else int(row["step"]))
            l1.append(float(row["l1_normalized"]))
    with open(os.path.join(run_dir, "manifest.json")) as f:
        m = json.load(f)
    return steps, l1, m

def svg(case, series, out_path):
    W, H, ML, MR, MT, MB = 760, 430, 78, 150, 46, 52
    PW, PH = W - ML - MR, H - MT - MB
    colours = {"sl": "#2563eb", "maccormack": "#dc2626"}
    xmax = max((max(s) for s, _, _ in series.values()), default=1)
    lo, hi = -8, 0  # log10 decades for normalized L1
    def x(step): return ML + PW * step / xmax
    def y(v):
        lv = math.log10(max(v, 10.0 ** lo))
        return MT + PH - PH * (lv - lo) / (hi - lo)
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" font-family="system-ui,Segoe UI,sans-serif">']
    s.append(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
    s.append(f'<text x="{ML}" y="26" font-size="16" font-weight="700" fill="#334155">Passive advection error: {case}</text>')
    s.append(f'<rect x="{ML}" y="{MT}" width="{PW}" height="{PH}" fill="none" stroke="#334155"/>')
    s.append(f'<text x="{ML+PW/2}" y="{H-14}" font-size="12" fill="#334155" text-anchor="middle">simulation step</text>')
    s.append(f'<text x="{ML+PW+10}" y="{MT+PH-12}" font-size="10" fill="#64748b">Values below 10^-8</text>')
    s.append(f'<text x="{ML+PW+10}" y="{MT+PH+2}" font-size="10" fill="#64748b">(including zero) clipped.</text>')
    s.append(f'<text transform="translate(18,{MT+PH/2}) rotate(-90)" font-size="12" fill="#334155" text-anchor="middle">normalized L1 error (log₁₀)</text>')
    for e in range(lo, hi + 1):
        yy = y(10.0 ** e)
        s.append(f'<line x1="{ML}" y1="{yy:.1f}" x2="{ML+PW}" y2="{yy:.1f}" stroke="#e2e8f0"/>')
        s.append(f'<text x="{ML-8}" y="{yy+4:.1f}" font-size="10" fill="#334155" text-anchor="end">10^{e}</text>')
    ly = MT + 12
    for mode, (steps, l1, _) in sorted(series.items()):
        col = colours.get(mode, "#334155")
        pts = " ".join(f"{x(st):.1f},{y(v):.2f}" for st, v in zip(steps, l1))
        s.append(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="2"/>')
        s.append(f'<line x1="{ML+PW+18}" y1="{ly}" x2="{ML+PW+40}" y2="{ly}" stroke="{col}" stroke-width="2.5"/>')
        s.append(f'<text x="{ML+PW+46}" y="{ly+4}" font-size="12" fill="#334155">{mode}</text>')
        ly += 22
    s.append("</svg>")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(s))

def main(root):
    figures = os.path.join(root, "figures")
    os.makedirs(figures, exist_ok=True)
    by_case = {}
    for run_dir in sorted(glob.glob(os.path.join(root, "*"))):
        if not os.path.isfile(os.path.join(run_dir, "steps.csv")):
            continue
        steps, l1, m = load(run_dir)
        case = m.get("scenario", os.path.basename(run_dir))
        mode = m.get("advection", "unknown")
        by_case.setdefault(case, {})[mode] = (steps, l1, m)
    for case, series in by_case.items():
        svg(case, series, os.path.join(figures, f"{case}.svg"))
        print(f"wrote figures/{case}.svg")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "diagnostics/runs/reference")
