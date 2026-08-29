#include "scoreloader.h"

// The read + layout dance is ScoreRW::readScore (the submodule's test util)
// with the Score::update() pass appended; kept in step with it so a
// submodule bump that changes the loading sequence shows up as a corpus diff,
// not as silent drift between the CLI and the wasm build.

#include "global/io/path.h"

#include "engraving/compat/scoreaccess.h"
#include "engraving/compat/mscxcompat.h"
#include "engraving/dom/masterscore.h"
#include "engraving/engravingerrors.h"
#include "engraving/infrastructure/localfileinfoprovider.h"

#include "log.h"

using namespace muse;
using namespace mu::engraving;

namespace sve {
MasterScore* loadScore(const io::path_t& path, bool doLayout)
{
    MasterScore* score = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
    score->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(path));

    std::string suffix = io::suffix(path);
    Err rv;
    if (suffix == "mscz" || suffix == "mscx") {
        rv = static_cast<Err>(compat::loadMsczOrMscx(score, path.toString(), false).code());
    } else {
        rv = Err::FileUnknownType;
    }

    if (rv != Err::NoError) {
        LOGE() << "can't load score, path: " << path;
        delete score;
        return nullptr;
    }

    if (doLayout) {
        ScoreLoad sl;
        for (Score* s : score->scoreList()) {
            s->doLayout();
        }
    }

    // As in ScoreRW::readScore: elements may have consulted the incomplete
    // repeat list during reading; make sure the next user recomputes it.
    score->setPlaylistDirty();

    if (doLayout) {
        // Desktop and webmscore both run Score::update() after the initial
        // layout (via NotationProject setup / webmscore's _doLoad). If the
        // layout added elements that request a tempomap rebuild (courtesy
        // time signatures do), that pass wipes the volta tempo entries
        // Volta::setTempo() planted during layout — without it the exported
        // MIDI carries extra tempo events the Qt pipeline doesn't have
        // (vtest: volta-1/2, slurs-30).
        score->update();
    }

    return score;
}
}
