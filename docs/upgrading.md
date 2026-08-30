# Upgrading

> **Historical.** This is the operating manual of the Qt fork
> `AndiMb/webmscore`, which was retired on 2026-08-30 and whose repository —
> like `AndiMb/qt-libqoffscreen-wasm` — no longer exists. Nothing here
> describes scoreview-engine, whose MuseScore bumps run through the pinned
> submodule and the corpus gate instead. It is kept because it records what a
> Qt/Emscripten/MuseScore move actually costs, and because the Qt line is the
> fidelity reference this engine is measured against.

Three things can move independently: the offscreen QPA plugin, the Qt and
Emscripten pair, and the MuseScore version underneath. Move one at a time — a
failing wasm build is bisected through CI rounds, and two variables at once
double them.

Whatever moves, the acceptance test is the same: the `corpus` job against the
last release, described in [the README](../README.md#checking-a-build-against-a-released-one).
Same page counts, same fingerprints, same audio.

## What a rebase has to carry

The fork's patch set is roughly 80 files inside MuseScore's tree. Most of it is
mechanical; two areas are not.

**Prefer a seam to a patch.** Where MuseScore resolves something through the IoC
container, the web build can register its own implementation and leave the file
alone — a patch that does not exist cannot conflict. Two are in place:
`web/webfilesystem.h` subclasses `muse::io::FileSystem` to map `:/…` resource
paths onto the preloaded file system, which took eight patched files before, one
of them listing every bundled font; and `main.cpp` registers it in place of the
upstream class. `EngravingConfiguration` and `NotationConfiguration` are the
obvious next candidates — both are patched only to answer questions a user
interface would answer, both are non-`final`, and every method involved is
virtual.

**The audio module.** From 4.7, MuseScore treats WebAssembly as a thin facade
inside a surrounding application — one that supplies a Web Worker, a JS
counterpart and a clock. As a library there is none of that, so eleven places
take the opposite decision from upstream: the audio facade mode, the audio
driver, the soundfont repository, the file system, starting the engine, the
engine's lifecycle, the RPC channel, *which side of that channel a handler
belongs to*, the wait loops in the audio writer, how long to wait before
playback is ready, and which notation the audio export hands to the playback
controller. Each is argued in the commit that made it, and the last four are the
subtle ones: with a single thread the channel's "am I the main side?" flag is
true for both sides, so it decides not only where traffic goes but where a
handler is *filed*.

**`src/framework`.** 40 of the patched files live there, and upstream has
extracted that directory into its own repository (`musescore/muse_framework`,
carried as the `muse/` submodule). A fork of the MuseScore repository cannot
carry those patches across that split — see [If the framework split lands](#if-the-framework-split-lands).

Adapting one class for the web build takes three edits, not one: the use site,
the header it includes, and the CMake source list. Missing any of the three
fails later and further away than expected. The same shape appears in guards — a
function that registers *and* unregisters needs both halves guarded, and only
the second call reveals that you forgot.

## Bumping Qt and Emscripten

Qt for WebAssembly ships a single QPA platform, `wasm`, and it needs a canvas.
The offscreen plugin is grafted in instead, and it is ABI-bound to exactly the
Qt and Emscripten pair it was built with. So the chain is:

    libqoffscreen.a  ->  Qt  ->  the Emscripten version Qt documents for it

Nothing in MuseScore's source pins the Qt version, and the fork no longer carries
accommodations for an older one. Four files used to hold `QT_VERSION_CHECK`
guards from the detour through Qt 6.4.3 — `draw/types/font.cpp`
(`PreferTypoLineMetrics`, 6.8), `global/io/internal/filesystem.cpp`
(`QDirListing` vs `QDirIterator`, 6.8), `global/api/internal/apiregister.cpp`
(capturing lambdas in `qmlRegisterSingletonType`, 6.5) and
`importexport/imagesexport/internal/pdfwriter.cpp` (`QPdfWriter::ColorModel`,
6.8). All four took their modern branch and are gone; the first three are
verbatim upstream again. Should a Qt bump ever need a guard back, it belongs to
that bump and comes out with the next one — a dead branch for a Qt nobody builds
against is patch surface that conflicts for free.

**Steps.**

1. **Build the plugin.** Push a tag `vX.Y.Z` to `AndiMb/qt-libqoffscreen-wasm`
   (deleted 2026-08-30; the last build, `v6.10.2`, is kept as
   `libqoffscreen.a`/`.prl` beside the checkouts).
   Its workflow checks out `qt/qtbase` at that tag, picks the Emscripten version
   Qt documents for it, patches two files, configures `-qpa offscreen`, and
   releases `libqoffscreen.a` and `.prl` — about seven minutes.
2. **Swap it in.** Both files go to `buildscripts/qt/plugins/platforms/`, and the
   version strings in the six `Qt6QOffscreenIntegrationPlugin*.cmake` files under
   `buildscripts/qt/lib/cmake/Qt6Gui/` have to match. The import glue needs
   nothing — the workflow compiles `QOffscreenIntegrationPlugin_init.cpp` with
   the same `em++` that builds everything else.
3. **Bump the workflow.** `Qt6_VER` and `EM_VERSION` in
   `.github/workflows/build.yml`, and `CMAKE_PREFIX_PATH` in `web/Makefile`.

**What to check afterwards, in this order** — each only becomes visible once the
one before it is fixed, and none of them shows at link time:

* **Link flags** in `web/CMakeLists.txt`. Emscripten renames and drops settings
  between majors, and an unknown `-s NAME` is not always an error.
* **The `emcc.py` patch** in the workflow forces `MEM_INIT_IN_WASM` off with
  `sed`. If the setting is gone the `sed` matches nothing and passes silently,
  and the `.mem` shuffling in `web-public/package.json` is conditional — so this
  degrades quietly rather than failing loudly.
* **Module members.** Emscripten only attaches what it is asked for.
  `_malloc`/`_free` are in `EXPORTED_FUNCTIONS`, `HEAPU8` in
  `EXPORTED_RUNTIME_METHODS`; anything the JS wrapper reaches for has to be
  named there.
* **The DOM shim** in `web-public/src/utils.js`. Qt's wasm event dispatcher
  schedules timers through `window.setTimeout`, which Node has no `window` for.
  The shimmed timers are `unref`'d, so a pending Qt timer cannot hold the
  process open after the caller is done.
* **The CommonJS bundle's `import.meta.url`**, which Emscripten 4 passes to
  `createRequire`.

Do this by fetching the `npm-pack` artifact and running it under Node locally —
seconds per round, against an afternoon of CI.

## Bumping MuseScore

Same acceptance test, plus the fork's own audio and position outputs, which the
corpus fingerprint covers.

Iterate natively against the same Qt rather than in the wasm CI: the same
compile errors surface in minutes instead of in hour-long builds. Two things
used to stand in the way of that and no longer do — the root `CMakeLists.txt`
had been deleted (incidentally, in `d7d31b8bd5`, not as a decision), which left
`build.cmake`, `ninja_build.sh` and `CMakePresets.json` with nothing to
configure; and `buildscripts/cmake/SetupQt6.cmake` commented the web build's
trimmed component list in for *every* configuration instead of branching on
`OS_IS_WASM`. Both are fixed, so MuseScore's own entry points work again —
`./ninja_build.sh` on Linux and macOS, `CMakePresets.json` on Windows. Nothing
else in the fork builds natively; `web/` stays emscripten-only. What this buys
is the compile errors, which are the same ones.

Read the symbol map rather than guessing — the build emits
`webmscore.lib.symbols`, which maps the `wasm-function[N]` frames in a
JavaScript stack trace straight to C++ names
([README, Debugging](../README.md#debugging)).

### If the framework split lands

The implementation plan for this exit, de-risked by the glyph-path spike of
2026-08-28, is [qtfree-plan.md](qtfree-plan.md). The section below states the
problem and the constraints; the plan states the phases.

When this fork next rebases onto a MuseScore that has completed the
`muse_framework` extraction, the 33 patched files under `src/framework` are no
longer in the repository being forked, and carrying them means maintaining a
second fork.

The way out is to stop being invasive: MuseScore enters as a submodule, our code
sits beside it in its own directory, the way `tools/check_build_without_qt`
already consumes the tree — and Qt is dropped along with it. The pieces exist:
`MscReader`/`MscLoader` read `.mscz` through `muse::io` rather than `QFile`;
`BufferedPaintProvider` records draw commands into a `DrawData` buffer with no
Qt; `fontsengine`/`fontfaceft` carry FreeType branches under
`#ifndef MUSE_MODULE_DRAW_USE_QTTEXTDRAW`. What would be ours to write is the
`DrawData` → SVG writer, and audio would be dropped (ScoreView synthesises in
the browser from the MIDI).

Two upstream switches stand in the way, and both conflate "no Qt" with "no
implementation":

* `DRAW_NO_INTERNAL` removes the Qt painter providers *and* the FreeType font
  engine in one `else()` branch, so the Qt-free build has no fonts at all. And
  `MUSE_MODULE_DRAW_USE_QTTEXTDRAW` is defined unconditionally whenever the font
  engine is built, so the FreeType branches are compiled by no upstream
  configuration — the Qt-free glyph path is untested code, and reviving it is
  the one risk that can sink this plan.
* `ENGRAVING_NO_INTERNAL` drops `engravingconfiguration.cpp` — which does use
  `QLocale`/`QPageSize` — together with `engravingfont.cpp` and
  `engravingfontsprovider.cpp`, which contain no Qt at all. Without it there is
  no implementation of `IEngravingConfiguration`, an interface with 59 pure
  virtual methods including the ones layout reads for spatium, page size and
  style defaults.

Both are two `else()` branches in CMake and both land in `src/framework`, so
under the no-upstream-PR rule they would be patches in a second repository.
The spike found a way that needs neither: build with the `NO_INTERNAL`
configurations as upstream ships them and compile the handful of internal
sources from outside the tree — a forced prelude header where symbols went
missing, a diff-guarded shadow copy where a line must change. No fork of
either repository. The mechanics are in
[qtfree-plan.md](qtfree-plan.md#target-architecture).
