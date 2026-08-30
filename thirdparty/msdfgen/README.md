# Vendored msdfgen

MuseScore's Qt-free font path (`ifontface.h`, `fontfaceft.cpp`, `fontsengine`)
still compiles against msdfgen, which upstream MuseScore removed on 2024-03-29
(`c7cf38d1f6`, "removed msdfgen and harfbuzz"). The submodule therefore
carries the *users* of msdfgen but not msdfgen itself, and this copy supplies
it.

## Why not a submodule, and why not a newer msdfgen

This is not stock msdfgen 1.4. MuseScore reworked it: upstream stores a
contour's edges as heap-allocated `EdgeHolder`s, this copy stores
`EdgeSegment` **by value** — a union plus an `ActualType` tag, allocation-free.
Every file under `core/` differs, and `EdgeHolder.{h,cpp}` is gone.

That rework is not incidental: `ifontface.h` in the submodule compares glyph
shapes field by field over exactly those types, down to
`msdfgen::EdgeSegment::ActualType`. So

* a submodule at any upstream tag cannot work — the types it needs do not
  exist upstream, in 1.4 or in today's 1.13;
* upgrading would mean rewriting submodule files we deliberately do not patch,
  and shadow-copying them instead (`src/shadow/README.md`).

Both for code that never runs here: nothing in the conversion path calls
`FontsEngine::render()`, so dead-code elimination strips msdfgen from the wasm
build entirely. It only has to compile. That is the whole reason this stays a
frozen copy instead of a maintained dependency.

## License

MIT, see `msdfgen-1.4/LICENSE.txt`.
