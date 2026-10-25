#pragma once

// Views of a loaded score: a stored or generated part (excerpt), a page
// format, concert pitch, a transposition. MuseScore does the engraving; this
// module only selects, sets and remembers what the result has to be mapped
// back to.
//
// A view shares the main score's time axis and measure count - that is what
// lets a player keep its one MIDI and its one measure numbering while the
// picture changes. Two things make the mapping explicit for the caller:
//   * baseElid  - for every ChordRest segment of the view, the segment number
//                 the *main score* has at the same notated tick;
//   * staffMap  - for every staff of the view, the staff index in the main
//                 score.
// The main score's segment numbers are taken before anything is changed
// (captureBase), because a view on the full score edits the main score
// itself.

#include <map>
#include <string>
#include <vector>

#include "global/types/bytearray.h"
#include "global/types/string.h"

namespace mu::engraving {
class MasterScore;
class Score;
}

namespace sve {
struct ViewOptions {
    enum class Format {
        Original,
        Phone,           // portrait
        PhoneLandscape,
    };

    Format format = Format::Original;
    // -1: as the file says, 0: written pitch, 1: concert pitch
    int concertPitch = -1;
    // -12 ... 12, 0 = none
    int transposeSemitones = 0;
};

class ScoreViews
{
public:
    //! The score a view id stands for: -1 is the main score, 0 ... n-1 the
    //! excerpts in file order (stored ones first, generated ones appended).
    static mu::engraving::Score* score(mu::engraving::MasterScore* master, int excerptId);

    //! Creates one excerpt holding exactly the given parts (by part id) and
    //! lays it out. Returns its view id, or -1 when a part id is unknown.
    static int generateExcerpt(mu::engraving::MasterScore* master, const std::vector<std::string>& partIds,
                               muse::String* error);

    //! Parses {"format":"orig|phone|phonel","concertPitch":bool|null,
    //! "transposeSemitones":n}. Unknown keys are an error, not ignored: a
    //! typo would otherwise give the caller the original view without a word.
    static bool parseOptions(const std::string& json, ViewOptions& out, muse::String* error);

    //! Applies the options to the view and lays it out again.
    static bool apply(mu::engraving::Score* view, const ViewOptions& options, muse::String* error);

    //! Remembers the main score's segment numbers by tick. Idempotent: only
    //! the first call per score counts, so it can be called before every edit.
    static void captureBase(mu::engraving::MasterScore* master);
    static void forget(mu::engraving::MasterScore* master);

    //! {"baseElid":{"<viewElid>":<mainElid>,...},"staffMap":[...]} for the view.
    static muse::ByteArray mapping(mu::engraving::MasterScore* master, mu::engraving::Score* view);
};
}
