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

# Progress map — dark grid-of-squares style: summary percentages plus groups of
# small squares with hover titles (same visual language as the reference SVG).
# status per check: 2 = verified, 1 = diagnostic/opt-in/seeded, 0 = not proven.
# A group's fill fraction is weighted: verified = 1, diagnostic = 0.5.
GREEN = "#2ea043"

SUMMARY = [
    ("Guest init", 3618, 3618),
    ("APS2 relocations", 565390, 565390),
    ("JNI slots", 231, 231),
    ("App-shell callback", 0, 1),
    ("Gate / marker", 1, 2),
]

GROUPS = [
    ("Native runtime", 2, [
        ("native ELF / no runtime", 2),
        ("guest init 3618/3618", 2),
        ("APS2 relocs 565390/565390", 2),
        ("Bionic TSD shim", 2),
        ("X11/EGL window", 2),
        ("first frame 0x649d422", 2),
    ]),
    ("JNI bridge", 2, [
        ("JNIEnv 231 slots", 2),
        ("cached JNI IDs 49", 2),
        ("fields / arrays / scalars", 2),
        ("VM ops + JavaVM install", 2),
        ("fn[224] self-pointer root cause", 2),
    ]),
    ("Diagnostics / seeded state", 1, [
        ("callback provenance (diag)", 1),
        ("notifier-hit probe (diag)", 1),
        ("app state (seeds only)", 1),
        ("singleton services", 1),
        ("controller installer", 1),
        ("MessageBus publish", 1),
        ("rung1 boundary replay", 1),
    ]),
    ("Not proven / withheld", 0, [
        ("gameLoadedCallback", 0),
        ("login / app-shell frame", 0),
        ("marker emitted", 0),
        ("rung2 StartLuaAppDM", 0),
    ]),
]

W = 1000
CELL, GAP, PER_ROW = 10, 1, 80
MARGIN = 4
GROUP_H = 42
HEADER_H = 81 + len(SUMMARY) * 30 + 10
H = HEADER_H + len(GROUPS) * GROUP_H + 10


def fraction(items):
    if not items:
        return 0.0
    score = sum(1.0 if s == 2 else 0.5 if s == 1 else 0.0 for _, s in items)
    return score / len(items)


all_items = [item for _, _, items in GROUPS for item in items]
counts = {s: sum(1 for _, v in all_items if v == s) for s in (2, 1, 0)}
overall = fraction(all_items) * 100

parts = [
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="Sober progress map">',
    f'<rect x="0.00" y="0.00" width="{W}.00" height="{H}.00" fill="{BG}" stroke="{BG}" stroke-width="0"/>',
    '<text x="4.00" y="21.00" font-family="sans-serif" font-size="16" fill="#ffffff" stroke="#0d1117" stroke-width="3" paint-order="stroke">Sober native Linux port — progress map</text>',
    f'<text x="4.00" y="51.00" font-family="sans-serif" font-size="16" fill="#ffffff" stroke="#0d1117" stroke-width="3" paint-order="stroke">Overall: {overall:.2f}% ({counts[2]} verified / {counts[1]} diagnostic / {counts[0]} not proven)</text>',
]
for i, (label, done, total) in enumerate(SUMMARY):
    p = 100.0 * done / total if total else 0.0
    parts.append(
        f'<text x="4.00" y="{81 + i * 30}.00" font-family="sans-serif" font-size="16" '
        f'fill="#ffffff" stroke="#0d1117" stroke-width="3" paint-order="stroke">'
        f'{esc(label)}: {p:.2f}% ({done}/{total})</text>'
    )

for gi, (name, status, items) in enumerate(GROUPS):
    p = fraction(items) * 100
    n2 = sum(1 for _, v in items if v == 2)
    n1 = sum(1 for _, v in items if v == 1)
    n0 = sum(1 for _, v in items if v == 0)
    y = HEADER_H + 16 + gi * GROUP_H
    color = {2: GREEN, 1: ORANGE, 0: RED}[status]
    parts.append(
        f'<g><title>{esc(name)}: {n2} verified / {n1} diagnostic / {n0} not proven ({p:.2f}%)</title>'
    )
    parts.append(
        f'<text x="4.00" y="{y}.00" font-family="sans-serif" font-size="16" fill="#ffffff" '
        f'stroke="#0d1117" stroke-width="3" paint-order="stroke">'
        f'{esc(name)}: {p:.2f}% ({n2}+{n1}/{len(items)})</text>'
    )
    filled = round(p / 100 * PER_ROW)
    for c in range(PER_ROW):
        x = MARGIN + c * (CELL + GAP)
        fill = color if c < filled else GRAY
        parts.append(
            f'<rect x="{x}.00" y="{y + 8}.00" width="{CELL}.00" height="{CELL}.00" '
            f'fill="{fill}" stroke="#0d1117" stroke-width="0.5"/>'
        )
    parts.append('</g>')

parts.append("</svg>")
(ASSETS / "progress.svg").write_text("\n".join(parts) + "\n")

print(f"generated {len(BADGES)} badges + progress.svg (grid style) in {ASSETS}")
