#include "scoremeta.h"

#include <cfloat>
#include <cmath>
#include <vector>

#include "global/serialization/json.h"
#include "global/realfn.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/text.h"
#include "engraving/dom/timesig.h"
#include "engraving/style/style.h"

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

    if (title.isEmpty()) {
        title = score->name();
    }

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

    json.set("composer", composer(score));
    json.set("duration", score->duration());
    json.set("fileVersion", score->mscVersion());
    json.set("hasHarmonies", boolToString(score->hasHarmonies()));
    json.set("hasLyrics", boolToString(score->hasLyrics()));
    json.set("keysig", static_cast<int>(score->keysig()));
    json.set("lyrics", score->extractLyrics());
    json.set("measures", static_cast<int>(score->nmeasures()));
    json.set("mscoreVersion", score->mscoreVersion());
    json.set("pageFormat", pageFormatJson(score->style()));
    json.set("pages", static_cast<int>(score->npages()));
    json.set("parts", partsJsonArray(score));
    json.set("poet", poet(score));
    json.set("previousSource", score->metaTag(u"source"));
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
