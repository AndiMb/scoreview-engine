#!/usr/bin/env python3
"""Write a new MuseScore version into the files that configure the build.

The mechanical half of a MuseScore bump, and only that half. What this does:

    tools/deps.json               version and the submodule commit pin
    docs/dependencies.md          the MuseScore row of the inventory table
    README.md                     the release-tag and submodule-pin mentions
    web-public/package.json       the package version (it tracks MuseScore)
    web-public/package-lock.json  the same, in both places the lock keeps it

What it deliberately does not do: the submodule gitlink itself (the workflow
writes that with `git update-index --cacheinfo`, so MuseScore never has to be
checked out), and every judgement call — shadow copies, upstream.lock, the
corpus baseline and its waivers. Those are why the resulting pull request is
a draft. See src/shadow/README.md and docs/dependencies.md.

Anchored on field names and sentence fragments, never on the version string.
That is not fussiness: `git grep 4.7.4` finds sixteen places in this tree and
several of them must never move. README.md's `v4.7.4-scoreview.7`, the
`module` line of testdata/corpus-baseline.json and half of
testdata/corpus-waivers.json name the version something was *measured*
against — the frozen Qt reference. A global replace would leave the
comparison green while the provenance it claims became false.

Every edit must match exactly once, or the script exits 2 and changes
nothing. A bump that silently skipped a file is worse than one that failed:
it leaves a tree that builds and lies about what it is.

Usage: tools/bump-musescore.py 4.7.5 3654226c2e99289916916953a98e585a3d3b315a
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
SHA_RE = re.compile(r"^[0-9a-f]{40}$")


class BumpError(Exception):
    """An anchor did not match exactly once."""


PENDING = {}


def read(rel):
    """Lines of a file, read once and edited in memory until commit_all().

    newline="" so a CRLF working copy stays CRLF. The runner is Linux, but
    this is also the script you run by hand on Windows when the bot could
    not, and a bump that rewrote every line ending of README.md would bury
    the two lines it meant to change.
    """
    if rel not in PENDING:
        with (ROOT / rel).open("r", encoding="utf-8", newline="") as f:
            PENDING[rel] = f.read().split("\n")
    return PENDING[rel]


def commit_all():
    """Write every edited file, once every anchor has matched.

    Nothing reaches the disk before this. An anchor that fails on the fifth
    file would otherwise leave the first four bumped and the tree in a shape
    nobody asked for.
    """
    for rel, lines in PENDING.items():
        with (ROOT / rel).open("w", encoding="utf-8", newline="") as f:
            f.write("\n".join(lines))


def edit_line(rel, lines, index, old, new):
    """Replace `old` with `new` on one line, or raise."""
    line = lines[index]
    if old not in line:
        raise BumpError(f"{rel}:{index + 1}: expected {old!r} in {line.strip()!r}")
    if line.count(old) != 1:
        raise BumpError(f"{rel}:{index + 1}: {old!r} appears more than once")
    lines[index] = line.replace(old, new)
    return f"{rel}:{index + 1}"


def find_anchors(rel, lines, anchor, expect=1):
    hits = [i for i, l in enumerate(lines) if anchor in l]
    if len(hits) != expect:
        raise BumpError(
            f"{rel}: anchor {anchor!r} matched {len(hits)} lines, expected {expect}"
        )
    return hits


def field_after(rel, lines, start, field):
    """Index of the first line carrying `field`, at or after `start`."""
    for i in range(start, min(start + 12, len(lines))):
        if field in lines[i]:
            return i
    raise BumpError(f"{rel}: no {field!r} within 12 lines of line {start + 1}")


def bump(version, sha):
    deps = json.loads((ROOT / "tools" / "deps.json").read_text(encoding="utf-8"))
    ms = next((c for c in deps["components"] if c["name"] == "musescore"), None)
    if ms is None:
        raise BumpError("tools/deps.json: no component named musescore")
    old = ms["version"]
    old_sha = ms["verify"]["commit"]

    if old == version and old_sha == sha:
        print(f"already at {version} ({sha[:8]}), nothing to do")
        return []
    if old == version:
        raise BumpError(
            f"tools/deps.json says {version} already but pins {old_sha[:8]}, "
            f"not {sha[:8]} — resolve by hand"
        )

    touched = []

    # tools/deps.json — the manifest, and with it the pin that
    # check-deps.py --verify holds the checked-out submodule against.
    rel = "tools/deps.json"
    lines = read(rel)
    (at,) = find_anchors(rel, lines, '"name": "musescore",')
    touched.append(edit_line(rel, lines, field_after(rel, lines, at, '"version":'),
                             f'"{old}"', f'"{version}"'))
    touched.append(edit_line(rel, lines, field_after(rel, lines, at, '"commit":'),
                             old_sha, sha))

    # docs/dependencies.md — the prose twin of the manifest. Anchored on the
    # row label, so the cell moves and the note beside it stays.
    rel = "docs/dependencies.md"
    lines = read(rel)
    (at,) = find_anchors(rel, lines, "| MuseScore |")
    touched.append(edit_line(rel, lines, at, f"| {old} |", f"| {version} |"))

    # README.md — two mentions of the version in use. The other two mentions
    # in this file are v4.7.4-scoreview.7, the retired Qt build the corpus
    # baseline was measured against, and they stay where they are.
    rel = "README.md"
    lines = read(rel)
    (at,) = find_anchors(rel, lines, "npm tarballs attached to GitHub Releases")
    touched.append(edit_line(rel, lines, at, f"v{old}-engine.N", f"v{version}-engine.N"))
    (at,) = find_anchors(rel, lines, "pinned to an upstream release tag")
    touched.append(edit_line(rel, lines, at, f"(v{old})", f"(v{version})"))

    # The published package carries the MuseScore version as its own — the
    # tarball is scoreview-engine-<musescore version>.tgz, which is how
    # ScoreView's watcher reads the version back out of a release asset.
    rel = "web-public/package.json"
    lines = read(rel)
    (at,) = find_anchors(rel, lines, '"name": "scoreview-engine",')
    touched.append(edit_line(rel, lines, field_after(rel, lines, at, '"version":'),
                             f'"{old}"', f'"{version}"'))

    # The lock keeps the same version twice: once at the top level and once
    # for the root package under "packages". npm rewrites both; so do we,
    # rather than shelling out to npm for two strings.
    rel = "web-public/package-lock.json"
    lines = read(rel)
    for at in find_anchors(rel, lines, '"name": "scoreview-engine",', expect=2):
        touched.append(edit_line(rel, lines, field_after(rel, lines, at, '"version":'),
                                 f'"{old}"', f'"{version}"'))

    # Every anchor matched. Only now does anything reach the disk.
    commit_all()

    # Cheap last word: the three JSON files must still parse, and must now
    # agree on the new version. A broken deps.json would take the whole
    # weekly dependency check down with it.
    for rel in ("tools/deps.json", "web-public/package.json",
                "web-public/package-lock.json"):
        try:
            json.loads((ROOT / rel).read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            raise BumpError(f"{rel}: no longer valid JSON after the edit ({e})")

    deps = json.loads((ROOT / "tools" / "deps.json").read_text(encoding="utf-8"))
    ms = next(c for c in deps["components"] if c["name"] == "musescore")
    if ms["version"] != version or ms["verify"]["commit"] != sha:
        raise BumpError("tools/deps.json did not come out as intended")

    print(f"musescore {old} -> {version} ({old_sha[:8]} -> {sha[:8]})")
    for t in touched:
        print(f"  {t}")
    return touched


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().split("\n")[-1], file=sys.stderr)
        return 2
    version, sha = argv[1], argv[2].lower()
    if not VERSION_RE.match(version):
        print(f"not a MuseScore version: {version}", file=sys.stderr)
        return 2
    if not SHA_RE.match(sha):
        # Full 40 hex only. An abbreviated sha in a submodule pin is a
        # gitlink that resolves on one machine and not on the next.
        print(f"not a full commit sha: {sha}", file=sys.stderr)
        return 2
    try:
        bump(version, sha)
    except BumpError as e:
        # Anchors are checked before anything is written, so a failure here
        # normally leaves the tree untouched — unless it came from the
        # post-write validation, which says so in its own message.
        print(f"bump failed: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
