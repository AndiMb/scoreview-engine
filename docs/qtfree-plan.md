# Plan: Qt-free webmscore on a MuseScore submodule

Status: **done** — Phases 0 and 2 through 6 were accepted on 2026-08-29
(Phase 1 skipped), and ScoreView's `local` backend runs on the engine.
Phase 7 followed on 2026-08-30, harder than written below: instead of
freezing and archiving, `AndiMb/webmscore` and `AndiMb/qt-libqoffscreen-wasm`
were deleted — before the MuseScore bump this plan made the precondition.
De-risked by the glyph-path spike of 2026-08-28 (also deleted; its findings
are summarized below).
This document is the implementation plan for what
[upgrading.md](upgrading.md#if-the-framework-split-lands) sketches as the way
out of the fork model.

## Why, and when

Three pressures point at the same exit:

* **The fork model has an expiry date.** 40 of the 83 patched files live in
  `src/framework`, which upstream has extracted into its own repository
  (`muse_framework`). The next MuseScore bump beyond 4.7.x cannot carry those
  patches without maintaining a second fork.
* **The Qt toolchain is most of the operational complexity.** Qt-for-wasm,
  the grafted offscreen QPA plugin (its own repository,
  `AndiMb/qt-libqoffscreen-wasm`), aqtinstall, the `emcc.py` sed patch — all
  of it exists only to run Qt where no UI is wanted.
* **ScoreView needs less than webmscore offers.** The consumer contract is
  the sidecar contract: SVG pages, MIDI, spos/mpos, metadata. No audio (the
  browser synthesizes from MIDI), no PNG/PDF. About 30 of the 83 patched
  files serve audio alone — the most invasive part of the patch set.

**Timing.** Phases 1–4 are executable now — 4.7.4 is the newest upstream tag,
so nothing is racing us. The switchover (phases 5–7) becomes *necessary* with
the first MuseScore bump past the framework split; doing phases 2–4 early
means that bump lands on a plan instead of a wall.

## What the spike established (2026-08-28)

The one risk upgrading.md called existential — "the Qt-free glyph path is
untested code, and reviving it is the one risk that can sink this plan" — is
retired:

* **The path compiles and works.** `fontsengine`/`fontfaceft` without
  `MUSE_MODULE_DRAW_USE_QTTEXTDRAW` build after a ~10-line prelude (four dead
  symbols, values recoverable from history commit `9a05283009`). Metrics are
  the desktop's: Edwin line spacing exactly 1.200 em (hhea) — the value
  Qt-wasm gets wrong today. HarfBuzz shapes with kerning; SMuFL glyphs
  resolve by name; outlines and SDFs carry real content.
* **A page renders without Qt.** `.mscz` → read → layout → page 1 into a
  `DrawData` buffer, with Leland symbol runs and Edwin text runs recorded —
  the exact input a `DrawData` → SVG writer consumes. No Qt symbol in the
  binary.
* **Fidelity is not free.** Page counts against the desktop and against the
  current (Qt) webmscore: repeat-test 1=1, What_Was_I_Made_For 5=5=5,
  **Duckwerk 3 vs 4** — the text-heaviest score diverges, with complete fonts
  and faithful configuration values. Horizontal text measurement
  (HarfBuzz/FreeType vs Qt) shifts system distribution. The corpus job is the
  acceptance instance, and metric tuning on text-heavy scores is budgeted
  work, not a surprise.

Revival costs measured by the spike (full list in the spike's README):
the fontsengine prelude; msdfgen (removed upstream 2024-03-29 in
`c7cf38d1f6`, and it is a *modified* 1.4 — stock does not fit; restorable
from history); no Qt-free `IFileSystem` in `muse::io` (~50 lines to supply);
`IEngravingConfiguration` needs an own implementation (60 methods, almost all
constants); one gratuitous `toQString()` in `engravingfontsprovider.cpp`;
`version.cmake` must precede `MuseSetupConfiguration`; the qrc resources
(fonts, SMuFL metadata, two XMLs, `chords_std.xml`) served as real files.

## Target architecture

A build in which **MuseScore is a pinned submodule and nothing inside it is
patched**:

    <repo>
    ├── musescore/            # submodule, pinned to an upstream release tag
    ├── src/
    │   ├── platform/         # IFileSystem (native + wasm), resource path mapping
    │   ├── config/           # IEngravingConfiguration, fonts init (was EngravingModule::onInit)
    │   ├── shadow/           # shadow-compiled upstream sources (see rule below)
    │   ├── svg/              # DrawData -> SVG writer (ours)
    │   ├── positions/        # spos/mpos writer from layout data (port of the fork's savePositions)
    │   └── api/              # load/convert API, wasm bindings
    ├── resources/            # fonts + metadata replacing the qrc
    ├── web/                  # JS wrapper (trimmed: no audio, no worker glue)
    └── tools/mscz2media/     # native CLI, same outputs as the wasm build

* **Toolchain:** Emscripten only. No Qt, no QPA plugin repository, no
  aqtinstall, no `emcc.py` patch. FreeType/HarfBuzz/zlib as today (in-tree /
  muse_deps / emscripten port).
* **Outputs:** SVG pages, MIDI, spos/mpos, metadata JSON — the sidecar
  contract, nothing more. Audio, PNG, PDF, MusicXML export are dropped
  unless ScoreView grows a need.
* **The shadow rule** (the spike's key architectural finding): where an
  upstream file cannot be used as-is, it is not patched in the submodule —
  it is either compiled from the submodule with a forced prelude header
  (fontsengine.cpp), or copied into `src/shadow/` with a minimally
  documented diff (engravingfontsprovider.cpp today). **CI diffs every
  shadow copy against the submodule and fails when upstream drifts**, so
  divergence is loud instead of silent. This keeps the no-upstream-PRs rule
  satisfied with zero forks: not of MuseScore, not of muse_framework.

## Phases

Each phase is independently useful and ends in something runnable; the
acceptance test is stated with the phase. Effort figures are rough
orientation, not commitments.

### Phase 0 — Decisions (hours)

**Decided 2026-08-29:**

1. **Repository: a new repository, named `scoreview-engine`** (repo and npm
   package). Named for its role — the conversion engine for ScoreView — with
   no trademark exposure. The current `AndiMb/webmscore` carries MuseScore's
   full history (~2 GB class); a submodule-based repo clones in seconds and
   starts with clean history. The existing repo stays as the maintenance
   line until switchover.
2. **API surface: webmscore-compatible for the supported subset.** The JS
   wrapper keeps the existing webmscore method names and signatures for
   everything the new engine produces (SVG pages, MIDI, spos/mpos, metadata,
   `destroy()`), so existing calling code keeps working unchanged. Audio,
   PNG/PDF and MusicXML methods are *not* carried over — they throw a clear
   "not supported in this build" error rather than silently missing.
   (Deviates from the original recommendation of a free-form sidecar
   contract.)
3. **SVG text encoding: glyph outlines as `<path>`.** Self-contained SVG, no
   font delivery to browsers, matches how consumers already treat sidecar
   SVGs. Larger files accepted.
4. **Phase 1 (audio removal from the live fork): skipped.** 4.7.4 is the
   newest upstream tag, so no further Qt-line rebases are expected before
   the switchover; the effort would not pay for itself.

### Phase 1 — Optional now: drop audio from the live fork (1–2 days + CI)

Shrinks the *current* fork's patch set by ~30 files (framework/audio,
audioplugins, audioexport, playback) and the libsndfile submodule while the
Qt line is still the shipping line. Reduces every future rebase until the
switchover. The corpus fingerprint contains no audio, so the guard stays
intact. Skippable if the switchover comes soon anyway.

### Phase 2 — Native Qt-free library + `mscz2media` (days)

Productize the spike into `tools/mscz2media`:

* the spike's prelude, shadow copy, `IFileSystem`, `IEngravingConfiguration`,
  fonts/resources init, ScoreRW-equivalent loading — as maintained code with
  the CI drift guard;
* add the remaining outputs: MIDI export (verified 2026-08-29: `iex_midi`'s
  logic — `exportmidi.cpp` on `CompatMidiRender`/`EventsHolder` — depends
  only on engraving and is Qt-free; the Qt surface is plumbing, `QFile`/
  `QIODevice`/`QString` in the `ExportMidi` facade and `midishared/midifile`,
  so both become drift-guarded shadow copies with the byte sink swapped for
  a Qt-free one; the fork's three patches there are include-only and
  irrelevant), spos/mpos writer (port of the fork's `savePositions` fix,
  reading layout data), metadata JSON (port from `web/`);
* msdfgen: restore from history as build-only third party. Nothing in the
  conversion path calls `FontsEngine::render()`, and dead-code elimination
  strips it from wasm; revisit only if it ever shows up in size.

**Acceptance:** the corpus scores convert natively on Linux CI; spos/mpos,
MIDI and metadata are produced for all of them; repeat-test, WWIMF and
Duckwerk reproduce the spike's numbers.

**Met 2026-08-29** (repo `AndiMb/scoreview-engine`): 569 vtest scores, 567
convert, 554 fingerprints byte-identical to the released Qt webmscore —
including MIDI byte counts, after one real fix (instrument templates must be
loaded, or every instrument defaults to single-note dynamics and the MIDI
export grows CC events per note). The 15 remaining differences on 11 scores
are recorded as waivers (`testdata/corpus-waivers.json`) and are the
concrete Phase-4 worklist: 3 text-metric page counts (Duckwerk class), 4
durations, 3 small MIDI deltas (volta/slur), 2 debug-only NaN asserts.
Spike numbers reproduce: repeat-test 1, WWIMF 5, Duckwerk 3. The corpus
gate runs in the new repo's CI on every build.

### Phase 3 — DrawData → SVG writer (1–2 weeks)

The one genuinely new component. Consumes `DrawData` (paths, polygons, text
runs with font state), emits per-page SVG; text per the Phase-0 decision
(glyph outlines via FreeType `FT_Outline` → SVG path data, cached per glyph
id). Reuse `drawdatajson` for golden tests.

**Acceptance:** for the corpus, page-by-page comparison against the current
webmscore release's SVGs — structural (viewBox, element counts, coordinate
fingerprints within tolerance) plus a rendered-pixel spot check on a dozen
scores. File sizes recorded.

**Met 2026-08-29.** `tools/svg-spotcheck.py` (scoreview-engine) renders
same-named SVGs from the Qt release and from `mscz2media --svg` with the
same headless Chromium and compares viewBox numerically and pixels with a
blur-tolerant metric: 15/15 pass (13 vtest families + WWIMF + Duckwerk;
the ink-densest faithful pages measure 2.8 of 255, a deliberately broken
render measured 15). The full corpus converts 569/569 with SVG enabled —
no writer crash on any element class — and CI exercises SVG on every
build. File sizes: native page-1 SVGs total 58 % of the Qt generator's
(glyphs deduplicated via `<defs>`/`<use>`).

### Phase 4 — Fidelity loop (open-ended; the risk budget lives here)

Drive the Duckwerk class of divergence to a decision per score family:

* diff horizontal advances Qt vs HarfBuzz on the corpus's text-heavy scores;
  candidate causes: kerning defaults, fractional-advance rounding, DPI
  scaling in `pixelSizeForFont`;
* either match Qt's measurement closely enough that the corpus fingerprint
  passes, or — where the FreeType result is defensibly *more* correct (as
  with the fork's vertical-metrics fix, which exists because Qt-wasm was
  wrong) — rebaseline deliberately and record why;
* extend the corpus job to run the new pipeline against the released Qt
  webmscore, same tolerances as today (0.5 % SVG size, zero structural or
  metadata drift, page counts equal unless recorded).

**Acceptance:** corpus green under the recorded tolerance set. This gate,
not compilation, is what "done" means for the engine.

**Met 2026-08-29** (scoreview-engine `e14959b`). Every waiver was driven
to a per-family decision, with desktop MuseScore Studio 4.7.4 as ground
truth on the same scores:

* *midiBytes (slurs-30, volta-1/2) — fixed.* The native pipeline never
  ran `Score::update()` after the initial layout; desktop and webmscore
  do, and when the layout added elements that request a tempomap rebuild
  (courtesy time signatures), that pass wipes the `endTick-1` tempo
  entries `Volta::setTempo()` plants during layout. `mscz2media` now
  calls it; the exports are byte-identical to the Qt baseline.
* *meta.duration (breath-4, partials/2, stacking-order) — baseline
  wrong, waived with the verified reason.* webmscore's
  `saveMetadata().duration` reads a stale repeat-list cache
  (`Score::duration()` mixes `repeatList(true)` with the
  expandRepeats-flag-dependent `repeatList()` inside `utick2utime`);
  calling `saveMidi` first flips webmscore to the desktop values, which
  are exactly what the native engine reports.
* *pages — split verdict, waived.* bigTimeSig-3/4: desktop lays out
  2 pages like native; Qt-webmscore's 1 page is the outlier.
  vibrato-1/text-line-scaling: genuine native deviation — sub-percent
  HarfBuzz-vs-Qt glyph-advance differences flip a tight system break
  (one measure fewer per system); desktop agrees with Qt here.

Corpus standing: 569/569 convert; outside the 8 waived scores every
fingerprint matches the Qt baseline except the deliberately empty
`tracks` metadata field; gate green in CI.

### Phase 5 — wasm build + JS wrapper (days to a week)

Emscripten build of the Phase-2 library; JS wrapper trimmed from
`web-public` (no audio, no soundfont, no worker/RPC glue — and no Qt event
dispatcher, so the `window.setTimeout` shim dies too). Node and browser
entries, worker-capable as today.

**Acceptance:** the corpus run under Node matches the native build's
fingerprints exactly; bundle size and conversion speed measured against the
Qt build (expect a substantial size drop without Qt).

**Met 2026-08-29** (scoreview-engine `a086df8`, CI green with a second
wasm job). Emscripten build (emsdk 4.0.7, resources preloaded at
`/resources`, LZ4) with a webmscore-compatible C ABI in `web/main.cpp`;
the JS wrapper is webmscore's `web-public`, trimmed — everything Qt-only
throws `NotSupportedError`, Node/browser/worker bundles as before
(package name `scoreview-engine`). Both pipelines share one loader,
`sve::loadScore`. The gate forced two determinism fixes: stable_sort for
the SVG paint order (libstdc++ and libc++ permute equal-z elements
differently) and a deterministic SVG `<title>` (title text / workTitle
instead of the file name). Result: 569/569 convert under Node, every
structural field identical to the native build, 558/569 SVGs
byte-identical — the 11 others differ only in libm float noise (glibc vs
musl last-ulp in slur/ornament trig, 0.4 µm–0.03 mm), waived per score
in `testdata/corpus-wasm-waivers.json`. Numbers vs the Qt build:
9.1 MB wasm vs 17.5 MB (−48 %); full corpus under Node 7.0 s vs 20.2 s
(2.8×). Data pack still ships otf (9.3 MB vs 4.2 MB woff2) — follow-up.

### Phase 6 — CI, release, ScoreView integration (days)

Build/test/corpus workflow; release tarball (new package name, not
`webmscore4`); ScoreView browser integration behind the app's existing
HTTP-API abstraction so the frontend never learns which backend converted
(the same principle that keeps the sidecar revisable, E3).

**Acceptance:** a release exists; a ScoreView instance without a sidecar
converts and plays a score in the browser.

**Met 2026-08-29.** Releases `v4.7.4-engine.1` and `.2` (the latter adds
addFont/CJK text-substitution) are published from CI — a `release v<tag>`
commit on main attaches the corpus-gated npm tarball. ScoreView
integration (`AndiMb/scoreview` `f77db70`) turned out simpler than the
plan's browser sketch: ScoreView had meanwhile grown a second server-side
backend (`conversion_backend = local` — webmscore-wasm run by the
server's Node per conversion, same IAppData cache, frontend fully
ignorant), so the engine landed as a **drop-in swap of that npm module**
— exactly the "reiner Backend-Austausch" the app's E3 promised. Fallout
handled in ScoreView: the SVG sanitizer now admits `<use>` on local
fragments (and its allowlists actually apply — `USE_PROFILES` had been
silently overriding them), and the self-test's notehead probe reads the
engine's glyph form. Verified on a live instance with **no sidecar
container running**: local backend converts (self-test 0.4 s vs ~1 s),
the viewer renders the glyph SVGs, and playback runs (headless Chromium,
trusted input click). The plan's in-browser conversion variant remains
possible but is not needed — E3's server-decides architecture covers the
sidecar-less case with cache semantics intact.

### Phase 7 — Switchover and retirement

*As planned:* new line becomes the shipping line; `AndiMb/webmscore` frozen as
a maintenance branch with a pointer here; `qt-libqoffscreen-wasm` archived;
docs and project memory updated — only after Phase 4's gate had held for one
real MuseScore bump.

*As executed, 2026-08-30:* both repositories were **deleted** instead, and
before that bump. Kept from the Qt line: the frozen `testdata/corpus-
baseline.json` and these two documents, moved here. The release tarball
`webmscore4-4.7.4.tgz` (`v4.7.4-scoreview.7`) and the `libqoffscreen`
artifacts were held outside this repository only briefly and then deleted
too, on the same day. The consequences are deliberate: a Qt
baseline for a *future* MuseScore version can no longer be produced, so
fidelity questions are decided against the frozen baseline plus desktop
MuseScore Studio (as Phase 4 already did), and audio/PDF/PNG/MusicXML have no
implementation on either line any more.

## Risks

| Risk | Standing | Mitigation |
|---|---|---|
| Qt-free glyph path dead | **retired** by the spike | — |
| Text-metric divergence (Duckwerk class) | **root cause found and fixed 2026-08-29**: the Qt-free metrics side (FontMetrics → IFontProvider) lacked the engraving-DPI rescale the drawing side applies (`Painter::applyFontSizeScaling`) — text measured exactly 10/3 too narrow. Shadow `fontprovider.cpp` fixes it; Duckwerk lays out at 4 pages like Qt/desktop, all 569 corpus scores convert. | residual after Phase 4: two scores (vibrato-1, text-line-scaling) where a sub-percent HarfBuzz-vs-Qt advance difference flips a tight system break; waived, desktop sides with Qt |
| Shadow copies drift with upstream | structural | CI diff guard fails loudly |
| `iex_midi` not Qt-free | **retired** 2026-08-29: logic is Qt-free, only the I/O plumbing is Qt | shadow copies of `exportmidi`/`midifile` with a Qt-free byte sink |
| msdfgen bit-rots further upstream | contained | vendored from history, build-only, strippable |
| SVG writer fidelity | **retired** 2026-08-29: spot check 15/15 against the release, corpus 569/569 with SVG | svg-spotcheck.py stays the acceptance instrument; corpus CI exercises SVG on every build |

## Relation to the current fork

Until Phase 7, `AndiMb/webmscore` (Qt line) remains the released, corpus-
guarded truth — it is also the *baseline* the new pipeline is measured
against in Phases 3–5. Nothing in this plan changes it except the optional
Phase 1 audio removal.
