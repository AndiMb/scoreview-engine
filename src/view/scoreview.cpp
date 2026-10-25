#include "scoreview.h"

#include <cmath>
#include <map>
#include <unordered_map>

#include "global/serialization/json.h"
#include "global/types/translatablestring.h"
#include "log.h"

#include "engraving/dom/excerpt.h"
#include "engraving/dom/linkedobjects.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/editing/transpose.h"
#include "engraving/style/style.h"
#include "engraving/types/constants.h"

#include "intervalmap.h"
#include "positions/segmentindex.h"

using namespace muse;
using namespace mu::engraving;

namespace sve {
namespace {
// Page sizes in millimetres. 105 mm is half an A4 width: a page that size,
// shown across a phone, comes out at about twice the size an A4 page would -
// larger notes without asking MuseScore to engrave them any differently than
// for a small page. 9:16, the shape of the screens it is meant for.
constexpr double PHONE_SHORT_MM = 105.0;
constexpr double PHONE_LONG_MM = 186.0;
constexpr double PHONE_MARGIN_MM = 4.0;

double mm(double v)
{
    return v / INCH;
}

// tick -> segment number of the main score, taken before any edit. Keyed by
// the score's address; forget() drops the entry with the score.
std::unordered_map<const MasterScore*, std::map<int, int> >& baseIndexes()
{
    static std::unordered_map<const MasterScore*, std::map<int, int> > indexes;
    return indexes;
}

void setPage(Score* s, double widthMm, double heightMm)
{
    s->undoChangeStyleVal(Sid::pageWidth, mm(widthMm));
    s->undoChangeStyleVal(Sid::pageHeight, mm(heightMm));
    s->undoChangeStyleVal(Sid::pagePrintableWidth, mm(widthMm - 2 * PHONE_MARGIN_MM));
    for (Sid sid : { Sid::pageEvenLeftMargin, Sid::pageOddLeftMargin, Sid::pageEvenTopMargin,
                     Sid::pageEvenBottomMargin, Sid::pageOddTopMargin, Sid::pageOddBottomMargin }) {
        s->undoChangeStyleVal(sid, mm(PHONE_MARGIN_MM));
    }
    // One-sided: margins that alternate between left and right pages make no
    // sense for a page that is only ever shown on its own.
    s->undoChangeStyleVal(Sid::pageTwosided, false);
    // Empty staves cost a phone page the most height, and a choir score with
    // a resting voice per system is the common case.
    s->undoChangeStyleVal(Sid::hideEmptyStaves, true);
}

// Line and page breaks were placed for the file's own page; on a page half
// as wide they would force overfull systems. Section breaks stay: they carry
// meaning (a new movement, numbering from one again), not just layout.
void removeLayoutBreaks(Score* s)
{
    for (MeasureBase* mb = s->first(); mb; mb = mb->next()) {
        if (mb->lineBreak()) {
            mb->undoSetLineBreak(false);
        }
        if (mb->pageBreak()) {
            mb->undoSetPageBreak(false);
        }
    }
}

bool transposeAll(Score* s, int semitones, String* error)
{
    const int index = intervalIndexFor(semitones);
    if (index < 0) {
        *error = String(u"transposeSemitones out of range: %1").arg(semitones);
        return false;
    }
    // Transpose::transpose works on the selection, as in MuseScore's own
    // command line (--score-transpose selects all first). trKeys only takes
    // effect for a range selection, which selectAll is.
    s->cmdSelectAll();
    const bool ok = Transpose::transpose(s, TransposeMode::BY_INTERVAL,
                                         semitones < 0 ? TransposeDirection::DOWN : TransposeDirection::UP,
                                         Key::C, index, /*trKeys*/ true, /*transposeChordNames*/ true,
                                         /*useDoubleSharpsFlats*/ false);
    s->deselectAll();
    if (!ok) {
        *error = u"transposition failed";
    }
    return ok;
}
}

Score* ScoreViews::score(MasterScore* master, int excerptId)
{
    if (!master) {
        return nullptr;
    }
    if (excerptId == -1) {
        return master;
    }
    const std::vector<Excerpt*>& excerpts = master->excerpts();
    if (excerptId < 0 || static_cast<size_t>(excerptId) >= excerpts.size()) {
        return nullptr;
    }
    return excerpts.at(excerptId)->excerptScore();
}

int ScoreViews::generateExcerpt(MasterScore* master, const std::vector<std::string>& partIds, String* error)
{
    captureBase(master);

    std::vector<Part*> parts;
    for (const std::string& id : partIds) {
        Part* found = nullptr;
        for (Part* p : master->parts()) {
            if (p->id().toStdString() == id) {
                found = p;
                break;
            }
        }
        if (!found) {
            *error = String(u"unknown part id: %1").arg(String::fromStdString(id));
            return -1;
        }
        parts.push_back(found);
    }
    if (parts.empty()) {
        *error = u"no parts given";
        return -1;
    }

    // One excerpt for all the given parts - createExcerptsFromParts() would
    // make one per part. Its setup, for a single excerpt.
    Excerpt* excerpt = new Excerpt(master);
    String name;
    for (Part* part : parts) {
        excerpt->parts().push_back(part);
        name += (name.empty() ? String() : String(u", ")) + part->partName();
    }
    excerpt->setName(name, /*saveAndNotify*/ false);
    excerpt->setInitialPartId(parts.front()->id());
    // The track mapping in the excerpt's own numbering, part after part.
    TracksMap tracks;
    track_idx_t j = 0;
    for (Part* part : parts) {
        for (track_idx_t i = part->startTrack(); i < part->endTrack(); ++i, ++j) {
            tracks.insert({ i, j });
        }
    }
    excerpt->setTracksMapping(tracks);

    master->startCmd(TranslatableString::untranslatable("scoreview: generate excerpt"));
    master->initAndAddExcerpt(excerpt, /*fakeUndo*/ false);
    master->endCmd(false, /*layoutAllParts*/ true);
    excerpt->excerptScore()->doLayout();

    const std::vector<Excerpt*>& all = master->excerpts();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == excerpt) {
            return static_cast<int>(i);
        }
    }
    *error = u"excerpt was not added";
    return -1;
}

