# scoreview-engine

Qt-free WebAssembly build of MuseScore's engraving engine — the conversion
engine for [ScoreView](https://github.com/AndiMb/scoreview). Takes `.mscz`
files and produces SVG pages, MIDI, playback positions (spos/mpos) and
metadata JSON. No Qt, no audio synthesis, no UI.

**Status: scaffold.** This repository implements the plan in
`webmscore-fork/docs/qtfree-plan.md`; currently Phase 2 (native Qt-free
library + `mscz2media` CLI) is being productized from the 2026-08-28 spike.

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

## Relation to webmscore

Until switchover, [AndiMb/webmscore](https://github.com/AndiMb/webmscore)
(the Qt line) remains the released, corpus-guarded truth and the baseline
this pipeline is measured against. The acceptance gate for this engine is
the corpus job: same tolerances, page counts equal unless deliberately
rebaselined.

## License

GPL-3.0 — see [LICENSE.txt](LICENSE.txt). Contains code from
[MuseScore](https://github.com/musescore/MuseScore) (via submodule and
shadow copies).
