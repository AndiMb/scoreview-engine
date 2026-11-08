#include "scoreloader.h"

// The read + layout dance is ScoreRW::readScore (the submodule's test util)
// with the Score::update() pass appended; kept in step with it so a
// submodule bump that changes the loading sequence shows up as a corpus diff,
// not as silent drift between the CLI and the wasm build.

#include <algorithm>

#include "global/io/path.h"
#include "global/io/buffer.h"

#include "engraving/compat/scoreaccess.h"
#include "engraving/compat/mscxcompat.h"
#include "engraving/dom/imageStore.h"
#include "engraving/dom/masterscore.h"
#include "engraving/engravingerrors.h"
#include "engraving/infrastructure/localfileinfoprovider.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/rw/mscloader.h"

#include "api/scoreguard.h"
#include "config/engravinginit.h"

#include "log.h"

using namespace muse;
using namespace mu::engraving;

namespace {
std::string hexOf(const ByteArray& b)
{
    static const char* D = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < b.size(); ++i) {
        s += D[b[i] >> 4];
        s += D[b[i] & 15];
    }
    return s;
}

// The store name -> hash mapping of ImageStore::getImage, char for char
// (including what it does with characters that are not lower-case hex), so
// the comparison below asks exactly what the reader will ask the store.
bool storeHash(const std::string& ref, std::string& hash)
{
    const std::string name = io::completeBasename(ref).toStdString();
    if (name.size() != 32) {
        return false;
    }
    auto toInt = [](char c) { return (c >= '0' && c <= '9') ? c - '0' : c - 'a' + 10; };
    ByteArray h(16);
    for (int i = 0; i < 16; ++i) {
        h[i] = static_cast<uint8_t>(toInt(name.at(i * 2)) * 16 + toInt(name.at(i * 2 + 1)));
    }
    hash = hexOf(h);
    return true;
}

// Issue #34. The image store is global to the module and resolves an
// <Image> by name alone, so a score could name a picture it does not carry
// and get whatever another live score put there. A reference is fine if the
// score's own container holds those bytes, or if nothing in the store would
// answer it (the image then simply stays empty, as before).
bool referencesForeignPicture(const sve::guard::Report& r)
{
    for (const std::string& ref : r.imageRefs) {
        std::string hash;
        if (!storeHash(ref, hash)) {
            continue;
        }
        if (std::find(r.pictureHashes.begin(), r.pictureHashes.end(), hash) != r.pictureHashes.end()) {
            continue;
        }
        for (const ImageStoreItem* item : imageStore) {
            if (hexOf(item->hash()) == hash) {
                return true;
            }
        }
    }
    return false;
}

// Issue #35. Read206 keeps the text styles it cannot map in a file-scope
// static (read206.cpp, excessTextStyles206) and clears it only when it reads
// a <Style>, so a 2.x score without one would inherit the previous 2.x
// score's. Reading a <Style/> is the one way to clear it from out here.
bool s_read206Dirty = false;

void resetRead206()
{
    // MscIoMode::XmlFile is MuseScore's single-file XML container, not a
    // plain .mscx: the score goes in as the <file> named after filePath.
    static const char RESET[]
        = "<files><file name=\"reset206.mscx\"><![CDATA["
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<museScore version=\"2.06\"><Score><Style/></Score></museScore>\n"
          "]]></file></files>\n";
    ByteArray data(RESET, sizeof(RESET) - 1);
    io::Buffer buf(&data);

    MscReader::Params params;
    params.device = &buf;
    params.filePath = u"reset206.mscx";
    params.mode = MscIoMode::XmlFile;
    MscReader reader(params);
    if (reader.open()) {
        MasterScore* score = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
        // As compat::loadMsczOrMscx does: the reader asks the score for its
        // file info and does not expect to find none.
        score->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(params.filePath));
        const Ret ret = MscLoader().loadMscz(score, reader, nullptr, true);
        if (!ret) {
            LOGE() << "2.x style reset failed: " << ret.toString();
        }
        delete score;
    } else {
        LOGE() << "2.x style reset could not open its document";
    }
    s_read206Dirty = false;
}
}

namespace sve {
MasterScore* loadScore(const io::path_t& path, bool doLayout, std::string* error)
{
    // Everything the upstream reader would trust, checked before it runs;
    // see api/scoreguard.h.
    const guard::Report report = guard::checkFile(path.toStdString());
    if (!report.ok || referencesForeignPicture(report)) {
        const std::string why = report.ok ? "image refers to a picture this score does not contain" : report.reason;
        LOGE() << "refusing score " << path << ": " << why;
        if (error) {
            *error = why;
        }
        return nullptr;
    }
    if (std::find(report.versionMajors.begin(), report.versionMajors.end(), 2) != report.versionMajors.end()) {
        if (s_read206Dirty) {
            resetRead206();
        }
        s_read206Dirty = true;
    }

    // The one read the caller actually asked for. Everything else the engine
    // opens is a resource, and a path a *score* names gets no say — see
    // sve::allowRead and enginefilesystem.h.
    allowRead(path);

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
        if (error) {
            *error = "the score could not be read";
        }
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
