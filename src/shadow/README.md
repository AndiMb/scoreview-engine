# Shadow-compiled upstream sources

The MuseScore submodule is never patched. Where an upstream file cannot be
used as-is, one of two mechanisms applies:

1. **Forced prelude** — the file is compiled unmodified from the submodule,
   with a `-include` header supplying symbols the never-compiled branch lost
   (`fontsengine_prelude.h` for `src/framework/draw/internal/fontsengine.cpp`).
2. **Shadow copy** — the file is copied here with a minimal, marked diff
   (`/* shadow diff: ... */` comments in the copy).

Current shadow copies:

| File here | Upstream counterpart | Diff |
|---|---|---|
| `engravingfontsprovider.cpp` | `src/engraving/internal/engravingfontsprovider.cpp` | one line: `dirpath(filePath)` instead of `dirpath(filePath.toQString())` |
| `midifile.h` / `midifile.cpp` | `src/importexport/midi/internal/midishared/midifile.{h,cpp}` | `QIODevice` → `muse::io::IODevice`, `qint64` → `int64_t`, `putChar` → `put`; the MIDI *import* path is not carried over; supplies Qt's `uchar` typedef for `midievent.h` (included unmodified, pinned) |
| `exportmidi.h` / `exportmidi.cpp` | `src/importexport/midi/internal/midiexport/exportmidi.{h,cpp}` | `QIODevice` → `muse::io::IODevice`, `QString`/`QFile` overloads dropped, `qPrintable` → `muse::String` |
| `fontsengine.h` / `fontsengine.cpp` | `src/framework/draw/internal/fontsengine.{h,cpp}` | added `GlyphRun`/`glyphRuns()` — render()'s loop yielding glyph identities and pen positions instead of SDF bitmaps, for the SVG writer; still compiled with the forced prelude |
| `fontprovider.cpp` | `src/framework/draw/internal/fontprovider.cpp` | every Font rescaled ×(1200/360) before reaching the FontsEngine — the metrics-side twin of `Painter::applyFontSizeScaling`; without it text is measured 10/3 too narrow (the Duckwerk page-count class) |

FreeType used to be a third case here — no copy, but the same guard on
`src/framework/draw/thirdparty/freetype/CMakeLists.txt`, whose paths carry the
FreeType version and so made a bump visible. That pin is gone as of
2026-08-30: FreeType is vendored in `thirdparty/freetype` and the submodule's
copy is not built at all, so there is nothing upstream left to drift against.
Its version is now watched where the other dependencies are watched —
`tools/deps.json` and `tools/check-deps.py`, see `docs/dependencies.md`.

## Copied data files

`resources/` stands in for MuseScore's qrc tree, and part of it is plain
upstream data copied verbatim (only CRLF normalized to LF): the instrument
templates, the chord description files, and the per-era style defaults
MuseScore applies to scores written before 4.0. Nothing compiles them, so a
compiler never notices when upstream edits one — a stale copy would simply
engrave old scores by yesterday's rules, silently. They are therefore pinned
the same way the shadow copies are.

## Drift guard

`upstream.lock` pins the git blob id of every upstream file this repository
carries a copy of — shadow copies, prelude targets, and the data files under
`resources/`. `tools/check-shadow-drift.sh` verifies it, and CI runs that on
every build.

A line is

    <blob id>  <path in the submodule>  [<verbatim copy in this tree>]

**Two fields** pin the upstream side only. That is all a shadow copy or a
prelude target can be checked for: one carries a marked diff, the other has no
copy in this tree at all, so neither can be compared byte for byte against
upstream.

**Three fields** say the copy is upstream's bytes with CRLF normalized to LF
and nothing else — the data files under `resources/`. Those are compared in
both directions: the pin against the submodule, *and* the file in this tree
against the pinned blob. The second half matters because nothing else in the
build ever looks at those files. No compiler reads them, so an edited or
badly merged `resources/engraving/...` would have passed the drift guard, the
dependency check and the corpus gate alike, and only shown up as a score from
2018 being engraved by today's rules.

When the guard fires it says which half:

* `SHADOW DRIFT` — upstream moved. Re-derive the shadow copy, re-check the
  prelude, or re-copy the data file against the new upstream, then update the
  lock with the new blob id (`git -C musescore rev-parse HEAD:<path>`).
* `COPY DRIFT` — upstream did not move, so this copy was edited. Re-copy it,
  or, if the difference is deliberate, make it a real shadow copy: move it
  under `src/shadow/`, mark the diff, and drop the third field.
