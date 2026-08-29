# Stands in for the system libbrotlidec that FreeType looks for.
#
# FreeType's own FindBrotliDec.cmake asks pkg-config and then searches the
# sysroot — under Emscripten there is nothing to find, and on a build host
# whatever happens to be installed would decide whether woff2 works. Both are
# wrong for a build whose output must be byte-comparable between native and
# wasm, so the decoder is vendored (thirdparty/brotli) and answered here.
#
# FreeType appends its own module directory (list(APPEND CMAKE_MODULE_PATH)),
# so this file wins as long as cmake/ is on the path first — the top-level
# CMakeLists puts it there.

get_filename_component(BROTLIDEC_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../thirdparty/brotli/brotli-1.1.0/c/include ABSOLUTE)

set(BROTLIDEC_LIBRARIES brotlidec)   # the target from thirdparty/brotli
set(BROTLIDEC_DEFINITIONS "")
set(BROTLIDEC_VERSION 1.1.0)
set(BROTLIDEC_FOUND TRUE)
