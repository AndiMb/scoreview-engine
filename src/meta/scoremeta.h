#pragma once

// Qt-free port of converter::NotationMeta (the fork's score-based variant of
// src/converter/internal/compat/notationmeta.{h,cpp}) onto muse's JSON
// serialization. The audio "tracks" list came from the project's audio
// settings; this build has no audio, so it is always the empty array — the
// same result the fork produced for a bare score. Keys are inserted in the
// alphabetical order QJsonObject used.

#include "global/types/bytearray.h"
#include "global/types/string.h"

namespace mu::engraving {
class Score;
}

namespace sve {
class ScoreMeta
{
public:
    static muse::ByteArray json(mu::engraving::Score* score);
    static muse::String title(const mu::engraving::Score* score);
};
}
