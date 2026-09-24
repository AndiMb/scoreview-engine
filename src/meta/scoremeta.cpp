#include "scoremeta.h"

#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "global/serialization/json.h"
#include "global/realfn.h"

#include "engraving/dom/keysig.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/text.h"
#include "engraving/dom/timesig.h"
#include "engraving/style/style.h"
#include "engraving/types/typesconv.h"

#include "log.h"

using namespace muse;
using namespace mu::engraving;

namespace sve {
static std::string boolToString(bool b)
{
    // string-typed booleans as in NotationMeta, kept for consumer compatibility
    return b ? "true" : "false";
}

static bool shouldTryRecognizeText(const Text* text)
{
    const TextStyleType type = text->textStyleType();
    if (type == TextStyleType::DEFAULT || type == TextStyleType::FRAME) {
        return true;
    }

    return (int)(type) >= (int)TextStyleType::USER1 && (int)(type) <= (int)TextStyleType::USER12;
}

static String recognizeTitle(const Score* score)
{
    const MeasureBase* mb = score->first();
    if (!mb || !mb->isVBox()) {
        return String();
    }

    const Text* titleText = nullptr;
    // DBL_MIN is the smallest POSITIVE double, not the most negative one, so
    // this is not the "lowest possible" sentinel it looks like. Kept verbatim
    // from upstream NotationMeta::recognizeTitle - font sizes are positive, so
    // it behaves, and changing it would move the corpus baseline for nothing.
    double maxFontSize = DBL_MIN;
    double minY = DBL_MAX;

    for (const EngravingItem* item : mb->el()) {
        if (!item || !item->isText()) {
            continue;
        }

        const Text* text = toText(item);
        if (!shouldTryRecognizeText(text)) {
            continue;
        }

        if (text->size() < maxFontSize) {
            continue;
        }

        if (RealIsEqual(text->size(), maxFontSize) && text->y() > minY) {
            continue;
        }

        titleText = text;
        maxFontSize = text->size();
        minY = text->y();
    }

    return titleText ? titleText->plainText() : String();
}

static String recognizeComposer(const Score* score)
{
    const MeasureBase* mb = score->first();
    if (!mb || !mb->isVBox()) {
        return String();
    }

    const Text* rightmostText = nullptr;
    double rightmostTextX = mb->ldata()->bbox().center().x();

    for (const EngravingItem* item : mb->el()) {
        if (!item || !item->isText()) {
            continue;
        }

        const Text* text = toText(item);
        if (!shouldTryRecognizeText(text)) {
            continue;
        }

        if (text->x() > rightmostTextX) {
            rightmostText = text;
            rightmostTextX = text->x();
        }
    }

    return rightmostText ? rightmostText->plainText() : String();
}

static String scoreTitle(const Score* score)
{
    String title;
    const Text* text = score->getText(TextStyleType::TITLE);
    if (text) {
        title = text->plainText();
    }

    if (title.isEmpty()) {
        title = score->metaTag(u"workTitle");
    }

    if (title.isEmpty()) {
        title = recognizeTitle(score);
    }

    // No fallback to score->name(), where upstream NotationMeta has one: the
    // name is the file's, and in the wasm build that file is /tmp/score-<n>,
    // numbered by how many scores the module has loaded before. An untitled
    // score answered "score-17", natively the upload's name - neither of them
    // the score's. Empty, as the SVG writer already has it.
    return title;
}

static String subtitle(const Score* score)
{
    String subtitle;
    const Text* text = score->getText(TextStyleType::SUBTITLE);
    if (text) {
        subtitle = text->plainText();
    }

    return subtitle;
}

static String composer(const Score* score)
{
    String composer;
    const Text* text = score->getText(TextStyleType::COMPOSER);
    if (text) {
        composer = text->plainText();
    }

    if (composer.isEmpty()) {
        composer = score->metaTag(u"composer");
    }

    if (composer.isEmpty()) {
        composer = recognizeComposer(score);
    }

    return composer;
}

static String poet(const Score* score)
{
    String poet;
    const Text* text = score->getText(TextStyleType::LYRICIST);
    if (text) {
        poet = text->plainText();
    }

    if (poet.isEmpty()) {
        poet = score->metaTag(u"lyricist");
    }

    return poet;
}

static String timesig(const Score* score)
{
    size_t staves = score->nstaves();
    size_t tracks = staves * VOICES;
    const Segment* timeSigSegment = score->firstSegmentMM(SegmentType::TimeSig);
    if (!timeSigSegment) {
        return String();
    }

    String timeSig;
    const EngravingItem* element = nullptr;
    for (size_t track = 0; track < tracks; ++track) {
        element = timeSigSegment->element(static_cast<int>(track));
        if (element) {
            break;
        }
    }

    if (element && element->isTimeSig()) {
        const TimeSig* ts = toTimeSig(element);
        timeSig = String(u"%1/%2").arg(ts->numerator()).arg(ts->denominator());
    }

    return timeSig;
}

// Note: no break - the LAST tempo text in the score wins, as in upstream
// NotationMeta::tempo. Deliberate, and part of the baseline.
static std::pair<int, String> tempo(const Score* score)
{
    int tempo = 0;
    String tempoText;
    for (const Segment* segment = score->firstSegmentMM(SegmentType::All); segment;
         segment = segment->next1MM()) {
        auto annotations = segment->annotations();
        for (const EngravingItem* annotation : annotations) {
            if (annotation && annotation->isTempoText()) {
                const TempoText* tt = toTempoText(annotation);
                tempo = round(tt->tempo().toBPM().val);
                tempoText = tt->xmlText();
            }
        }
    }

    return { tempo, tempoText };
}

static JsonArray partsJsonArray(const Score* score)
{
    JsonArray jsonPartsArray;
    for (const Part* part : score->parts()) {
        JsonObject jsonPart;
        jsonPart.set("harmonyCount", part->harmonyCount());
        jsonPart.set("hasDrumStaff", boolToString(part->hasDrumStaff()));
        jsonPart.set("hasPitchedStaff", boolToString(part->hasPitchedStaff()));
        jsonPart.set("hasTabStaff", boolToString(part->hasTabStaff()));
        jsonPart.set("id", part->id().toStdString());
        jsonPart.set("instrumentId", part->instrumentId());
        jsonPart.set("instrumentName", part->instrumentName());
        jsonPart.set("isVisible", boolToString(part->show()));
        jsonPart.set("lyricCount", part->lyricCount());
        jsonPart.set("name", String(part->longName()).replace(u"\n", u""));
        jsonPart.set("program", part->midiProgram());
        jsonPartsArray.append(jsonPart);
    }

    return jsonPartsArray;
}

static JsonObject pageFormatJson(const MStyle& style)
{
    JsonObject format;
    format.set("height", round(style.styleD(Sid::pageHeight) * INCH));
    format.set("twosided", boolToString(style.styleB(Sid::pageTwosided)));
    format.set("width", round(style.styleD(Sid::pageWidth) * INCH));

    return format;
}

static void findTextByType(TextStyleType textStyleType, std::vector<String>& strings, EngravingItem* element)
{
    if (!element->isTextBase()) {
        return;
    }

    const TextBase* text = toTextBase(element);
    if (text->textStyleType() == textStyleType) {
        strings.push_back(text->plainText());
    }
}

static JsonObject typeDataJson(Score* score)
{
    JsonObject typesData;
    static const std::vector<std::pair<std::string, TextStyleType> > namesTypesList {
        { "composers", TextStyleType::COMPOSER },
        { "poets", TextStyleType::LYRICIST },
        { "subtitles", TextStyleType::SUBTITLE },
        { "titles", TextStyleType::TITLE }
    };

    for (const auto& nameType : namesTypesList) {
        JsonArray typeData;
        std::vector<String> typeTextStrings;
        score->scanElements([&](EngravingItem* item) { findTextByType(nameType.second, typeTextStrings, item); });
        for (const auto& typeStr : typeTextStrings) {
            typeData.append(typeStr);
        }
        typesData.set(nameType.first, typeData);
    }

    return typesData;
}

// Key signatures and rehearsal marks (keySigs, rehearsalMarks)
//
// Neither field exists in upstream NotationMeta; both are additions of this
// repo. They live here and not in the submodule because the MuseScore core
// stays unpatched (see README, "Architecture") - and there is no reason to
// touch it: the metadata JSON is built entirely in this file, the core only
// provides the model read here. The stock MuseScore CLI (--score-media) does
// not know these fields, so consumers must tolerate their absence.
//
// Both lists count the same way:
// - "measure" is the running number of the measure in the notated chain
//   (firstMeasure/nextMeasure), 1-based. Deliberately not Measure::no():
//   that is the *displayed* number, shifted by a measure number offset and
//   by irregular measures (pickups) - a consumer addressing measures by
//   their order would be off. Deliberately not the MM chain either:
//   multi-measure rests are presentation, not measures.
// - "tick" is the notated tick, not the unrolled one - repeats are unrolled
//   by the consumer on its own timeline.

// Running, 1-based measure number for each measure of the notated chain.
static std::unordered_map<const Measure*, int> measureNumbers(const Score* score)
{
    std::unordered_map<const Measure*, int> numbers;
    int n = 0;
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        numbers[m] = ++n;
    }
    return numbers;
}

