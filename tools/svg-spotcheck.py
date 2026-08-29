#!/usr/bin/env python3
"""Rendered-pixel spot check between two directories of SVGs.

The Phase-3 acceptance instrument: compares same-named SVGs from a reference
generator (the released Qt webmscore) and a candidate (mscz2media --svg) by
(a) exact viewBox equality and (b) pixel statistics of both files rendered
with the same headless Chromium at the same width.

The two generators encode pages differently (Qt inlines every glyph path,
the candidate reuses <defs>), so nothing textual beyond the viewBox is
compared — the picture is the contract.

  svg-spotcheck.py --ref refdir --cand canddir --renderer chrome-headless-shell \
      [--width 1024] [--workdir tmp]

Prints one line per score and a summary; exits non-zero when a viewBox
differs or a rendering diverges beyond the thresholds (mean absolute
difference > 2/255, or > 2 % of pixels off by more than 32/255).
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

import urllib.parse

import numpy as np
from PIL import Image, ImageFilter

# Calibrated on the 15-score acceptance set of 2026-08-29: the ink-densest
# faithful pages (Duckwerk choral score, a drum exercise of one-line staves)
# reach mean 2.8 / 2.9 % from sub-pixel kerning shifts alone, while a real
# structural defect (glyphs rendered at 10x) measured mean 15 unblurred and
# far more blurred.
MEAN_THRESHOLD = 3.0        # of 255, over the whole page
BAD_PIXEL_LIMIT = 0.03      # fraction of pixels allowed off by > 32/255


def viewbox(path):
    head = open(path, encoding="utf-8", errors="replace").read(4096)
    m = re.search(r'viewBox="([^"]+)"', head)
    return m.group(1) if m else None


def viewbox_equal(a, b, tol=0.01):
    # generators format numbers differently (Qt: 6 significant digits);
    # compare numerically
    try:
        va = [float(x) for x in a.split()]
        vb = [float(x) for x in b.split()]
    except (AttributeError, ValueError):
        return False
    return len(va) == 4 and len(vb) == 4 and all(abs(x - y) <= tol * max(1.0, abs(x)) for x, y in zip(va, vb))


def render(renderer, svg_path, png_path, width, height, workdir):
    # Wrap in an HTML shell that scales the SVG to the window width: the two
    # generators declare different physical root sizes (mm vs px), and the
    # comparison must not depend on that.
    svg_url = "file:///" + urllib.parse.quote(os.path.abspath(svg_path).replace("\\", "/"))
    shell = os.path.join(workdir, os.path.basename(png_path) + ".html")
    with open(shell, "w", encoding="utf-8") as f:
        f.write(f'<!doctype html><body style="margin:0"><img src="{svg_url}" style="width:100%;display:block">')
    url = "file:///" + os.path.abspath(shell).replace("\\", "/")
    subprocess.run([renderer, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--virtual-time-budget=20000",
                    f"--screenshot={png_path}", f"--window-size={width},{height}", url],
                   check=True, capture_output=True, timeout=120)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True)
    ap.add_argument("--cand", required=True)
    ap.add_argument("--renderer", required=True)
    ap.add_argument("--width", type=int, default=1024)
    ap.add_argument("--workdir")
    args = ap.parse_args()

    names = sorted(n for n in os.listdir(args.ref)
                   if n.endswith(".svg") and os.path.exists(os.path.join(args.cand, n)))
    if not names:
        print("no common SVGs between the two directories", file=sys.stderr)
        return 2

    workdir = args.workdir or tempfile.mkdtemp(prefix="svg-spotcheck-")
    os.makedirs(workdir, exist_ok=True)

    failures = []
    print(f"{'score':38} {'viewBox':8} {'meanDiff':>8} {'badPx%':>7}  refB    candB")
    for name in names:
        ref_svg = os.path.join(args.ref, name)
        cand_svg = os.path.join(args.cand, name)

        vb_ref = viewbox(ref_svg)
        vb_cand = viewbox(cand_svg)
        vb_ok = vb_ref is not None and viewbox_equal(vb_ref, vb_cand)
        if not vb_ok:
            failures.append(f"{name}: viewBox {vb_ref!r} vs {vb_cand!r}")

        # render at identical size, derived from the reference viewBox
        try:
            _, _, w, h = (float(v) for v in vb_ref.split())
            height = max(1, int(args.width * h / w))
        except Exception:
            height = args.width

        ref_png = os.path.join(workdir, name + ".ref.png")
        cand_png = os.path.join(workdir, name + ".cand.png")
        render(args.renderer, ref_svg, ref_png, args.width, height, workdir)
        render(args.renderer, cand_svg, cand_png, args.width, height, workdir)

        # A light blur before the diff forgives the sub-pixel shifts the two
        # text-metric stacks produce (HarfBuzz vs Qt kerning) while leaving
        # structural divergence — missing, misplaced or mis-scaled elements —
        # clearly above the thresholds.
        def load(p):
            img = Image.open(p).convert("L").filter(ImageFilter.GaussianBlur(2))
            return np.asarray(img, dtype=np.int16)

        a = load(ref_png)
        b = load(cand_png)
        if a.shape != b.shape:
            hmin = min(a.shape[0], b.shape[0])
            wmin = min(a.shape[1], b.shape[1])
            a, b = a[:hmin, :wmin], b[:hmin, :wmin]
        diff = np.abs(a - b)
        mean = float(diff.mean())
        bad = float((diff > 32).mean())

        ok = vb_ok and mean <= MEAN_THRESHOLD and bad <= BAD_PIXEL_LIMIT
        if not ok and vb_ok:
            failures.append(f"{name}: meanDiff {mean:.2f}, badPx {bad * 100:.2f} %")
        print(f"{name:38} {'ok' if vb_ok else 'DIFF':8} {mean:8.2f} {bad * 100:6.2f}%  "
              f"{os.path.getsize(ref_svg):7} {os.path.getsize(cand_svg):7}")

    print(f"\n{len(names)} scores compared, renders in {workdir}")
    if failures:
        print(f"{len(failures)} failure(s):")
        for f in failures:
            print(f"  {f}")
        return 1
    print("spot check: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
