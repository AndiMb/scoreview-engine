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

## Drift guard

`upstream.lock` pins the git blob id of every upstream counterpart (shadow
copies and prelude targets). `tools/check-shadow-drift.sh`
verifies the pins against the submodule and fails when upstream drifts — CI
runs it on every build. When it fires: re-derive the shadow copy / re-check
the prelude against the new upstream file, then update the lock with the new
blob id (`git -C musescore rev-parse HEAD:<path>`).
