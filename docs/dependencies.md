# Dependencies

What this engine builds against, where each version is written down, and how a
security fix reaches it. The machine-readable version is
[`tools/deps.json`](../tools/deps.json); `tools/check-deps.py --verify` fails
the build when the two disagree, so this page cannot quietly rot.

## The inventory

Native, and therefore part of the shipped wasm:

| | In use | Where the version lives | How it moves |
|---|---|---|---|
| MuseScore | 4.7.4 | `musescore` submodule | manual; corpus gate + shadow copies re-derived |
| FreeType | 2.14.3 | `thirdparty/freetype/` | vendored, ours to bump |
| HarfBuzz | 12.3.0 | `SetupHarfBuzz.cmake` (submodule) | MuseScore's muse_deps channel |
| brotli | 1.2.0 | `thirdparty/brotli/` | vendored, ours to bump |
| zlib | 1.3.1 | emscripten port, follows the emsdk pin | only via an emsdk bump |
| msdfgen | 1.4 (MuseScore fork) | `thirdparty/msdfgen/` | frozen on purpose |

Toolchain and CI:

| | In use | Where |
|---|---|---|
| emsdk / Emscripten | 4.0.7 | `build.yml`, `tools/build-wasm-docker.sh` |
| Ubuntu | 24.04 | `build.yml`, `Dockerfile` |
| g++ | 10 | `Dockerfile` |
| Node | 22 (supported to 2027-04-30) | `build.yml`; package declares `engines: >=18` |

Build-time npm packages (`web-public`, none of them shipped — the wrapper is
published as bundles) and the workflow's actions are covered by Dependabot:
rollup 4.63.0, typescript 7.0.2, and actions on their current majors.

## Why release currency is the primary signal

The obvious design is to point a CVE scanner at the dependency list. That does
not work for vendored C libraries, and the FreeType bump of 2026-08-30 is the
worked example.

FreeType 2.14.1 was affected by nine CVEs fixed in 2.14.2 and 2.14.3. Asked
about exactly that version:

* **OSV**, queried as `pkg:generic/freetype@2.14.1`, returned **zero**
  vulnerabilities. Zero for zlib and brotli too; only HarfBuzz produced hits,
  and those were OSS-Fuzz entries.
* **NVD**, queried by `cpe:2.3:a:freetype:freetype:2.14.1`, returned **one** of
  the nine (CVE-2026-23865). Three sampled from the remainder — CVE-2026-22007,
  -22018, -34268 — turn out to be filed against Oracle Java, which bundles
  FreeType, with no FreeType CPE attached.

The blind spots are not the same in both, which is why both run: NVD found
nothing for FreeType beyond that one entry but did report two zlib CVEs that
OSV missed entirely (both inapplicable here — see the triage table below).
Neither is a substitute for reading whether you are on the current release.

