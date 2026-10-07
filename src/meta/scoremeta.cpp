#include "scoremeta.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

#include "global/serialization/json.h"
#include "global/realfn.h"

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/note.h"
#include "engraving/dom/ornament.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/text.h"
#include "engraving/dom/timesig.h"
#include "engraving/dom/trill.h"
#include "engraving/style/style.h"
#include "engraving/types/typesconv.h"

#include "positions/segmentindex.h"

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

// Lyric syllables and note spellings (lyricSyllables, noteSpellings)
//
// Additions of this repo like keySigs/rehearsalMarks above, and for the same
// reason built here and not in the submodule. Unlike those two lists they are
// addressed by "elid", not by measure: the number chordRestSegmentIndex()
// gives a ChordRest segment - the same number the position export hands to a
// player as the event id and the SVG export stamps as "seg-N" on what it
// draws. A consumer can thus put a syllable or a note name next to the
// notehead that sounds, without guessing from coordinates. Both walk the MM
// chain for that reason: another chain would be another numbering.

// Whether a staff of this measure is drawn at all - the condition
// Measure::scanElements and Segment::scanElements apply before they hand
// anything of that staff to Page::elements(), which the SVG writer paints.
static bool staffDrawn(const Measure* m, staff_idx_t staffIdx, const Score* score)
{
    return m->visible(staffIdx) && score->staff(staffIdx)->show();
}

// Upper bound for lyricSyllables. Beyond it the field is left out entirely
// rather than truncated: half a text is worse than none, and a consumer must
// be able to tell "too much" (field missing, hasLyrics "true") from "none"
// (empty list). 20000 syllables is several times an oratorio's choir part.
static constexpr size_t MAX_LYRIC_SYLLABLES = 20000;

// Every lyric syllable, ordered by elid, then staff, voice and verse.
// Returns false when there are more than MAX_LYRIC_SYLLABLES.
//
// Not Score::lyrics() / extractLyrics(): those unroll repeats through
// playbackCount and write it back into the model - a metadata call must not
// change what a later export sees. The repeat structure is the consumer's
// (it has the unrolled timeline from the positions); here a verse is just
// its number, 0-based as in the model.
//
// Only visible lyrics on staves that are drawn: the list is what the reader
// sees under the notes. plainText() rather than xmlText(), as for rehearsal
// marks - the syllable, not its formatting. "syllabic" uses the .mscx
// vocabulary (single/begin/middle/end), so a consumer joins a word exactly
// where MuseScore draws the hyphen; "melisma" says an extender line follows.
static bool lyricSyllablesJson(const Score* score, const std::unordered_map<const Segment*, int>& ids, JsonArray& out)
{
    size_t count = 0;
    const size_t tracks = score->ntracks();
    const Measure* first = score->firstMeasureMM();
    for (const Segment* s = first ? first->first(SegmentType::ChordRest) : nullptr; s;
         s = s->next1MM(SegmentType::ChordRest)) {
        const auto id = ids.find(s);
        if (id == ids.end()) {
            continue;
        }
        for (track_idx_t track = 0; track < tracks; ++track) {
            const EngravingItem* e = s->element(track);
            if (!e || !e->isChordRest() || !staffDrawn(s->measure(), track / VOICES, score)) {
                continue;
            }
            std::vector<const Lyrics*> lyrics;
            for (const Lyrics* l : toChordRest(e)->lyrics()) {
                if (l && l->visible()) {
                    lyrics.push_back(l);
                }
            }
            // The model keeps them in insertion order; verses entered out of
            // order would otherwise come out swapped. Stable, so two lines
            // with the same number (above and below the staff) keep theirs.
            std::stable_sort(lyrics.begin(), lyrics.end(),
                             [](const Lyrics* a, const Lyrics* b) { return a->verse() < b->verse(); });
            for (const Lyrics* l : lyrics) {
                if (++count > MAX_LYRIC_SYLLABLES) {
                    return false;
                }
                JsonObject o;
                o.set("elid", id->second);
                o.set("staff", static_cast<int>(track / VOICES));
                o.set("voice", static_cast<int>(track % VOICES));
                o.set("verse", l->verse());
                o.set("syllabic", std::string(std::string_view(TConv::toXml(l->syllabic()))));
                o.set("text", l->plainText());
                o.set("melisma", l->isMelisma());
                out.append(o);
            }
        }
    }
    return true;
}

// Whether the SVG writer paints this note: Page::elements() leaves out
// invisible items (getChildren(false)), and the writer skips items that do
// not collect for drawing or have no bounding box.
static bool noteDrawn(const Note* n)
{
    return n->visible() && n->collectForDrawing()
           && n->ldata() && n->ldata()->isSetBbox() && !n->ldata()->bbox().isEmpty();
}

// The notes Chord::scanElements hands to Page::elements() for one chord, in
// its order: first the cue notes of its ornaments (articulations are scanned
// before the notes - the auxiliary note of a turn or mordent drawn small
// beside the main note), then the chord's own notes, ascending, then its
// grace chords - before and after - in model order, each again the same way.
// Grace and cue chords are reached through the main chord, and the writer's
// findAncestor(SEGMENT) gives their noteheads the main chord's classes.
static void collectNotes(const Chord* chord, std::vector<EngravingItem*>& out)
{
    for (const Articulation* a : chord->articulations()) {
        if (a && a->isOrnament()) {
            if (const Chord* cue = toOrnament(a)->cueNoteChord()) {
                collectNotes(cue, out);
            }
        }
    }
    for (Note* n : chord->notes()) {
        out.push_back(n);
    }
    for (const Chord* grace : chord->graceNotes()) {
        collectNotes(grace, out);
    }
}