// Key::INVALID means "no key signature set", musically zero accidentals.
// A sentinel value in the JSON would be a trap for every consumer.
static int concertKeyValue(const KeySigEvent& ev)
{
    const Key key = ev.concertKey();
    return key == Key::INVALID ? 0 : static_cast<int>(key);
}

static JsonObject keySigJson(int measure, int tick, const KeySigEvent& ev)
{
    JsonObject k;
    k.set("measure", measure);
    k.set("tick", tick);
    k.set("concertKey", concertKeyValue(ev));
    // "unknown" becomes null: most scores set no mode, and a consumer must
    // not mistake "not stated" for a mode called "unknown". All other values
    // use the .mscx spelling (TConv::toXml), so there is exactly one
    // vocabulary.
    if (ev.mode() == KeyMode::UNKNOWN) {
        JsonValue nullMode;
        nullMode.setNull();
        k.set("mode", nullMode);
    } else {
        k.set("mode", std::string(std::string_view(TConv::toXml(ev.mode()))));
    }
    return k;
}

// Sounding key, one entry per key change, from the first staff.
//
// The first staff is enough: concertKey() is the sounding key and thus the
// same on every staff; transposing instruments differ only in the notated
// key. The KeySig elements themselves are read, not staff->keyList(): only
// the element knows whether layout created it (generated() - the courtesy
// repeat at the start of a system), and only real changes should appear.
static JsonArray keySigsJson(const Score* score, const std::unordered_map<const Measure*, int>& numbers)
{
    JsonArray list;
    const Staff* staff = score->staff(0);
    const Measure* first = score->firstMeasure();
    if (!staff || !first) {
        return list;
    }

    bool havePrev = false;
    int prevKey = 0;
    KeyMode prevMode = KeyMode::UNKNOWN;
    auto push = [&](int measure, int tick, const KeySigEvent& ev) {
        const int k = concertKeyValue(ev);
        // The same key again (e.g. restated after a section break) is not a
        // change.
        if (havePrev && k == prevKey && ev.mode() == prevMode) {
            return;
        }
        list.append(keySigJson(measure, tick, ev));
        havePrev = true;
        prevKey = k;
        prevMode = ev.mode();
    };

    // Without a key signature at the start (C major is often only implicit)
    // measure 1 carries the key the staff knows there - so the list always
    // starts at measure 1 and a consumer needs no default.
    bool haveStart = false;
    for (const Measure* m = first; m; m = m->nextMeasure()) {
        for (const Segment* s = m->first(SegmentType::KeySig); s; s = s->next(SegmentType::KeySig)) {
            const EngravingItem* e = s->element(0);
            if (!e || !e->isKeySig() || e->generated()) {
                continue;
            }
            if (!haveStart && s->tick().isNotZero()) {
                push(1, 0, staff->keySigEvent(Fraction(0, 1)));
            }
            haveStart = true;
            const auto it = numbers.find(m);
            push(it != numbers.end() ? it->second : 0, s->tick().ticks(), toKeySig(e)->keySigEvent());
        }
    }
    if (!haveStart) {
        push(1, 0, staff->keySigEvent(Fraction(0, 1)));
    }
    return list;
}

