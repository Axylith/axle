#!/usr/bin/env python3
"""Generate progress badge from roadmap.yml."""
import yaml, os, sys

ROADMAP = "roadmap.yml"
BADGE   = ".github/badges/v1-progress.svg"
TABLE   = ".github/badges/v1-table.md"

with open(ROADMAP) as f:
    data = yaml.safe_load(f)

features = data["features"]
total    = len(features)
done     = sum(1 for f in features if f["done"])
pct      = int(100 * done / total) if total else 0

# pick a color
if   pct < 25: color = "#d73a4a"     # red
elif pct < 60: color = "#fbca04"     # yellow
elif pct < 90: color = "#0e8a16"     # green
else:          color = "#1f6feb"     # blue

# SVG progress bar (shields.io-ish style)
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="220" height="20">
  <linearGradient id="g" x2="0" y2="100%">
    <stop offset="0" stop-color="#444"/>
    <stop offset="1" stop-color="#222"/>
  </linearGradient>
  <rect rx="3" width="220" height="20" fill="#1a1a1a"/>
  <rect rx="3" x="80" width="140" height="20" fill="#2a2a2a"/>
  <rect rx="3" x="80" width="{int(140*pct/100)}" height="20" fill="{color}"/>
  <text x="40" y="14" fill="#ddd" text-anchor="middle" font-family="DejaVu Sans,Verdana,sans-serif" font-size="11">V1 progress</text>
  <text x="150" y="14" fill="#fff" text-anchor="middle" font-family="DejaVu Sans,Verdana,sans-serif" font-size="11">{done} / {total} ({pct}%)</text>
</svg>
'''

os.makedirs(os.path.dirname(BADGE), exist_ok=True)
with open(BADGE, "w") as f:
    f.write(svg)

# markdown checklist for the README
lines = ["| Feature | Status |", "|---|---|"]
for feat in features:
    icon = "✅" if feat["done"] else "⬜"
    lines.append(f"| {feat['name']} | {icon} |")
with open(TABLE, "w") as f:
    f.write("\n".join(lines))

print(f"V1 progress: {done}/{total} ({pct}%)")