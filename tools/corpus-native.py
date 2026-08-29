#!/usr/bin/env python3
"""Convert a corpus of scores with mscz2media and fingerprint every output.

The native twin of the fork's web-example/corpus.cjs: same report shape, so
the same comparison answers "did anything change" against the released Qt
webmscore. No SVG yet (Phase 3) — corpus-compare.py skips svgBytes when the
candidate has none.

  corpus-native.py --bin /build/mscz2media --resources /src/resources \
      --scores /repo/vtest/scores --out report.json [--limit N]
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

# Fields that move on their own; corpus.cjs keeps the list next to the reason
# (title falls back to the file name for untitled scores).
VOLATILE = {"programVersion", "programRevision", "mscoreVersion", "encoding-date", "title"}


def stable_meta(meta):
    out = {}
    for key in sorted(meta.keys()):
        if key in VOLATILE:
            continue
        v = meta[key]
        # Keep the shape, not the contents, for the big nested fields.
        if isinstance(v, list):
            out[key] = len(v)
        elif isinstance(v, dict):
            out[key] = len(v.keys())
        else:
            out[key] = v
    return out


def fingerprint(binary, resources, score, workdir):
    proc = subprocess.run(
        [binary, score, "--resources", resources, "--out", workdir,
         "--midi", "--spos", "--mpos", "--meta"],
        capture_output=True, text=True, timeout=180)
    if proc.returncode != 0:
        tail = (proc.stderr or proc.stdout or "").strip().splitlines()
        raise RuntimeError(tail[-1] if tail else f"exit {proc.returncode}")

    pages = None
    for line in proc.stdout.splitlines():
        if line.startswith("pages="):
            pages = int(line.split()[0].split("=")[1])
    if pages is None:
        raise RuntimeError("no pages= line in output")

    def load(name):
        with open(os.path.join(workdir, name), encoding="utf-8") as f:
            return json.load(f)

    meta = load("meta.json")
    positions = load("mpos.json")   # corpus.cjs: savePositions(false) = measures
    segments = load("spos.json")    # corpus.cjs: savePositions(true) = segments
    midi_bytes = os.path.getsize(os.path.join(workdir, "score.mid"))

    return {
        "pages": pages,
        "measures": meta.get("measures"),
        "parts": len(meta.get("parts") or []),
        "posElements": len(positions.get("elements") or []),
        "posMeasures": len(positions.get("events") or []),
        "segElements": len(segments.get("elements") or []),
        "midiBytes": midi_bytes,
        "meta": stable_meta(meta),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--resources", required=True)
    ap.add_argument("--scores", action="append", required=True)
    ap.add_argument("--out", default="report.json")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    files = []
    for d in args.scores:
        files += [os.path.join(d, n) for n in sorted(os.listdir(d)) if n.lower().endswith(".mscz")]
    if args.limit:
        files = files[:args.limit]

    report = {"module": "mscz2media", "scores": {}}
    ok = failed = 0
    for i, f in enumerate(files, 1):
        key = os.path.basename(f)
        with tempfile.TemporaryDirectory() as workdir:
            try:
                report["scores"][key] = fingerprint(args.bin, args.resources, f, workdir)
                ok += 1
            except Exception as err:  # noqa: BLE001 — every failure is a data point
                report["scores"][key] = {"error": str(err)}
                failed += 1
        if i % 50 == 0:
            print(f"  {i}/{len(files)}", file=sys.stderr)

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    print(f"{len(files)} scores: {ok} converted, {failed} failed -> {args.out}")


if __name__ == "__main__":
    main()
