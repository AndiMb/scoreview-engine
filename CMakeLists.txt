# Qt-free build of MuseScore's engraving engine from the unpatched submodule.
#
# The internal font stack (FontsEngine/FontFaceFT, FreeType + HarfBuzz) is
# compiled from the submodule WITHOUT MUSE_MODULE_DRAW_USE_QTTEXTDRAW — the
# configuration no upstream build compiles. Missing symbols come from a forced
# prelude header, the one file needing a real change is shadow-copied; both
# are pinned by src/shadow/upstream.lock (tools/check-shadow-drift.sh).

cmake_minimum_required(VERSION 3.16)

project(scoreview-engine LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(MU_ROOT ${CMAKE_CURRENT_LIST_DIR}/musescore)
set(MUSE_FRAMEWORK_PATH ${MU_ROOT})

set(CMAKE_MODULE_PATH
    ${MU_ROOT}/buildscripts
    ${MU_ROOT}/buildscripts/cmake
    ${MU_ROOT}/src/framework/cmake
    ${CMAKE_MODULE_PATH}
)

include(SetupBuildEnvironment)
include(GetPlatformInfo)

if(EMSCRIPTEN)
    # muse_global's zip/serialization code includes zlib.h and expects the
    # library "from the main linker" under emcc — emscripten-ports provides
    # both, but only if the flag is set at compile time for every module.
    add_compile_options("-sUSE_ZLIB=1")
endif()

set(THIRDPARTY_DIR ${MU_ROOT}/thirdparty)

set(MUSE_QT_SUPPORT OFF)
include(${MU_ROOT}/version.cmake)   # must precede MuseSetupConfiguration: sets MUSE_APP_VERSION* for muse_framework_config.h
include(${MU_ROOT}/src/framework/cmake/MuseSetupConfiguration.cmake)

set(GLOBAL_NO_INTERNAL ON)
add_subdirectory(${MU_ROOT}/src/framework/global global)

# draw as the base (types, Painter, BufferedPaintProvider) without internal —
# the font stack is added below as our own source list.
set(DRAW_NO_INTERNAL ON)
add_subdirectory(${MU_ROOT}/src/framework/draw draw)

# FreeType from the tree, HarfBuzz as in the regular build (muse_deps download;
# prefetch/ can pre-seed the archive for offline container builds).
add_subdirectory(${MU_ROOT}/src/framework/draw/thirdparty/freetype freetype)
include(${MU_ROOT}/src/framework/draw/cmake/SetupHarfBuzz.cmake)

# msdfgen: removed upstream 2024-03-29 (c7cf38d1f6, "removed msdfgen and
# harfbuzz") but still referenced by the Qt-free branch (ifontface.h,
# fontsengine.cpp, fontfaceft.cpp). Vendored from history — a *modified* 1.4,
# stock does not fit. Build-only; nothing in the conversion path calls
# FontsEngine::render(), so dead-code elimination strips it from wasm.
set(MSDFGEN_DIR ${CMAKE_CURRENT_LIST_DIR}/thirdparty/msdfgen/msdfgen-1.4)
add_library(msdfgen STATIC
    ${MSDFGEN_DIR}/core/Bitmap.cpp
    ${MSDFGEN_DIR}/core/Contour.cpp
    ${MSDFGEN_DIR}/core/Shape.cpp
    ${MSDFGEN_DIR}/core/SignedDistance.cpp
    ${MSDFGEN_DIR}/core/Vector2.cpp
    ${MSDFGEN_DIR}/core/edge-segments.cpp
    ${MSDFGEN_DIR}/core/equation-solver.cpp
    ${MSDFGEN_DIR}/core/msdfgen.cpp
    ${MSDFGEN_DIR}/ext/import-font.cpp
)
target_include_directories(msdfgen PUBLIC ${MSDFGEN_DIR})
target_include_directories(msdfgen PRIVATE ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(msdfgen PUBLIC ${FREETYPE_LIBRARIES})

include_directories(${MU_ROOT}/thirdparty/dtl)
set(ENGRAVING_NO_INTERNAL ON)
set(ENGRAVING_NO_ACCESSIBILITY ON)
add_subdirectory(${MU_ROOT}/src/engraving engraving)

# The internal draw sources, unmodified from the submodule, compiled without
# the Qt defines. fontsengine.{h,cpp} are shadow copies (glyphRuns addition
# for the SVG writer) and keep the forced prelude header — see
# src/shadow/fontsengine_prelude.h and src/shadow/README.md.
set_source_files_properties(src/shadow/fontsengine.cpp
    PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/src/shadow/fontsengine_prelude.h")

set(FONTSTACK_SRC
    src/shadow/fontsengine.cpp
    ${MU_ROOT}/src/framework/draw/internal/fontfaceft.cpp
    ${MU_ROOT}/src/framework/draw/internal/fontfacedu.cpp
    ${MU_ROOT}/src/framework/draw/internal/fontsdatabase.cpp
    src/shadow/fontprovider.cpp
)

add_library(sve_engine STATIC
    src/api/scoreloader.cpp
    src/config/engravinginit.cpp
    src/shadow/engravingfontsprovider.cpp
    src/shadow/midifile.cpp
    src/shadow/exportmidi.cpp
    src/positions/positionswriter.cpp
    src/meta/scoremeta.cpp
    src/svg/drawdatasvg.cpp
    src/svg/svgwriter.cpp
    ${MU_ROOT}/src/engraving/internal/engravingfont.cpp
    ${FONTSTACK_SRC}
)
target_include_directories(sve_engine PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${MU_ROOT}                            # thirdparty/intervaltree (spannermap.h)
    ${MU_ROOT}/src
    ${MU_ROOT}/src/framework
    ${MU_ROOT}/src/framework/global
    ${MU_ROOT}/src/framework/draw
    ${MU_ROOT}/src/framework/draw/internal
    ${MU_ROOT}/src/engraving
    ${MU_ROOT}/src/engraving/internal
    ${MU_ROOT}/src/engraving/infrastructure   # smufl.h from engravingfont.cpp
    ${CMAKE_BINARY_DIR}                  # muse_framework_config.h
    ${FREETYPE_INCLUDE_DIRS}
    ${HARFBUZZ_INCLUDE_DIRS}
)
target_link_libraries(sve_engine PUBLIC
    engraving
    muse::global
    muse::draw
    msdfgen
    ${HARFBUZZ_LIBRARIES}
    ${FREETYPE_LIBRARIES}
)

if(NOT EMSCRIPTEN)
    add_executable(mscz2media tools/mscz2media/main.cpp)
    target_link_libraries(mscz2media sve_engine)
endif()

if(EMSCRIPTEN)
    # The wasm module. JS glue contract: MODULARIZE/EXPORT_ES6 default export
    # consumed by web-public/src/helper.js, C ABI + WasmRes layout consumed by
    # the wrapper, the preloaded package at /resources read by
    # sve::initEngraving. Output: scoreview.lib.js + .wasm + .data.
    set(CMAKE_EXECUTABLE_SUFFIX ".lib.js")

    add_executable(scoreview
        web/main.cpp
        web/wasmres.cpp
    )
    target_link_libraries(scoreview sve_engine)
    # SetupBuildEnvironment points CMAKE_RUNTIME_OUTPUT_DIRECTORY into the
    # submodule tree for wasm; keep the artifacts in the build tree instead.
    set_target_properties(scoreview PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

    set(WASM_LINK_FLAGS "--no-entry")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s WASM=1")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s DYNAMIC_EXECUTION=0")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s EXPORT_ES6=1 -s USE_ES6_IMPORT_META=1 -s MODULARIZE=1")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s NODEJS_CATCH_EXIT=0 -s NODEJS_CATCH_REJECTION=0")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s ALLOW_TABLE_GROWTH=1")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s INITIAL_MEMORY=32MB")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s EXPORTED_FUNCTIONS='[\"_malloc\", \"_free\"]'")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\", \"stringToUTF8\", \"UTF8ToString\", \"getValue\", \"setValue\", \"HEAPU8\", \"FS_createDataFile\"]'")
    # zlib for the mscz container (muse_global expects it from the linker
    # under emcc), LZ4 for the preloaded package.
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s USE_ZLIB=1")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} -s LZ4=1")
    set(WASM_LINK_FLAGS "${WASM_LINK_FLAGS} --preload-file ${CMAKE_CURRENT_LIST_DIR}/resources@/resources")
    set_target_properties(scoreview PROPERTIES LINK_FLAGS ${WASM_LINK_FLAGS})
endif()
