#!/bin/sh
# Fails when the staged bump touches anything but the mechanical files.
#
# The line this draws is the whole reason the MuseScore bump can be automated
# at all. tools/bump-musescore.py rewrites version strings; everything else a
# bump needs is judgement — re-deriving the five shadow copies and the forced
# prelude, re-copying the verbatim data files under resources/, filling
# src/shadow/upstream.lock with new blob ids, and deciding one by one whether
# a corpus deviation is a regression or a waiver. If the bot ever wrote one of
# those, the draft pull request would arrive looking finished while nobody had
# looked at the part that matters.
#
# Run against the index, before the bot commits: on a violation nothing is
# pushed, no branch appears and no pull request opens.
#
# Deliberately NOT a check on the pull request itself. The human working in
# that pull request MUST touch upstream.lock and the waivers — that is the
# work. This is a property of the bot's commit, not of the branch.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"

# Kept in step with the file list in tools/bump-musescore.py, plus the
# submodule gitlink, which the workflow writes with `git update-index
# --cacheinfo` rather than by checking MuseScore out.
allowed="musescore
tools/deps.json
docs/dependencies.md
README.md
web-public/package.json
web-public/package-lock.json"

staged="$(git -C "$root" diff --cached --name-only)"

if [ -z "$staged" ]; then
    echo "BUMP DIFF: nothing staged - the bump produced no change" >&2
    exit 1
fi

fail=0
for path in $staged; do
    if ! printf '%s\n' "$allowed" | grep -qxF "$path"; then
        echo "BUMP DIFF: $path is not a mechanical bump file" >&2
        fail=1
    fi
done

if [ "$fail" != 0 ]; then
    echo "  -> the bot may only rewrite version strings; shadow copies," >&2
    echo "     upstream.lock, resources/ and the corpus baseline are the" >&2
    echo "     human half of the bump (src/shadow/README.md)" >&2
    exit 1
fi

echo "bump diff check: OK ($(printf '%s\n' "$staged" | wc -l | tr -d ' ') files, all mechanical)"
