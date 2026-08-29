#!/bin/sh
# Native Linux build via Docker — same environment as MuseScore's
# "Build: Without Qt" CI job (ubuntu-22.04, g++-10; the distro g++ 11 trips
# over a missing <memory> in kors_logger).
#
# usage: tools/build-native-docker.sh [ninja-args...]
# Build tree lives in the named volume sve-build; binary at /build/mscz2media.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
# Docker Desktop on Windows wants C:/-style paths
rootw="$(cygpath -m "$root" 2>/dev/null || echo "$root")"
vol=sve-build

docker build -t scoreview-engine-build "$rootw"

# Pre-seed HarfBuzz from prefetch/ — the muse_deps download resolves an
# IPv6-only host and fails inside the container.
docker run --rm -v "$rootw:/src:ro" -v $vol:/build scoreview-engine-build bash -c '
    set -e
    if [ ! -f /build/_deps/harfbuzz/harfbuzz.cmake ]; then
        mkdir -p /build/_deps/harfbuzz
        cp /src/prefetch/* /build/_deps/harfbuzz/
        cd /build/_deps/harfbuzz && cmake -E tar xf harfbuzz_src.7z
    fi
    cmake -S /src -B /build -GNinja -DCMAKE_BUILD_TYPE=Debug
    ninja -C /build '"$*"'
'