// Cue chords of trill lines, by the segment and track their noteheads are
// classed with. A trill's cue note is not reached through its chord but
// through the line (TrillSegment::scanElements), and lines are scanned per
// system after all of the system's measures (Page::scanElements) - so these
// notes come after everything collectNotes() finds for the same chord. The
// cue chord's parent is the start chord's segment, which is where seg-N
// comes from. Several trills on one chord follow the spanner map's order.
using CueKey = std::pair<const Segment*, track_idx_t>;
static std::map<CueKey, std::vector<const Chord*> > trillCueChords(const Score* score)
{
    std::map<CueKey, std::vector<const Chord*> > cues;
    for (const auto& entry : score->spanner()) {
        const Spanner* sp = entry.second;
        if (!sp || !sp->isTrill()) {
            continue;
        }
        const Chord* cue = toTrill(sp)->cueNoteChord();
        const EngravingItem* segment = cue ? cue->findAncestor(ElementType::SEGMENT) : nullptr;
        if (segment) {
            cues[{ toSegment(segment), cue->track() }].push_back(cue);
        }
    }
    return cues;
}

// Pitch and spelling of every drawn notehead, one entry per chord, keyed like
// the SVG class of its noteheads: "seg-<elid> st-<staff> vc-<voice>".
//
// The consumer assigns the n-th pair of "notes" to the n-th Note element
// with that class in the SVG, so the list has to hold exactly those
// noteheads, in exactly their document order:
// - Grace notes and the cue notes of ornaments and trills are included
//   (collectNotes, trillCueChords): they carry the main chord's seg/st/vc,
//   and leaving one out would pair every later note of the key with the
//   wrong head.
// - Notes the writer does not paint (noteDrawn, staffDrawn, disabled
//   segments) are left out.
// - The writer stable-sorts all page elements by elementLessThan. Within one
//   key all notes share the track, so only z can reorder them (a user-set
//   stacking order); the same stable sort here keeps the order identical
//   instead of relying on that being rare.
//
// "pitch" is the sounding MIDI pitch, ppitch(): the stored pitch plus the
// ottava/capo offset (for drums the variant's pitch) - what the MIDI
// renderer plays (compatmidirenderinternal: note->ppitch()). Under an 8va
// the plain pitch() would be an octave off the sound. Transposing
// instruments need no correction: the model's pitch is concert pitch.
//
// "tpc" is the spelling as written in the notation the SVG shows: tpc(),
// which follows the score's concert-pitch setting the layout used - tpc2
// (transposed) normally, tpc1 when the score was saved in concert pitch. An
// ottava does not change the spelling, so a note name derived from it is the
// name of the notehead the reader sees.
static JsonArray noteSpellingsJson(const Score* score, const std::unordered_map<const Segment*, int>& ids)
{
    JsonArray list;
    const auto trillCues = trillCueChords(score);
    const size_t tracks = score->ntracks();
    const Measure* first = score->firstMeasureMM();
    for (const Segment* s = first ? first->first(SegmentType::ChordRest) : nullptr; s;
         s = s->next1MM(SegmentType::ChordRest)) {
        const auto id = ids.find(s);
        if (id == ids.end() || !s->enabled()) {
            continue;
        }
        for (track_idx_t track = 0; track < tracks; ++track) {
            const EngravingItem* e = s->element(track);
            if (!e || !e->isChord() || !staffDrawn(s->measure(), track / VOICES, score)) {
                continue;
            }
            const Chord* chord = toChord(e);
            std::vector<EngravingItem*> notes;
            collectNotes(chord, notes);
            const auto trills = trillCues.find({ s, track });
            if (trills != trillCues.end()) {
                for (const Chord* cue : trills->second) {
                    collectNotes(cue, notes);
                }
            }
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                                       [](const EngravingItem* n) { return !noteDrawn(toNote(n)); }),
                        notes.end());
            if (notes.empty()) {
                continue;
            }
            std::stable_sort(notes.begin(), notes.end(), elementLessThan);

            JsonArray pairs;
            for (const EngravingItem* item : notes) {
                const Note* n = toNote(item);
                JsonArray pair;
                pair.append(n->ppitch());
                pair.append(n->tpc());
                pairs.append(pair);
            }
            JsonObject o;
            o.set("elid", id->second);
            o.set("staff", static_cast<int>(track / VOICES));
            o.set("voice", static_cast<int>(track % VOICES));
            o.set("notes", pairs);
            list.append(o);
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
    const auto segmentIds = chordRestSegmentIndex(score);
    JsonArray syllables;
    const bool syllablesComplete = lyricSyllablesJson(score, segmentIds, syllables);

    json.set("composer", composer(score));
    json.set("duration", score->duration());
    json.set("fileVersion", score->mscVersion());
    json.set("hasHarmonies", boolToString(score->hasHarmonies()));
    json.set("hasLyrics", boolToString(score->hasLyrics()));
    json.set("keySigs", keySigsJson(score, numbers));
    json.set("keysig", static_cast<int>(score->keysig()));
    if (syllablesComplete) {
        json.set("lyricSyllables", syllables);
    }
    json.set("lyrics", score->extractLyrics());
    json.set("measures", static_cast<int>(score->nmeasures()));
    json.set("mscoreVersion", score->mscoreVersion());
    json.set("noteSpellings", noteSpellingsJson(score, segmentIds));
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
