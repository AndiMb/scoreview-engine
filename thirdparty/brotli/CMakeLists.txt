# Brotli decoder (google/brotli 1.2.0, MIT), vendored for FreeType's woff2
# support: woff2 stores its font tables brotli-compressed, so without this
# FT_New_Face refuses every .woff2 — the resource pack and every font handed
# to addFont() would have to be otf/ttf.
#
# Decoder only. The encoder (c/enc) is not vendored: nothing here writes
# woff2. Provenance and how to refresh the copy: README.md next to this file.
#
# Answered to FreeType through cmake/FindBrotliDec.cmake, which stands in for
# the system libbrotlidec that no Emscripten sysroot has.

set(BROTLI_DIR ${CMAKE_CURRENT_LIST_DIR}/brotli-1.2.0)

add_library(brotlidec STATIC
    ${BROTLI_DIR}/c/common/constants.c
    ${BROTLI_DIR}/c/common/context.c
    ${BROTLI_DIR}/c/common/dictionary.c
    ${BROTLI_DIR}/c/common/platform.c
    ${BROTLI_DIR}/c/common/shared_dictionary.c
    ${BROTLI_DIR}/c/common/transform.c
    ${BROTLI_DIR}/c/dec/bit_reader.c
    ${BROTLI_DIR}/c/dec/decode.c
    ${BROTLI_DIR}/c/dec/huffman.c
    ${BROTLI_DIR}/c/dec/prefix.c
    ${BROTLI_DIR}/c/dec/state.c
    ${BROTLI_DIR}/c/dec/static_init.c
)

# The public headers are <brotli/decode.h>; the sources reach their internal
# headers relatively ("../common/platform.h"), so the tree layout is enough.
target_include_directories(brotlidec PUBLIC ${BROTLI_DIR}/c/include)

if(COMMAND target_no_warning)
    target_no_warning(brotlidec -Wno-conversion)
endif()
