# Vendored FreeType

FreeType parses every font this engine touches: the woff2 resource pack loaded
by `sve::initEngraving`, and whatever a caller hands `addFont()`. It is the
one dependency here that reads attacker-shaped input by design, so it is also
the one that must be updatable on its own schedule.

Until 2026-08-30 it came from the MuseScore submodule
(`musescore/src/framework/draw/thirdparty/freetype/freetype-2.14.1`). That made
the only route to a FreeType fix a MuseScore bump — and upstream MuseScore is
not a security channel for it: `muse_deps/prebuilt.lock`, rebuilt 2026-08-17,
still pinned 2.14.1, two security releases behind. Waiting would not have
helped. Hence this copy.

The submodule's FreeType is no longer built. The wrapper next to it
(`draw/thirdparty/freetype/CMakeLists.txt`) was never used either: it sets
`FT_DISABLE_BROTLI`, and without brotli FreeType rejects every .woff2, so the
top-level `CMakeLists.txt` has always configured FreeType itself. Only
`FREETYPE_DIR` changed.

## What is here

Upstream: [freetype/freetype](https://gitlab.freedesktop.org/freetype/freetype),
release **2.14.3** (2026-03-22), from the official tarball
`freetype-2.14.3.tar.xz`, SHA-256
`36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f`.

The tarball is signed. Its OpenPGP signature verifies against
`E306 7470 7856 409F F194 8010 BE6C 3AAC 63AD 8E3F`, "Werner Lemberg
<wl@gnu.org>" — the same key that signed 2.14.1, the release this copy
replaces.

Unmodified, and complete except for two generated documentation directories
that only add weight to the repository:

* `docs/reference/` — 6.7 MB of generated HTML API docs,
* `docs/oldlogs/` — 1.8 MB of historical changelogs.

Nothing else is removed, so everything present is byte-identical to the signed
tarball. `docs/CHANGES`, `LICENSE.TXT`, `docs/FTL.TXT` and `docs/GPLv2.TXT`
are kept.

## Why 2.14.3

2.14.1 is affected by nine CVEs fixed in 2.14.2 and 2.14.3 — out-of-bounds
reads and information disclosure while parsing fonts: CVE-2026-22007,
-22008, -22013, -22016, -22018, -22021, -23865, -34268, -34282
([GLSA 202608-02](https://security.gentoo.org/glsa/202608-02)). Both releases
say the same thing in `docs/CHANGES`: "A bunch of potential security problems
have been found. All users should update."

Only one of the nine, CVE-2026-23865, carries a `cpe:2.3:a:freetype:freetype`
configuration in NVD, so a CPE-based scan of this dependency finds one of them.
The three sampled from the remainder (-22007, -22018, -34268) are filed against
Oracle Java, which bundles FreeType. That is why `tools/check-deps.py` treats
*release currency* as the primary signal for the vendored C libraries and CVE
feeds as advisory. See `docs/dependencies.md`.

## Refreshing it

    curl -sSLO https://download.savannah.gnu.org/releases/freetype/freetype-<version>.tar.xz
    curl -sSLO https://download.savannah.gnu.org/releases/freetype/freetype-<version>.tar.xz.sig
    gpg --verify freetype-<version>.tar.xz.sig freetype-<version>.tar.xz
    sha256sum freetype-<version>.tar.xz          # record it above
    tar xf freetype-<version>.tar.xz
    rm -rf freetype-<version>/docs/reference freetype-<version>/docs/oldlogs
    rm -rf freetype-<old>

Then update `FREETYPE_DIR` in the top-level `CMakeLists.txt`, the version in
`cmake/FindBrotliDec.cmake`'s sibling comment if it mentions a path, and the
`freetype` entry in `tools/deps.json` — `tools/check-deps.py --verify` fails
until the manifest and the tree agree.

**A FreeType bump changes glyph outlines and metrics, so it changes SVG
output.** It goes through the corpus gate like any other engraving change; see
[the README](../../README.md#checking-a-build-against-a-released-one). The
2.14.1 → 2.14.3 move was measured over all 569 vtest scores and left every
fingerprint — MIDI, positions, metadata and SVG — bit-identical; the release's
rendering changes are in LCD filtering, which this build does not use.

## License

FreeType is dual-licensed under the FreeType License (BSD-style with credit
clause) and GPLv2. This project uses it under the FreeType License; see
`freetype-2.14.3/LICENSE.TXT` and `freetype-2.14.3/docs/FTL.TXT`.
