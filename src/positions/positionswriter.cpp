#include "positionswriter.h"

#include <cmath>
#include <unordered_map>

#include "global/serialization/json.h"

#include "log.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/page.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/system.h"
#include "engraving/types/types.h"

#include "segmentindex.h"

using namespace muse;
using namespace mu::engraving;

namespace sve {
// The fork's writer scaled all geometry by pngDpiResolution() == 1.0
// ("for compatibility with previous webmscore versions"); the factor is
// dropped here.

static void writeElementPosition(JsonArray& elements, int id, const PointF& pos, const PointF& sPos, page_idx_t pageIndex)
{
    JsonObject el;
    el.set("id", id);
    el.set("page", static_cast<int>(pageIndex));
    el.set("sx", sPos.x());
    el.set("sy", sPos.y());
    el.set("x", pos.x());
    el.set("y", pos.y());
    elements.append(el);
}

static void writeEventPosition(JsonArray& events, int id, int time)
{
    JsonObject ev;
    ev.set("elid", id);
    ev.set("position", time);
    events.append(ev);
}

static void writeMeasureEvents(JsonArray& events, Measure* m, int offset, const std::unordered_map<const Segment*, int>& segments)
{
    for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
        // The index is built over the MM chain from firstMeasureMM; a segment
        // the repeat walk reaches but the index does not know has no number to
        // hand a player. Skip it rather than let at() throw across the C ABI.
        const auto id = segments.find(s);
        if (id == segments.end()) {
            continue;
        }
        int tick = s->tick().ticks() + offset;
        int time = lrint(m->score()->repeatList().utick2utime(tick) * 1000);

        writeEventPosition(events, id->second, time);
    }
}

static void writePageSize(JsonObject& json, const Score* score)
{
    const Page* page = score->pages().at(0);  // all pages sizes should be the same as the first page

    JsonObject pageSize;
    pageSize.set("height", page->height());
    pageSize.set("width", page->width());
    json.set("pageSize", pageSize);
}

static void writeSegmentsPositions(JsonObject& json, const Score* score)
{
    const std::unordered_map<const Segment*, int> ids = chordRestSegmentIndex(score);

    JsonArray elements;

    Measure* measure = score->firstMeasureMM();
    for (Segment* segment = (measure ? measure->first(SegmentType::ChordRest) : nullptr);
         segment; segment = segment->next1MM(SegmentType::ChordRest)) {
        double sx = 0;
        size_t tracks = score->nstaves() * VOICES;
        for (size_t track = 0; track < tracks; track++) {
            EngravingItem* e = segment->element(static_cast<int>(track));
            // Geometry lives in the layout data, not on the item itself. Not
            // every item has been laid out (a measure may be empty in a given
            // track), hence the isSetBbox() guard: reading an unset bbox is an
            // LD_ACCESS::CHECK violation and logs an error per element.
            if (e && e->ldata() && e->ldata()->isSetBbox()) {
                sx = std::max(sx, e->ldata()->bbox().width());
            }
        }

        // Everything below is layout output. json() refuses a score that was
        // never laid out; this guard covers the partially laid out one, where
        // a measure can still sit on no system at all.
        const System* system = segment->measure()->system();
        const auto id = ids.find(segment);
        if (!system || id == ids.end()) {
            continue;
        }

        double sy = system->height();

        // truncated to int as in the fork's writer
        int x = segment->pagePos().x();
        int y = segment->pagePos().y();

        page_idx_t pageIndex = score->pageIdx(system->page());

        writeElementPosition(elements, id->second, PointF(x, y), PointF(sx, sy), pageIndex);
    }

    json.set("elements", elements);
}

static void writeMeasuresPositions(JsonObject& json, const Score* score)
{
    int id = 0;

    JsonArray elements;

    for (Measure* measure = score->firstMeasureMM(); measure; measure = measure->nextMeasureMM()) {
        // Consume the number even when the measure is skipped: writeEvents-
        // Positions numbers the same chain unconditionally, and the two sides
        // have to keep agreeing on which measure is which.
        const int elementId = id++;

        const System* system = measure->system();
        if (!system) {
            continue;
        }

        double sx = measure->ldata() && measure->ldata()->isSetBbox() ? measure->ldata()->bbox().width() : 0.0;
        double sy = system->height();
        double x = measure->pagePos().x();
        double y = system->pagePos().y();

        page_idx_t pageIndex = score->pageIdx(system->page());

        writeElementPosition(elements, elementId, PointF(x, y), PointF(sx, sy), pageIndex);
    }

    json.set("elements", elements);
}

static void writeEventsPositions(JsonObject& json, const Score* score, PositionsWriter::ElementType elementType)
{
    JsonArray events;

    score->masterScore()->setExpandRepeats(true);

    std::unordered_map<const Segment*, int> segmentIds;
    std::unordered_map<const Measure*, int> measureIds;
    if (elementType == PositionsWriter::ElementType::SEGMENT) {
        segmentIds = chordRestSegmentIndex(score);
    } else {
        int id = 0;
        for (Measure* m = score->firstMeasureMM(); m; m = m->nextMeasureMM()) {
            measureIds[m] = id++;
        }
    }

    for (const RepeatSegment* repeatSegment : score->repeatList()) {
        int startTick = repeatSegment->tick;
        int endTick = startTick + repeatSegment->len();
        int tickOffset = repeatSegment->utick - repeatSegment->tick;
        for (Measure* measure = score->tick2measureMM(Fraction::fromTicks(startTick)); measure; measure = measure->nextMeasureMM()) {
            if (elementType == PositionsWriter::ElementType::SEGMENT) {
                writeMeasureEvents(events, measure, tickOffset, segmentIds);
            } else {
                const auto id = measureIds.find(measure);
                if (id != measureIds.end()) {
                    int tick = measure->tick().ticks() + tickOffset;
                    int time = std::lrint(measure->score()->repeatList().utick2utime(tick) * 1000);

                    writeEventPosition(events, id->second, time);
                }
            }

            if (measure->endTick().ticks() >= endTick) {
                break;
            }
        }
    }

    json.set("events", events);
}

ByteArray PositionsWriter::json(const Score* score) const
{
    // Positions are layout data: every geometry read below goes through
    // System*, which does not exist before doLayout(), and writePageSize wants
    // a first page. load(..., doLayout: false) is a documented mode - answer
    // emptily so the caller gets an error, not a wasm trap that unwinds
    // without running a single destructor.
    if (!score || score->pages().empty()) {
        LOGW() << "no positions: the score has no layout";
        return ByteArray();
    }

    JsonObject json;
    if (m_elementType == ElementType::SEGMENT) {
        writeSegmentsPositions(json, score);
    } else {
        writeMeasuresPositions(json, score);
    }
    writeEventsPositions(json, score, m_elementType);
    writePageSize(json, score);

    return JsonDocument(json).toJson(JsonDocument::Format::Compact);
}
}