bool ScoreViews::parseOptions(const std::string& json, ViewOptions& out, String* error)
{
    out = ViewOptions();
    if (json.empty()) {
        return true;
    }
    std::string parseError;
    const JsonDocument doc = JsonDocument::fromJson(ByteArray(json.data(), json.size()), &parseError);
    if (!parseError.empty() || !doc.isObject()) {
        *error = u"view options are not a JSON object";
        return false;
    }
    const JsonObject o = doc.rootObject();
    for (const std::string& key : o.keys()) {
        if (key == "format") {
            const std::string f = o.value(key).toStdString();
            if (f == "orig") {
                out.format = ViewOptions::Format::Original;
            } else if (f == "phone") {
                out.format = ViewOptions::Format::Phone;
            } else if (f == "phonel") {
                out.format = ViewOptions::Format::PhoneLandscape;
            } else {
                *error = String(u"unknown format: %1").arg(String::fromStdString(f));
                return false;
            }
        } else if (key == "concertPitch") {
            const JsonValue v = o.value(key);
            out.concertPitch = v.isNull() ? -1 : (v.toBool() ? 1 : 0);
        } else if (key == "transposeSemitones") {
            const JsonValue v = o.value(key);
            if (!v.isNumber() || v.toDouble() != std::floor(v.toDouble()) || std::abs(v.toInt()) > 12) {
                *error = u"transposeSemitones must be an integer from -12 to 12";
                return false;
            }
            out.transposeSemitones = v.toInt();
        } else {
            *error = String(u"unknown view option: %1").arg(String::fromStdString(key));
            return false;
        }
    }
    return true;
}

bool ScoreViews::apply(Score* view, const ViewOptions& options, String* error)
{
    if (!view) {
        *error = u"no such view";
        return false;
    }
    captureBase(view->masterScore());

    view->startCmd(TranslatableString::untranslatable("scoreview: apply view"));
    switch (options.format) {
    case ViewOptions::Format::Original:
        break;
    case ViewOptions::Format::Phone:
        setPage(view, PHONE_SHORT_MM, PHONE_LONG_MM);
        removeLayoutBreaks(view);
        break;
    case ViewOptions::Format::PhoneLandscape:
        setPage(view, PHONE_LONG_MM, PHONE_SHORT_MM);
        removeLayoutBreaks(view);
        break;
    }
    if (options.concertPitch != -1) {
        view->cmdConcertPitchChanged(options.concertPitch == 1);
    }
    bool ok = true;
    if (options.transposeSemitones != 0) {
        ok = transposeAll(view, options.transposeSemitones, error);
    }
    view->endCmd(!ok, /*layoutAllParts*/ true);
    if (!ok) {
        return false;
    }

    // endCmd lays out what the commands marked; a page size change concerns
    // every page, so lay out all of it rather than trusting the marks.
    view->doLayout();
    return true;
}

void ScoreViews::captureBase(MasterScore* master)
{
    if (!master) {
        return;
    }
    auto& indexes = baseIndexes();
    if (indexes.count(master)) {
        return;
    }
    std::map<int, int> byTick;
    for (const auto& [segment, id] : chordRestSegmentIndex(master)) {
        byTick.emplace(segment->tick().ticks(), id);
    }
    indexes.emplace(master, std::move(byTick));
}

void ScoreViews::forget(MasterScore* master)
{
    baseIndexes().erase(master);
}

ByteArray ScoreViews::mapping(MasterScore* master, Score* view)
{
    captureBase(master);
    const std::map<int, int>& byTick = baseIndexes().at(master);

    // A view's segment lies on a tick the main score has a segment on too -
    // the main score holds every part. The floor lookup is for the one case
    // where that is not so: a multimeasure rest in the main score, whose
    // inner ticks it does not number.
    JsonObject baseElid;
    for (const auto& [segment, id] : chordRestSegmentIndex(view)) {
        auto it = byTick.upper_bound(segment->tick().ticks());
        if (it == byTick.begin()) {
            continue;
        }
        --it;
        baseElid.set(std::to_string(id), it->second);
    }

    // Staff of the view -> staff of the main score, through the staff links
    // MuseScore keeps between a part and the score. The main score maps to
    // itself.
    JsonArray staffMap;
    for (Staff* staff : view->staves()) {
        int target = -1;
        if (view == master) {
            target = static_cast<int>(staff->idx());
        } else if (const LinkedObjects* links = staff->links()) {
            for (EngravingObject* linked : *links) {
                if (linked->score() == master) {
                    target = static_cast<int>(toStaff(linked)->idx());
                    break;
                }
            }
        }
        staffMap.append(target);
    }

    JsonObject json;
    json.set("baseElid", baseElid);
    json.set("staffMap", staffMap);
    return JsonDocument(json).toJson(JsonDocument::Format::Compact);
}
}
