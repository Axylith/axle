#!/usr/bin/env python3
"""
Generate the V1 progress badge SVG from roadmap.yml.

Run manually:    python tools/generate_progress.py
Run from CI:     .github/workflows/progress.yml invokes this on roadmap.yml changes.

Output is a single SVG at .github/assets/v1-progress.svg, designed to match
the visual language of the header (warm amber accent, dark background,
JetBrains Mono typography).
"""

import os
import sys
import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROADMAP = os.path.join(ROOT, "roadmap.yml")
OUTPUT = os.path.join(ROOT, ".github", "assets", "v1-progress.svg")


def render(features, target):
    total = len(features)
    done = sum(1 for f in features if f.get("done"))
    pct = (done / total * 100) if total else 0

    # Layout constants — match header's visual rhythm.
    W = 900
    H = 60 + 36 * total + 40   # header + rows + footer

    # Color tier for the bar, based on completion.
    if pct < 25:
        accent_a, accent_b = "#8a4a3a", "#6e3a2e"
    elif pct < 60:
        accent_a, accent_b = "#a87838", "#8a6228"
    elif pct < 90:
        accent_a, accent_b = "#c89858", "#b88848"
    else:
        accent_a, accent_b = "#6e8a48", "#5a7238"

    # Pre-compose the feature rows.
    rows = []
    y0 = 90
    for i, feat in enumerate(features):
        y = y0 + i * 36
        is_done = feat.get("done", False)
        name = feat["name"]
        # Status mark on the left.
        if is_done:
            mark = (f'<g transform="translate(50,{y - 14})" stroke="{accent_a}" '
                    f'fill="none" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">'
                    f'<polyline points="2,9 7,14 17,4"/></g>')
        else:
            mark = (f'<circle cx="60" cy="{y - 7}" r="6.5" fill="none" '
                    f'stroke="#3a3d44" stroke-width="1.2"/>')
        # Row number on the far left.
        num = f'<text x="32" y="{y - 1}" fill="#3a3d44" text-anchor="end" font-size="10" letter-spacing="1">{i+1:02d}</text>'
        # Feature name.
        color = "#e8dcc8" if is_done else "#6a6258"
        nm = f'<text x="86" y="{y - 1}" fill="{color}" font-size="14" font-weight="{500 if is_done else 400}">{name}</text>'
        # Light divider below each row.
        div = f'<line x1="40" y1="{y + 12}" x2="860" y2="{y + 12}" stroke="#1a1c20" stroke-width="0.5"/>'
        rows.append(num + mark + nm + div)
    rows_svg = "\n    ".join(rows)

    # Progress bar geometry.
    bar_y = H - 36
    bar_track_w = 600
    bar_track_x = 200
    bar_fill_w = int(bar_track_w * (pct / 100))

    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="V1 progress: {done} of {total} features complete">
  <defs>
    <linearGradient id="bgp" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#0d0e10"/>
      <stop offset="100%" stop-color="#13151a"/>
    </linearGradient>
    <linearGradient id="barfill" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%" stop-color="{accent_a}"/>
      <stop offset="100%" stop-color="{accent_b}"/>
    </linearGradient>
  </defs>

  <rect width="{W}" height="{H}" fill="url(#bgp)" rx="8"/>

  <!-- Header strip -->
  <text x="40" y="36" font-family="JetBrains Mono, monospace" font-size="11" fill="#5a5249" letter-spacing="2">
    V1 ROADMAP &#x2022; {target.upper()}
  </text>
  <text x="860" y="36" font-family="JetBrains Mono, monospace" font-size="11" text-anchor="end" fill="{accent_a}" letter-spacing="2">
    {done}/{total} &#x2022; {pct:.0f}%
  </text>
  <line x1="40" y1="46" x2="860" y2="46" stroke="#2a2d34" stroke-width="1"/>

  <!-- Feature rows -->
  <g font-family="JetBrains Mono, monospace">
    {rows_svg}
  </g>

  <!-- Progress bar -->
  <g transform="translate(0, {bar_y})">
    <text x="40" y="0" font-family="JetBrains Mono, monospace" font-size="10" fill="#5a5249" letter-spacing="1.5">PROGRESS</text>
    <rect x="{bar_track_x}" y="-10" width="{bar_track_w}" height="12" rx="3" fill="#1a1c20" stroke="#2a2d34" stroke-width="0.5"/>
    <rect x="{bar_track_x}" y="-10" width="{bar_fill_w}" height="12" rx="3" fill="url(#barfill)"/>
    <text x="860" y="0" font-family="JetBrains Mono, monospace" font-size="10" text-anchor="end" fill="#6a6258" letter-spacing="0.5">PRE-V1</text>
  </g>
</svg>
"""


def main():
    if not os.path.exists(ROADMAP):
        print(f"error: {ROADMAP} not found", file=sys.stderr)
        return 1
    with open(ROADMAP) as f:
        data = yaml.safe_load(f)

    features = data.get("features", [])
    target = data.get("target", "Initial release")

    svg = render(features, target)
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w") as f:
        f.write(svg)

    done = sum(1 for x in features if x.get("done"))
    print(f"wrote {OUTPUT}  ({done}/{len(features)} features)")
    return 0


if __name__ == "__main__":
    sys.exit(main())