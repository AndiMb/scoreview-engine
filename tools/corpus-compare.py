#!/usr/bin/env python3
"""Compare a corpus-native.py report against a corpus.cjs baseline.

Same rules as the fork's corpus_diff.cjs: the count fields must match
exactly, the stable metadata must match, conversion failures in either
direction are news. svgBytes is skipped when the candidate has none (the
native pipeline grows SVG in Phase 3).

A waivers file records known, deliberate deviations per score and field
("pages", "midiBytes", "error", "meta.<key>", ...) with a reason; waived
differences are reported but not fatal. A waiver that no longer fires is
itself a regression — stale entries must be removed.

  corpus-compare.py baseline.json candidate.json \
      [--allow-meta KEY ...] [--waivers waivers.json]
"""

import argparse
import json
import sys

EXACT = ["pages", "measures", "parts", "posElements", "posMeasures", "segElements", "midiBytes"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline")
    ap.add_argument("candidate")
    ap.add_argument("--allow-meta", action="append", default=[],
                    help="metadata key allowed to differ (recorded, not fatal)")
    ap.add_argument("--waivers", help="JSON file of known deviations per score and field")
    ap.add_argument("--exact-svg", action="store_true",
                    help="compare svgBytes exactly too (native vs wasm of the same "
                         "engine; never against the Qt baseline, whose generator differs)")
    args = ap.parse_args()

    exact = EXACT + (["svgBytes"] if args.exact_svg else [])

    with open(args.baseline, encoding="utf-8") as f:
        base = json.load(f)["scores"]
    with open(args.candidate, encoding="utf-8") as f:
        cand = json.load(f)["scores"]

    waivers = {}
    if args.waivers:
        with open(args.waivers, encoding="utf-8") as f:
            waivers = {k: v for k, v in json.load(f).items() if not k.startswith("_")}
    unused_waivers = {(name, field) for name, fields in waivers.items() for field in fields}

    regressions = []
    notes = []

    def report(name, field, line):
        if field in waivers.get(name, {}):
            unused_waivers.discard((name, field))
            notes.append(f"{line} [waived: {waivers[name][field]}]")
        else:
            regressions.append(line)

    for name in sorted(set(base) | set(cand)):
        b = base.get(name)
        c = cand.get(name)

        if b is None:
            notes.append(f"{name}: only in candidate")
            continue
        if c is None:
            regressions.append(f"{name}: missing from candidate")
            continue

        if "error" in b and "error" not in c:
            notes.append(f"{name}: failed before, converts now ({b['error']})")
            continue
        if "error" not in b and "error" in c:
            report(name, "error", f"{name}: converted before, fails now: {c['error']}")
            continue
        if "error" in b and "error" in c:
            continue

        for key in exact:
            if b.get(key) != c.get(key):
                report(name, key, f"{name}: {key} {b.get(key)} -> {c.get(key)}")

        bm, cm = b.get("meta", {}), c.get("meta", {})
        changed = [k for k in set(bm) | set(cm) if bm.get(k) != cm.get(k)]
        for k in sorted(changed):
            if k in args.allow_meta:
                notes.append(f"{name}: metadata (allowed) {k} {bm.get(k)!r} -> {cm.get(k)!r}")
            else:
                report(name, f"meta.{k}", f"{name}: metadata {k} {bm.get(k)!r} -> {cm.get(k)!r}")

    for name, field in sorted(unused_waivers):
        regressions.append(f"{name}: waiver for '{field}' no longer fires — remove the stale entry")

    print(f"compared {len(set(base) | set(cand))} scores")
    print(f"  baseline: {args.baseline}")
    print(f"  candidate: {args.candidate}")

    if notes:
        print(f"\n{len(notes)} difference(s) within tolerance:")
        for n in notes[:40]:
            print(f"  {n}")
        if len(notes) > 40:
            print(f"  ... and {len(notes) - 40} more")

    if regressions:
        print(f"\n{len(regressions)} regression(s):")
        for r in regressions[:60]:
            print(f"  {r}")
        if len(regressions) > 60:
            print(f"  ... and {len(regressions) - 60} more")
        sys.exit(1)

    print("\nno regressions")


if __name__ == "__main__":
    main()
