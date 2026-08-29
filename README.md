# scoreview-engine

Qt-free WebAssembly build of MuseScore's engraving engine — the conversion
engine for [ScoreView](https://github.com/AndiMb/scoreview). Takes `.mscz`
files and produces SVG pages, MIDI, playback positions (spos/mpos) and
metadata JSON. No Qt, no audio synthesis, no UI.

Releases are npm tarballs attached to GitHub Releases (`v4.7.4-engine.N`),
API-compatible with [webmscore](https://github.com/AndiMb/webmscore) for the
surface they support; ScoreView's server-side `local` conversion backend runs
on them. Every release comes out of a CI run whose corpus gate passed: all
569 vtest scores converted natively and under Node, fingerprinted against the
released Qt webmscore.

## Architecture

MuseScore is a **pinned, unpatched submodule**. Where an upstream file cannot
be used as-is, it is never patched in the submodule — it is either compiled
with a forced prelude header, or shadow-copied into `src/shadow/` with a
documented diff. CI diffs every shadow copy against the submodule and fails
when upstream drifts.

    ├── musescore/            # submodule, pinned to an upstream release tag (v4.7.4)
    ├── src/
    │   ├── platform/         # IFileSystem (native + wasm), resource path mapping
    │   ├── config/           # IEngravingConfiguration, fonts init
    │   ├── shadow/           # shadow-compiled upstream sources (diff-guarded)
    │   ├── svg/              # DrawData -> SVG writer
    │   ├── positions/        # spos/mpos writer from layout data
    │   ├── meta/             # metadata JSON writer
    │   └── api/              # the one score loader both builds use
    ├── resources/            # fonts (woff2) + SMuFL metadata (replaces the qrc)
    ├── web/                  # wasm entry (C ABI + WasmRes wire format)
    ├── web-public/           # JS wrapper (webmscore-compatible subset, no audio)
    ├── testdata/             # corpus baseline and waivers, one test score
    └── tools/                # mscz2media CLI, build scripts, corpus gates

* **Toolchain:** Emscripten only (wasm) / plain g++ (native CLI). FreeType,
  HarfBuzz and zlib, no Qt anywhere.
* **Fonts:** woff2, read by FreeType with the brotli decoder vendored in
  `thirdparty/brotli` — MuseScore's own FreeType wrapper disables brotli, so
  the top-level CMakeLists configures FreeType itself (see
  `src/shadow/README.md`). The same decoder is what lets a caller hand
  `addFont()` a woff2 file.
* **API:** webmscore-compatible for the supported subset (SVG pages, MIDI,
  spos/mpos, metadata, extra fonts, `destroy()`). Audio, PNG/PDF and MusicXML
  methods throw a clear "not supported in this build" error.
* **SVG text:** glyph outlines as `<path>` — self-contained SVG, no font
  delivery to browsers.

## From `.mscz` to SVG

The path through this engine:

    .mscz → sve::loadScore  (zip → MSCX → MasterScore, doLayout, Score::update)
          → ScoreRenderer::paintItem per element, into an SvgPaintProvider
          → DrawData (paths, polygons, glyph runs + pen/brush/transform state)
          → DrawDataSvg::toSvg → page-N.svg

Native (`mscz2media --svg`) and wasm (`saveSvg()` → `web/main.cpp`) enter that
chain at the same function, `sve::SvgWriter::write`, which is why their bytes
are comparable at all. Against webmscore — MuseScore's full Qt stack compiled
to wasm — the same score takes this route:

| Stage | webmscore (Qt line) | scoreview-engine |
|---|---|---|
| Container → score | `MscReader` (zip) → `MasterScore`, engraving code | the same engraving code from the unpatched submodule, behind `sve::loadScore` |
| Fonts | Qt: `QFontDatabase`/`QRawFont`, needing a QPA plugin (a grafted offscreen build) to exist at all | `FontsEngine`/`FontFaceFT` — FreeType + HarfBuzz, the `MUSE_MODULE_DRAW_USE_QTTEXTDRAW=OFF` path no upstream build compiles |
| Text measurement | Qt's font engine; its wasm build reports wrong vertical metrics, which the fork patches | HarfBuzz advances, FreeType `hhea` metrics; shadow `fontprovider.cpp` adds the engraving-DPI rescale (×1200/360) the drawing side applies |
| Layout | `doLayout()` per score, then `Score::update()` | identical, same sequence — one loader for both builds |
| Painting a page | `Painter` → `QPainterProvider` → `QPainter` on `SvgGenerator`, a `QPaintDevice` that streams SVG while painting | `Painter` → `SvgPaintProvider`, which *records* the page into a `DrawData` buffer; serialization is a separate, Qt-free step |
| Element identity | the writer sets the current `EngravingItem` on the paint engine before each item | `beginObject(classTag(e))` around each item → `<g class="Chord seg-42 st-0 vc-0">`, same class strings, `seg-N` the id the position export hands the player |
| Glyphs | Qt's paint-engine text emulation converts every glyph to an inline `<path>`, repeated at every occurrence | `FontsEngine::glyphRuns()` yields glyph ids and pen positions; each distinct glyph is emitted once as a `<path>` in `<defs>` and placed with `<use>` |
| Page geometry | `width`/`height` in mm, `viewBox` in engraving units (DPI 1200) | the same numbers — the pages are drop-in replacements |
| Page-1 SVG size | baseline | 58 % of it over the corpus (glyph deduplication) |
| Title | file name (a random temp name in webmscore) | title text / `workTitle`, or no `<title>` — deterministic across builds |

Up to the paint call both run the same upstream engraving code — what
differs is which font stack measures the text; from the paint call on, the
right column is this repository's own code. The residual divergence comes
from the first of the two: on two corpus scores a sub-percent
HarfBuzz-vs-Qt advance difference flips a tight system break and costs a
page (`testdata/corpus-waivers.json`).

## Building (native, Docker)

    git clone --recurse-submodules <this repo>   # submodule is shallow, clones fast
    tools/build-native-docker.sh                 # ubuntu-22.04/g++-10, build tree in volume sve-build

    # convert the checked-in test score:
    docker run --rm -v "$(pwd):/src:ro" -v sve-build:/build scoreview-engine-build \
        /build/mscz2media /src/testdata/repeat-test.mscz --resources /src/resources \
        --out /build/out --svg --midi --spos --mpos --meta

`prefetch/` carries the HarfBuzz source archive from muse_deps because the
download host resolves IPv6-only and fails inside containers. `resources/`
replaces MuseScore's qrc tree (fonts, SMuFL metadata, styles); see
`src/shadow/README.md` for the shadow rule and the drift guard
(`tools/check-shadow-drift.sh`).

## Building (wasm + JS wrapper)

    tools/build-wasm-docker.sh    # emscripten/emsdk:4.0.7, volume sve-build-wasm
    cd web-public && npm ci && npm run build

Outputs `scoreview.lib.js` / `.wasm` / `.data` (preloaded resources,
LZ4-compressed) plus the bundles `scoreview.nodejs.cjs` (Node),
`scoreview.mjs`/`scoreview.js` (browser, worker-capable) — 9.3 MB of wasm and
a 4.8 MB resource pack, against the Qt build's 17.5 MB of wasm, and the full
corpus converts under Node in 7.0 s against 20.2 s. The npm tarball is 7.0 MB.
Usage is webmscore's for the supported surface:

```js
const WebMscore = require('./web-public/scoreview.nodejs.cjs')
await WebMscore.ready
const score = await WebMscore.load('mscz', data, [], true)
await score.saveSvg(0)      // SVG page, text as glyph outlines
await score.saveMidi()      // repeats expanded, RPNs exported
await score.savePositions(true /* segments */)
await score.saveMetadata()
score.destroy()
```

The `fonts` argument of `load()` (webmscore's CJK path) registers extra fonts
as text-substitution fallbacks for glyphs the score fonts lack — woff2, otf,
ttf and ttc alike. Audio,
soundfonts, PNG/PDF, MusicXML, MSCZ writing and excerpts throw a
`NotSupportedError`. `web-example/browser.html` is a smoke test for both
browser bundles.

## Fidelity gates

The reference is [AndiMb/webmscore](https://github.com/AndiMb/webmscore),
the Qt line: the baseline this pipeline is measured against, score for score.

`tools/corpus-native.py` fingerprints every `musescore/vtest/scores`
conversion (pages, measures, positions counts, MIDI bytes, stable metadata;
with `--with-svg` also SVG generation and page-1 sizes) and
`tools/corpus-compare.py` diffs it against `testdata/corpus-baseline.json` —
fingerprints of the released Qt webmscore (v4.7.4-scoreview.7), regenerable
with the fork's `web-example/corpus.cjs`. Only the metadata `tracks` count
may differ (no audio here, the list is always empty); deliberate deviations
are recorded per score and field in `testdata/corpus-waivers.json`, and a
waiver that stops firing is itself a failure.

`tools/corpus-node.cjs` repeats the run with the wasm build under Node and
must match the native report exactly, SVG bytes included (`--exact-svg`) —
both go through `sve::loadScore` and the same writers, so any difference is
a wasm-side bug. The one tolerated class is libm float noise (glibc vs musl
round transcendentals differently, slur/ornament layout amplifies the last
ulp into micro coordinate shifts): 11 scores, waived in
`testdata/corpus-wasm-waivers.json`.

`tools/svg-spotcheck.py` compares the pictures: same-named SVGs from the Qt
release (via `saveSvg` in Node) and from `mscz2media --svg`, by viewBox and
by pixels, both rendered with the same headless Chromium behind a
width-normalizing shell. The generators encode glyphs differently, so
nothing textual beyond the viewBox is compared; a light blur forgives the
sub-pixel kerning shifts of HarfBuzz vs Qt, and structural defects land far
above the thresholds.

CI runs the native gate, the wasm gate and the shadow drift guard on every
build; a push to `main` whose commit message starts with `release v` attaches
that run's tarball to a GitHub Release.

## License

GPL-3.0 — see [LICENSE.txt](LICENSE.txt). Contains code from
[MuseScore](https://github.com/musescore/MuseScore) (via submodule and
shadow copies).
