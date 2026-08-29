#!/bin/sh
# wasm build via Docker — emscripten/emsdk pinned to the toolchain the Qt fork
# ships with (4.0.7), so toolchain drift never explains an output diff.
#
# usage: tools/build-wasm-docker.sh [make-args...]
# Build tree lives in the named volume sve-build-wasm; outputs
# (scoreview.lib.js/.wasm/.data) land in web-public/ afterwards via
# tools/collect-wasm.sh or manual docker cp — see CI.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
rootw="$(cygpath -m "$root" 2>/dev/null || echo "$root")"
vol=sve-build-wasm
image=emscripten/emsdk:4.0.7

# Pre-seed HarfBuzz from prefetch/ — the muse_deps download resolves an
# IPv6-only host and fails inside the container.
docker run --rm -v "$rootw:/src" -v $vol:/build $image bash -c '
    set -e
    if [ ! -f /build/_deps/harfbuzz/harfbuzz.cmake ]; then
        mkdir -p /build/_deps/harfbuzz
        cp /src/prefetch/* /build/_deps/harfbuzz/
        cd /build/_deps/harfbuzz && cmake -E tar xf harfbuzz_src.7z
    fi
    emcmake cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release
    cmake --build /build --target scoreview -- -j "$(nproc)" '"$*"'
    # publish the artifacts into the repo working tree (web-public consumes them)
    mkdir -p /src/web-public
    cp /build/scoreview.lib.js /build/scoreview.lib.wasm /build/scoreview.lib.data /src/web-public/ 2>/dev/null || true
    ls -la /build/scoreview.lib.* || true
'
