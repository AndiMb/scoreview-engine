#!/bin/sh
# Fails when the upstream counterpart of a shadow-compiled file drifts.
# src/shadow/upstream.lock pins the git blob id of every upstream file that
# is shadow-copied or compiled with a forced prelude; see src/shadow/README.md.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
while read -r blob path; do
    [ -z "$blob" ] && continue
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
