# Vendored brotli decoder

FreeType needs a brotli decoder to read woff2 — the format this engine's
resource fonts use and the one `addFont()` callers most often hand over.
MuseScore's FreeType wrapper disables brotli, and no Emscripten sysroot ships
`libbrotlidec`, so the decoder is vendored here and answered to FreeType
through `cmake/FindBrotliDec.cmake`.

## What is here

Upstream: [google/brotli](https://github.com/google/brotli), tag **v1.2.0**,
release tarball SHA-256
`816c96e8e8f193b40151dad7e8ff37b1221d019dbcb9c35cd3fadbfe6477dfec`.

Unmodified, but not complete: only `c/common`, `c/dec` and the public headers
(minus `encode.h`) are kept. The encoder, the tools, the tests and the
language bindings are not vendored — nothing here writes woff2.

## Refreshing it

    curl -sSLO https://github.com/google/brotli/archive/refs/tags/v<version>.tar.gz
    sha256sum v<version>.tar.gz          # record it above
    tar xzf v<version>.tar.gz
    rm -rf brotli-<old>
    mkdir -p brotli-<version>/c
    cp -r brotli-<version>-src/c/common brotli-<version>-src/c/dec \
          brotli-<version>-src/c/include brotli-<version>/c/
    cp brotli-<version>-src/LICENSE brotli-<version>-src/README.md brotli-<version>/
    rm -f brotli-<version>/c/include/brotli/encode.h

Then update `BROTLI_DIR` and the source list in `CMakeLists.txt` — the file
list does change between releases (1.2.0 added `dec/prefix.c` and
`dec/static_init.c`) — and run the corpus gate: the decoder sits in the path
that produces every glyph.

## License

MIT, see `brotli-1.2.0/LICENSE`.
