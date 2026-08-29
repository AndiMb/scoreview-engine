#include "positionswriter.h"

#include <cmath>
#include <unordered_map>

#include "global/serialization/json.h"

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
        int tick = s->tick().ticks() + offset;
        int id = segments.at(s);
        int time = lrint(m->score()->repeatList().utick2utime(tick) * 1000);

        writeEventPosition(events, id, time);
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

        double sy = segment->measure()->system()->height();

        // truncated to int as in the fork's writer
        int x = segment->pagePos().x();
        int y = segment->pagePos().y();

        Page* page = segment->measure()->system()->page();
        page_idx_t pageIndex = score->pageIdx(page);

        writeElementPosition(elements, ids.at(segment), PointF(x, y), PointF(sx, sy), pageIndex);
    }

    json.set("elements", elements);
}

static void writeMeasuresPositions(JsonObject& json, const Score* score)
{
    int id = 0;

    JsonArray elements;

    for (Measure* measure = score->firstMeasureMM(); measure; measure = measure->nextMeasureMM()) {
        double sx = measure->ldata() && measure->ldata()->isSetBbox() ? measure->ldata()->bbox().width() : 0.0;
        double sy = measure->system()->height();
        double x = measure->pagePos().x();
        double y = measure->system()->pagePos().y();

        Page* page = measure->system()->page();
        page_idx_t pageIndex = score->pageIdx(page);

        writeElementPosition(elements, id, PointF(x, y), PointF(sx, sy), pageIndex);

        id++;
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
                int tick = measure->tick().ticks() + tickOffset;
                int id = measureIds.at(measure);
                int time = std::lrint(measure->score()->repeatList().utick2utime(tick) * 1000);

                writeEventPosition(events, id, time);
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
