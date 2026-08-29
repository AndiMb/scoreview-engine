#pragma once

// The one score-loading path of the engine. mscz2media and the wasm build
// both go through here, so the two produce identical fingerprints by
// construction — the corpus gate compares them.

#include "global/io/path.h"

namespace mu::engraving {
class MasterScore;
}

namespace sve {
// Read a .mscz/.mscx from `path`, lay it out (unless doLayout is false) and
// run the post-layout Score::update() pass the desktop and webmscore
// pipelines run (without it, exported MIDI carries volta tempo events the
// Qt pipeline doesn't have). Returns nullptr on read failure.
mu::engraving::MasterScore* loadScore(const muse::io::path_t& path, bool doLayout = true);
}
