#!/bin/sh
# Fails when the upstream counterpart of a shadow-compiled file drifts.
# src/shadow/upstream.lock pins the git blob id of every upstream file that
# is shadow-copied or compiled with a forced prelude; see src/shadow/README.md.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
# upstream.lock is stored with LF, but a checkout with core.autocrlf=true --
# the default this repository is authored under -- hands it back as CRLF, and
# `read -r` leaves the \r on the last field. CI on Linux never sees it. The
# person following the drift procedure in src/shadow/README.md does, as a
# rev-parse failing on a path with an invisible character on the end, which
# reads as "that file is gone from upstream" rather than "your line endings".
# Stripping it here rather than pinning the file with .gitattributes, so the
# script also survives a zip download or a tree someone re-normalized.
cr="$(printf '\r')"
while read -r blob path; do
    [ -z "$blob" ] && continue
    path="${path%"$cr"}"
    actual="$(git -C "$root/musescore" rev-parse "HEAD:$path")"
    if [ "$actual" != "$blob" ]; then
        echo "SHADOW DRIFT: musescore/$path is $actual, lock expects $blob" >&2
        echo "  -> re-derive the shadow copy / prelude, then update src/shadow/upstream.lock" >&2
        fail=1
    fi
done < "$root/src/shadow/upstream.lock"
if [ "$fail" = 0 ]; then
    echo "shadow drift check: OK ($(wc -l < "$root/src/shadow/upstream.lock") pinned files)"
fi
exit $fail