What did say it plainly was FreeType's own `docs/CHANGES`: *"A bunch of
potential security problems have been found. All users should update."* And
the distributions, which map the Oracle-filed CVEs back onto FreeType
([GLSA 202608-02](https://security.gentoo.org/glsa/202608-02)).

So `tools/check-deps.py` treats **"are we on the current upstream release"** as
the signal that decides, and runs OSV and NVD as a second opinion whose silence
proves nothing. For a runtime or a runner image the question is different again
— there is always a newer Node — so those are watched by *end of support*
instead.

## Triaging what the feeds do report

A finding that is real but not applicable has to be written down, or it files
the same issue every Monday until nobody reads the issue any more. Each
component in `tools/deps.json` may carry an `advisories_acknowledged` map of
advisory ID to reason; those are still printed, but they no longer count as
findings. What is acknowledged today, and why:

| Advisory | | Why it does not apply |
|---|---|---|
| OSV-2023-137 | harfbuzz | Heap-buffer-overflow in `Coverage::get_population` — shaping code, so it *would* be reachable, but it was fixed in 2023 and 12.3.0 sits 4116 commits ahead of the fix and none behind. OSV records it as a commit range, which is why a version query still matches. |
| OSV-2026-53 | harfbuzz | Use-after-free in `graph::LigatureSubstFormat1::shrink` — hb-subset. The only HarfBuzz API this engine calls is the shaping one; nothing calls `hb_subset_*`. Linked in only because muse_deps builds the amalgamated source. |
| OSV-2026-962 | harfbuzz | Uninitialized value in `iup_delta_optimize` — hb-subset, same reasoning. |
| CVE-2026-22184 | zlib | Buffer overflow in `contrib/untgz`, CVSS 7.8. NVD's own text limits it to the standalone utility, and the emscripten port builds only the fifteen core `.c` files. `contrib/` is not compiled. |
| CVE-2026-27171 | zlib | Unbounded loop reachable through `crc32_combine64`, CVSS 2.9. Nothing here calls it — the mscz path uses `inflate` only. |

An acknowledgement is a claim about *this* build and it expires with the next
bump of that dependency: re-check them when the version moves.

## What runs when

* **Every build** (`build.yml`, native job): `check-deps.py --verify`. Offline,
  sub-second. Every version in the manifest must match what the tree actually
  builds — the directory name under `thirdparty/`, the pin in
  `SetupHarfBuzz.cmake`, the image tag in the workflow, the lockfile entry.
* **Weekly** (`dependencies.yml`, Mondays): `--verify --online --updates
  --advisories`. Asks each upstream for newer releases, resolves zlib against
  the pinned emsdk's port file, queries OSV and NVD, and files one running
  GitHub issue when something has moved. A broken checker goes red instead, so
  the two failure modes stay distinguishable.
* **Continuously**: Dependabot, for npm and the actions.

Set an `NVD_API_KEY` repository secret to lift the NVD rate limit; without it
the script paces itself and the job takes a minute or two longer.

## Updating each one

**FreeType, brotli** — vendored, so they move on our schedule. Recipe and
signature check in the README next to each. FreeType changes glyph outlines and
metrics, so a bump goes through the corpus gate; 2.14.1 → 2.14.3 was measured
over all 569 vtest scores and the fingerprints came out byte-identical (the
release's rendering changes are in LCD filtering, which this build does not
use).

**HarfBuzz** — comes from `musescore/muse_deps@legacy`, which currently offers
only 12.3.0 and 7.1.0, so 12.3.0 *is* current for the channel. Going past it
means leaving the channel (`prefetch/` already pre-seeds the archive, so it is
a URL and a 7z) and re-measuring the corpus baseline, because HarfBuzz decides
glyph advances and with them the layout the baseline was recorded against.
12.3.1 and 12.3.2 are fuzzing and NULL-dereference hardening, no CVE.

**zlib** — not directly updatable. The wasm links whatever the pinned emsdk's
port ships, today 1.3.1; the native CLI links the distro package and gets
distro security updates. zlib 1.3.2 (2026-02-17) carries the fixes from the
7ASecurity audit, and the only route to it is an emsdk bump.

**emsdk** — 4.0.7 against 6.0.8 upstream, two majors. Worth doing, and it is
the open item on this page, but it is its own piece of work: the wasm gate's
tolerated float noise is toolchain-specific, so the bump means re-measuring it,
and Emscripten renames link settings between majors without always erroring on
an unknown one.

**MuseScore** — a bump re-runs the corpus gate and re-derives every shadow copy
(`src/shadow/README.md`). Note that `main` no longer has `src/framework` at all:
the `muse_framework` split has landed, and there freetype, harfbuzz and msdfgen
arrive through `require_dep()` from muse_deps rather than as vendored trees. The
next submodule bump past 4.7.x therefore changes where the font stack comes
from — which is one more reason FreeType now lives in this repository.

**msdfgen** — deliberately frozen. It is a MuseScore rework of 1.4, not stock:
`ifontface.h` compares glyph shapes over its by-value `EdgeSegment` types, which
do not exist upstream in 1.4 or in today's 1.13. It is build-only — nothing in
the conversion path calls `FontsEngine::render()`, so dead-code elimination
strips it from the wasm entirely. It only has to compile.

## Upstream is not a security channel for this

Worth stating once, because it is the assumption the old arrangement rested on:
waiting for MuseScore does not deliver these fixes. When FreeType was moved into
this repository, `muse_deps/prebuilt.lock` — rebuilt 2026-08-17, five months
after FreeType 2.14.3 — still pinned freetype 2.14.1, harfbuzz 12.3.0 and zlib
1.3.1. Upstream was exactly as far behind as we were.
