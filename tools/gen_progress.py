#!/usr/bin/env python3
"""Generate local status badges and a progress map in the style of AnyPS5's README.

All values are derived from the verified state recorded in PROGRESS.md and the
tests/ reports. The map is deliberately honest: the app-shell callback and the
marker are RED/WITHHELD, not shown as progress toward a false green.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "docs" / "assets"
ASSETS.mkdir(parents=True, exist_ok=True)

LABEL_BG = "#484f58"
TEXT = "#ffffff"
GREEN = "#2ea44f"
ORANGE = "#d29922"
RED = "#da3633"
BLUE = "#1f6feb"
GRAY = "#30363d"
BG = "#0d1117"

BADGES = [
    ("badge-guest-init.svg", "guest init", "3618/3618", GREEN),
    ("badge-relocs.svg", "APS2 relocs", "565390/565390", GREEN),
    ("badge-jni.svg", "JNI slots", "231/231", GREEN),
    ("badge-callback.svg", "app-shell callback", "0/1", RED),
    ("badge-render.svg", "native render", "3/3", GREEN),
    ("badge-verification.svg", "verification", "2/2", BLUE),
    ("badge-gate.svg", "decision 013 gate", "ENFORCED", GREEN),
    ("badge-marker.svg", "app shell rendered", "WITHHELD", ORANGE),
]


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def badge(name: str, label: str, value: str, color: str) -> None:
    lw = max(70, 8 * len(label) + 20)
    vw = max(60, 8 * len(value) + 20)
    w = lw + vw
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="20" role="img" aria-label="{esc(label)}: {esc(value)}">
  <linearGradient id="s" x2="0" y2="100%"><stop offset="0" stop-color="#bbb" stop-opacity=".1"/><stop offset="1" stop-opacity=".1"/></linearGradient>
  <clipPath id="r"><rect width="{w}" height="20" rx="3"/></clipPath>
  <g clip-path="url(#r)">
    <rect width="{lw}" height="20" fill="{LABEL_BG}"/>
    <rect x="{lw}" width="{vw}" height="20" fill="{color}"/>
    <rect width="{w}" height="20" fill="url(#s)"/>
  </g>
  <g fill="{TEXT}" text-anchor="middle" font-family="Verdana,DejaVu Sans,sans-serif" font-size="11">
    <text x="{lw / 2:.0f}" y="14">{esc(label)}</text>
    <text x="{lw + vw / 2:.0f}" y="14">{esc(value)}</text>
  </g>
</svg>
'''
    (ASSETS / name).write_text(svg)


for b in BADGES:
    badge(*b)

# Progress map: subsystem tiles. status: 2=done, 1=partial/diagnostic, 0=not proven.
TILES = [
    ("native ELF / no runtime", 2), ("guest init 3618/3618", 2),
    ("APS2 relocations", 2), ("Bionic TSD shim", 2),
    ("X11/EGL window", 2), ("first frame 0x649d422", 2),
    ("JNIEnv 231 slots", 2), ("cached JNI IDs 49", 2),
    ("JNI fields/arrays/scalars", 2), ("callback provenance (diag)", 1),
    ("notifier-hit probe (diag)", 1), ("strict gate 013/038", 2),
    ("verification + reports", 2), ("app state (seeds only)", 1),
    ("singleton services", 1), ("controller installer", 1),
    ("MessageBus publish", 1), ("gameLoadedCallback", 0),
    ("login/app-shell frame", 0), ("marker emitted", 0),
]

COLS = 5
TW, TH = 150, 46
PAD = 10
W = COLS * (TW + PAD) + PAD
rows = (len(TILES) + COLS - 1) // COLS
H = 70 + rows * (TH + PAD) + PAD

parts = [
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" role="img" aria-label="progress map">',
    f'<rect width="{W}" height="{H}" fill="{BG}"/>',
    '<text x="12" y="26" fill="#e6edf3" font-family="DejaVu Sans,sans-serif" font-size="16" font-weight="bold">Sober native Linux port — progress map</text>',
    '<text x="12" y="46" fill="#8b949e" font-family="DejaVu Sans,sans-serif" font-size="11">green = verified · orange = diagnostic/opt-in only · red = not proven / withheld</text>',
]
colors = {2: GREEN, 1: ORANGE, 0: RED}
for i, (label, status) in enumerate(TILES):
    x = PAD + (i % COLS) * (TW + PAD)
    y = 60 + (i // COLS) * (TH + PAD)
    c = colors[status]
    parts.append(f'<rect x="{x}" y="{y}" width="{TW}" height="{TH}" rx="4" fill="{c}" fill-opacity="0.22" stroke="{c}"/>')
    # wrap long labels
    words = label.split()
    lines, cur = [], ""
    for w in words:
        if len(cur) + len(w) + 1 > 20:
            lines.append(cur)
            cur = w
        else:
            cur = f"{cur} {w}".strip()
    if cur:
        lines.append(cur)
    for j, ln in enumerate(lines[:2]):
        parts.append(f'<text x="{x + TW / 2:.0f}" y="{y + 20 + j * 13}" fill="#e6edf3" text-anchor="middle" font-family="DejaVu Sans,sans-serif" font-size="10">{esc(ln)}</text>')
parts.append("</svg>")
(ASSETS / "progress.svg").write_text("\n".join(parts) + "\n")

print(f"generated {len(BADGES)} badges + progress.svg in {ASSETS}")
