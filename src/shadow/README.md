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

## Drift guard

`upstream.lock` pins the git blob id of every upstream counterpart (including
prelude targets). `tools/check-shadow-drift.sh` verifies the pins against the
submodule and fails when upstream drifts — CI runs it on every build. When it
fires: re-derive the shadow copy / re-check the prelude against the new
upstream file, then update the lock with the new blob id
(`git -C musescore rev-parse HEAD:<path>`).
