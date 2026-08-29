#include "svgwriter.h"

#include <algorithm>

#include "painter.h"

#include "engraving/dom/measure.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/page.h"
#include "engraving/dom/score.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafflines.h"
#include "engraving/dom/system.h"
#include "engraving/rendering/score/scorerenderer.h"

#include "positions/segmentindex.h"
#include "drawdatasvg.h"
#include "svgpaintprovider.h"

#include "log.h"

using namespace muse;
using namespace muse::draw;
using namespace mu::engraving;

namespace sve {
// The class attribute, as the webmscore fork's getClass() builds it: the
// type name says what an element is, "seg-N" (the very number the position
// export hands to a player), "st-N" and "vc-N" say which one it is.
static std::string classTag(const EngravingItem* e, const std::unordered_map<const Segment*, int>& segmentIds)
{
    if (!e) {
        return std::string();
    }
    std::string name = e->typeName();

    if (const EngravingItem* ancestor = e->findAncestor(ElementType::SEGMENT)) {
        const auto it = segmentIds.find(static_cast<const Segment*>(ancestor));
        if (it != segmentIds.end()) {
            name += " seg-" + std::to_string(it->second);
        }
    }

    // Guarded on the track, not on staffIdx(): staffIdx() is track2staff() and
    // happily divides an unset track into a nonsense staff number.
    if (e->track() != muse::nidx) {
        name += " st-" + std::to_string(static_cast<int>(e->staffIdx()));
        name += " vc-" + std::to_string(static_cast<int>(e->voice()));
    }

    return name;
}

ByteArray SvgWriter::write(Score* score, size_t pageNumber)
{
    return write(score, pageNumber, Options());
}

ByteArray SvgWriter::write(Score* score, size_t pageNumber, const Options& opt)
{
    IF_ASSERT_FAILED(score) {
        return ByteArray();
    }

    const std::vector<Page*>& pages = score->pages();
    if (pageNumber >= pages.size()) {
        return ByteArray();
    }
    Page* page = pages.at(pageNumber);

    score->setPrinting(true);   // don't print page break symbols etc.
    MScore::pdfPrinting = true;
    MScore::svgPrinting = true;

    const std::unordered_map<const Segment*, int> segmentIds = chordRestSegmentIndex(score);

    RectF pageRect = page->pageBoundingRect();

    auto provider = std::make_shared<SvgPaintProvider>();
    Painter painter(provider, "svgwriter");
    painter.setAntialiasing(true);

    if (opt.drawPageBackground) {
        painter.fillRect(pageRect, Color::WHITE);
    }

    rendering::score::ScoreRenderer renderer;
    rendering::PaintOptions eopt;
    eopt.isPrinting = true;

    // 1st pass: StaffLines — drawn once per system instead of per measure,
    // ported from upstream's SvgWriter.
    for (const System* system : page->systems()) {
        size_t stavesCount = system->staves().size();

        for (size_t staffIndex = 0; staffIndex < stavesCount; ++staffIndex) {
            if (!score->staff(staffIndex)->show()) {
                continue;   // ignore invisible staves
            }

            if (system->staves().empty() || !system->staff(staffIndex)->show()) {
                continue;
            }

            Measure* firstMeasure = system->firstMeasure();
            if (!firstMeasure) {   // only boxes, hence no staff lines
                continue;
            }

            auto paintConcatenated = [&](StaffLines* sl, const Shape& shape, const Shape& mask) {
                sl->mutldata()->setShape(shape);
                sl->mutldata()->setMask(mask);
                provider->beginObject(classTag(sl, segmentIds));
                renderer.paintItem(painter, sl, eopt);
                provider->endObject();
                delete sl;
            };

            StaffLines* concatenatedSL = nullptr;
            Shape concatenatedShape;
            Shape concatenatedMask;
            StaffType* prevStaffType = nullptr;
            for (MeasureBase* measure = firstMeasure; measure; measure = system->nextMeasure(measure)) {
                if (!measure->isMeasure()) {
                    if (concatenatedSL != nullptr) {
                        paintConcatenated(concatenatedSL, concatenatedShape, concatenatedMask);
                        concatenatedSL = nullptr;
                        prevStaffType = nullptr;
                    }
                    continue;
                }

                Measure* m = toMeasure(measure);
                StaffLines* sl = m->staffLines(static_cast<int>(staffIndex));

                if ((!m->visible(staffIndex) && !m->isCutawayClef(staffIndex)) || !sl->visible()
                    || (score->staff(staffIndex)->staffType(m->tick()) != prevStaffType)) {
                    if (concatenatedSL != nullptr) {
                        paintConcatenated(concatenatedSL, concatenatedShape, concatenatedMask);
                        concatenatedSL = nullptr;
                        prevStaffType = nullptr;
                    }
                }

                if (concatenatedSL == nullptr) {
                    if ((m->visible(staffIndex) || m->isCutawayClef(staffIndex)) && sl->visible()) {
                        concatenatedSL = toStaffLines(sl->clone());
                        concatenatedShape = sl->ldata()->shape();
                        concatenatedMask = sl->ldata()->mask();
                        prevStaffType = score->staff(staffIndex)->staffType(m->tick());
                    }
                } else {
                    double lastX = sl->ldata()->bbox().right()
                                   + sl->pagePos().x()
                                   - concatenatedSL->pagePos().x();
                    std::vector<LineF> lines = concatenatedSL->lines();
                    for (size_t l = 0, c = lines.size(); l < c; l++) {
                        lines[l].setP2(PointF(lastX, lines[l].p2().y()));
                    }
                    concatenatedSL->setLines(lines);
                    concatenatedShape.add(sl->ldata()->shape().translated(sl->pagePos() - concatenatedSL->pagePos()));
                    concatenatedMask.add(sl->ldata()->mask().translated(sl->pagePos() - concatenatedSL->pagePos()));
                }
            }

            if (concatenatedSL != nullptr) {
                paintConcatenated(concatenatedSL, concatenatedShape, concatenatedMask);
                concatenatedSL = nullptr;
                prevStaffType = nullptr;
            }
        }
    }

    // 2nd pass: the rest of the elements, in paint order
    std::vector<EngravingItem*> elements = page->elements();
    std::sort(elements.begin(), elements.end(), elementLessThan);

    for (const EngravingItem* element : elements) {
        if (!element->collectForDrawing()) {
            continue;
        }

        // Match BspTree::items, which checks for bbox intersection
        // and empty RectF intersects with nothing
        if (element->ldata()->bbox().isEmpty()) {
            continue;
        }

        if (element->type() == ElementType::STAFF_LINES) {
            continue;   // handled in the 1st pass above
        }

        provider->beginObject(classTag(element, segmentIds));
        renderer.paintItem(painter, element, eopt);
        provider->endObject();
    }

    painter.endDraw();

    score->setPrinting(false);
    MScore::pdfPrinting = false;
    MScore::svgPrinting = false;

    DrawDataPtr dd = provider->drawData();
    IF_ASSERT_FAILED(dd) {
        return ByteArray();
    }
    // The provider's viewport is the painter's default; the page rect is the
    // document geometry the consumer needs.
    dd->viewport = RectF(0, 0, pageRect.width(), pageRect.height());

    std::string title = score->name().toStdString();
    if (pages.size() > 1) {
        title += " (" + std::to_string(pageNumber + 1) + ")";
    }

    return DrawDataSvg::toSvg(dd, title);
}
}
