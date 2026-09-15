#!/bin/sh
# Fails when the upstream counterpart of a file this repository copies drifts,
# and when a verbatim copy in this tree stops matching the blob it is pinned
# to. src/shadow/README.md has the procedure for both.
#
# src/shadow/upstream.lock is, per line:
#
#     <blob id>  <path in the submodule>  [<verbatim copy in this tree>]
#
# Two fields pin the upstream side only -- a shadow copy carries a marked diff
# and a prelude target has no copy at all, so neither can be byte-compared.
# The third field says the copy is upstream's bytes with no diff whatsoever
# (CRLF normalized to LF, nothing else): the data files under resources/, which
# no compiler ever looks at. Those get compared in both directions, because a
# stale or hand-edited one is exactly the failure nothing else in the build
# would notice.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
pinned=0
copies=0
# upstream.lock is stored with LF, but a checkout with core.autocrlf=true --
# the default this repository is authored under -- hands it back as CRLF, and
# `read -r` leaves the \r on the last field. CI on Linux never sees it. The
# person following the drift procedure in src/shadow/README.md does, as a
# rev-parse failing on a path with an invisible character on the end, which
# reads as "that file is gone from upstream" rather than "your line endings".
# Stripping it here rather than pinning the file with .gitattributes, so the
# script also survives a zip download or a tree someone re-normalized.
cr="$(printf '\r')"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

# `|| [ -n "$blob" ]`: read answers non-zero on a final line with no trailing
# newline, and the loop would drop that line unprocessed. upstream.lock is
# hand-edited at every MuseScore bump, on Windows -- a guard that quietly stops
# checking its last pin while still printing OK is worse than no guard at all.
while read -r blob path copy || [ -n "$blob" ]; do
    blob="${blob%"$cr"}"
    case "$blob" in
        '' | '#'*) continue ;;
    esac
    path="${path%"$cr"}"
    copy="${copy%"$cr"}"
    pinned=$((pinned + 1))

    actual="$(git -C "$root/musescore" rev-parse --quiet --verify "HEAD:$path" 2>/dev/null)" || actual=''
    if [ -z "$actual" ]; then
        echo "SHADOW DRIFT: musescore/$path is gone from upstream (lock expects $blob)" >&2
        echo "  -> the file moved or was removed; find its successor, then update src/shadow/upstream.lock" >&2
        fail=1
        continue
    fi
    if [ "$actual" != "$blob" ]; then
        echo "SHADOW DRIFT: musescore/$path is $actual, lock expects $blob" >&2
        echo "  -> re-derive the shadow copy / prelude / verbatim copy, then update src/shadow/upstream.lock" >&2
        fail=1
        continue
    fi

    [ -n "$copy" ] || continue
    copies=$((copies + 1))
    if [ ! -f "$root/$copy" ]; then
        echo "COPY MISSING: $copy is pinned to musescore/$path but is not in the tree" >&2
        fail=1
        continue
    fi
    # tr -d '\015' rather than '\r': POSIX tr is only required to know the
    # octal escape, and this script runs under dash on the runner.
    git -C "$root/musescore" cat-file blob "$blob" | tr -d '\015' > "$tmp"
    if ! tr -d '\015' < "$root/$copy" | cmp -s - "$tmp"; then
        echo "COPY DRIFT: $copy no longer matches musescore/$path" >&2
        echo "  -> the pin is current, so upstream did not move -- this copy was edited." >&2
        echo "     Re-copy it (CRLF -> LF) or explain the difference by making it a shadow copy." >&2
        fail=1
    fi
done < "$root/src/shadow/upstream.lock"

if [ "$fail" = 0 ]; then
    echo "shadow drift check: OK ($pinned pinned files, $copies of them compared byte for byte)"
fi
exit $fail
