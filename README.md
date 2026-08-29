# scoreview-engine

Qt-free WebAssembly build of MuseScore's engraving engine — the conversion
engine for [ScoreView](https://github.com/AndiMb/scoreview). Takes `.mscz`
files and produces SVG pages, MIDI, playback positions (spos/mpos) and
metadata JSON. No Qt, no audio synthesis, no UI.

**Status: Phase 3.** This repository implements the plan in
`webmscore-fork/docs/qtfree-plan.md`. Phase 2 (native library + `mscz2media`
CLI, corpus-gated against the released Qt webmscore) is accepted; the
DrawData→SVG writer is in place — text as glyph outlines (`<defs>`/`<use>`),
identity stamping (`class="Type seg-N st-N vc-N"`) compatible with the
webmscore fork's SVG output.

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
    │   └── api/              # load/convert API, wasm bindings
    ├── resources/            # fonts + SMuFL metadata (replaces the qrc)
    ├── web/                  # JS wrapper (webmscore-compatible subset, no audio)
    └── tools/mscz2media/     # native CLI, same outputs as the wasm build

* **Toolchain:** Emscripten only (wasm) / plain g++ (native CLI). FreeType,
  HarfBuzz and zlib as in the spike.
* **API:** webmscore-compatible for the supported subset (SVG pages, MIDI,
  spos/mpos, metadata, `destroy()`). Audio, PNG/PDF and MusicXML methods
  throw a clear "not supported in this build" error.
* **SVG text:** glyph outlines as `<path>` — self-contained SVG, no font
  delivery to browsers.

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

## Relation to webmscore

Until switchover, [AndiMb/webmscore](https://github.com/AndiMb/webmscore)
(the Qt line) remains the released, corpus-guarded truth and the baseline
this pipeline is measured against. The acceptance gate for this engine is
the corpus job: same tolerances, page counts equal unless deliberately
rebaselined.

The gate runs in CI: `tools/corpus-native.py` fingerprints every
`musescore/vtest/scores` conversion (pages, measures, positions counts,
MIDI bytes, stable metadata; with `--with-svg` also SVG generation and
page-1 sizes) and `tools/corpus-compare.py` diffs against
`testdata/corpus-baseline.json` — fingerprints of the released Qt
webmscore (v4.7.4-scoreview.7), regenerable with the fork's
`web-example/corpus.cjs`. Only the metadata `tracks` count may differ
(no audio here, the list is always empty).

SVG fidelity is checked with `tools/svg-spotcheck.py`: same-named SVGs
from the Qt release (via `saveSvg` in Node) and from `mscz2media --svg`
are compared by viewBox and by pixels, both rendered with the same
headless Chromium behind a width-normalizing shell (the generators
declare physical size differently: Qt in mm — which this writer also
emits — but encode glyphs differently, so the picture is the contract).
A light blur forgives the sub-pixel kerning shifts of HarfBuzz vs Qt;
structural defects land far above the thresholds.

## License

GPL-3.0 — see [LICENSE.txt](LICENSE.txt). Contains code from
[MuseScore](https://github.com/musescore/MuseScore) (via submodule and
shadow copies).
