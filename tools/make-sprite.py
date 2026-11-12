#!/usr/bin/env python3
"""Generates web/sprite.svg from the G_* glyphs in src/ui.cpp (rows of '#' cells), so the web app's icons are the handheld's."""
import re, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
src = (root / "src" / "ui_glyphs.h").read_text()

# G_WEB isn't in ui_glyphs.h until the handheld side lands; the spec fixes its cells
extra = {
	# the web player's fullscreen button: corners out (enter) and in (leave)
	"FULLSCREEN": ["###...###", "#.......#", "#.......#", ".........", ".........", ".........", "#.......#", "#.......#", "###...###"],
	"FULLSCREEN_EXIT": ["..#...#..", "..#...#..", "###...###", ".........", ".........", ".........", "###...###", "..#...#..", "..#...#.."],
	"WEB": ["..#####..", ".#..#..#.", "#..#.#..#", "#########", "#..#.#..#", "#########", "#..#.#..#", ".#..#..#.", "..#####.."]
}

glyphs = {}
for m in re.finditer(r'GLYPH\(G_(\w+),((?:\s*"[.#]*",?)+)\s*\)', src):
	glyphs[m.group(1)] = re.findall(r'"([.#]*)"', m.group(2))
for k, v in extra.items():
	glyphs.setdefault(k, v)

out = ['<svg xmlns="http://www.w3.org/2000/svg" style="display:none">']
for name, rows in sorted(glyphs.items()):
	w, h = max(len(r) for r in rows), len(rows)
	rects = []
	for y, row in enumerate(rows):
		for run in re.finditer(r"#+", row):
			rects.append(f'<rect x="{run.start()}" y="{y}" width="{run.end() - run.start()}" height="1"/>')
	out.append(f'<symbol id="g-{name.lower().replace("_", "-")}" viewBox="0 0 {w} {h}" shape-rendering="crispEdges">{"".join(rects)}</symbol>')
out.append("</svg>")
(root / "web" / "sprite.svg").write_text("\n".join(out) + "\n")
print(f"{len(glyphs)} glyphs -> web/sprite.svg")

# (the dashboard's logo is web/logo.png: assets/icons/app.png at 128 px, `sips -Z 128` or any resampler; not made here)