// Rehearsal marks as plain text, in score order.
//
// plainText() rather than xmlText(): the text may carry formatting
// (<b>, <font ...>), and a consumer wants the letter, not the markup.
// A system text can appear as a copy on further staves; per segment only the
// one on the topmost staff counts, so a mark is not listed more than once.
static JsonArray rehearsalMarksJson(const Score* score, const std::unordered_map<const Measure*, int>& numbers)
{
    JsonArray list;
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        const auto it = numbers.find(m);
        const int measure = it != numbers.end() ? it->second : 0;
        for (const Segment* s = m->first(); s; s = s->next()) {
            const RehearsalMark* found = nullptr;
            for (const EngravingItem* a : s->annotations()) {
                if (a && a->isRehearsalMark() && (!found || a->staffIdx() < found->staffIdx())) {
                    found = toRehearsalMark(a);
                }
            }
            if (!found) {
                continue;
            }
            JsonObject r;
            r.set("measure", measure);
            r.set("tick", s->tick().ticks());
            r.set("text", found->plainText());
            list.append(r);
        }
    }
    return list;
}

String ScoreMeta::title(const Score* score)
{
    return scoreTitle(score);
}

ByteArray ScoreMeta::json(Score* score)
{
    IF_ASSERT_FAILED(score) {
        return ByteArray();
    }

    JsonObject json;

    auto _tempo = tempo(score);
    const auto numbers = measureNumbers(score);

    json.set("composer", composer(score));
    json.set("duration", score->duration());
    json.set("fileVersion", score->mscVersion());
    json.set("hasHarmonies", boolToString(score->hasHarmonies()));
    json.set("hasLyrics", boolToString(score->hasLyrics()));
    json.set("keySigs", keySigsJson(score, numbers));
    json.set("keysig", static_cast<int>(score->keysig()));
    json.set("lyrics", score->extractLyrics());
    json.set("measures", static_cast<int>(score->nmeasures()));
    json.set("mscoreVersion", score->mscoreVersion());
    json.set("pageFormat", pageFormatJson(score->style()));
    json.set("pages", static_cast<int>(score->npages()));
    json.set("parts", partsJsonArray(score));
    json.set("poet", poet(score));
    json.set("previousSource", score->metaTag(u"source"));
    json.set("rehearsalMarks", rehearsalMarksJson(score, numbers));
    json.set("subtitle", subtitle(score));
    json.set("tempo", _tempo.first);
    json.set("tempoText", _tempo.second);
    json.set("textFramesData", typeDataJson(score));
    json.set("timesig", timesig(score));
    json.set("title", scoreTitle(score));
    json.set("tracks", JsonArray());

    return JsonDocument(json).toJson(JsonDocument::Format::Indented);
}
}
