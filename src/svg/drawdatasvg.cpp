#include "drawdatasvg.h"

#include <cmath>
#include <map>
#include <sstream>

#include "modularity/ioc.h"

#include "shadow/fontsengine.h"
#include "internal/ifontface.h"

#include <msdfgen.h>

using namespace muse;
using namespace muse::draw;

namespace sve {
// Number formatting: enough precision for 1/100 px at page scale, no
// trailing zeros — keeps files small and diffs readable.
static std::string fmt(double v)
{
    if (std::abs(v) < 1e-9) {
        return "0";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s;
}

static std::string xmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

static std::string colorStr(const Color& c)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.red(), c.green(), c.blue());
    return buf;
}

// stroke="..." [stroke-opacity] [width, cap, join, dashes] or stroke="none"
static void penAttrs(std::ostream& os, const Pen& pen)
{
    if (pen.style() == PenStyle::NoPen) {
        os << " stroke=\"none\"";
        return;
    }
    os << " stroke=\"" << colorStr(pen.color()) << "\"";
    if (pen.color().alpha() != 255) {
        os << " stroke-opacity=\"" << fmt(pen.color().alpha() / 255.0) << "\"";
    }
    os << " stroke-width=\"" << fmt(pen.widthF()) << "\"";
    switch (pen.capStyle()) {
    case PenCapStyle::FlatCap: break;                                       // butt is the SVG default
    case PenCapStyle::SquareCap: os << " stroke-linecap=\"square\""; break;
    case PenCapStyle::RoundCap: os << " stroke-linecap=\"round\""; break;
    default: break;
    }
    switch (pen.joinStyle()) {
    case PenJoinStyle::BevelJoin: os << " stroke-linejoin=\"bevel\""; break;
    case PenJoinStyle::RoundJoin: os << " stroke-linejoin=\"round\""; break;
    default: break;                                                          // miter is the SVG default
    }
    if (pen.style() != PenStyle::SolidLine) {
        std::vector<double> pattern = pen.dashPattern();
        if (!pattern.empty()) {
            os << " stroke-dasharray=\"";
            for (size_t i = 0; i < pattern.size(); ++i) {
                // dash pattern is in units of pen width, as in Qt
                os << (i ? "," : "") << fmt(pattern[i] * pen.widthF());
            }
            os << "\"";
        }
    }
}

static void brushAttrs(std::ostream& os, const Brush& brush)
{
    if (brush.style() == BrushStyle::NoBrush) {
        os << " fill=\"none\"";
        return;
    }
    os << " fill=\"" << colorStr(brush.color()) << "\"";
    if (brush.color().alpha() != 255) {
        os << " fill-opacity=\"" << fmt(brush.color().alpha() / 255.0) << "\"";
    }
}

static void pathData(std::ostream& os, const PainterPath& path)
{
    os << "d=\"";
    size_t n = path.elementCount();
    for (size_t i = 0; i < n; ++i) {
        PainterPath::Element e = path.elementAt(i);
        switch (e.type) {
        case PainterPath::ElementType::MoveToElement:
            os << "M" << fmt(e.x) << " " << fmt(e.y);
            break;
        case PainterPath::ElementType::LineToElement:
            os << "L" << fmt(e.x) << " " << fmt(e.y);
            break;
        case PainterPath::ElementType::CurveToElement: {
            PainterPath::Element c2 = path.elementAt(i + 1);
            PainterPath::Element end = path.elementAt(i + 2);
            os << "C" << fmt(e.x) << " " << fmt(e.y)
               << " " << fmt(c2.x) << " " << fmt(c2.y)
               << " " << fmt(end.x) << " " << fmt(end.y);
            i += 2;
            break;
        }
        case PainterPath::ElementType::CurveToDataElement:
            break;   // consumed with CurveToElement
        }
    }
    os << "\"";
}

// Glyph outlines: msdfgen shape units are pixels at the face's loaded pixel
// size, y-up; the <use> transform flips and scales into place.
static void shapeData(std::ostream& os, const msdfgen::Shape& shape)
{
    os << "d=\"";
    for (const msdfgen::Contour& contour : shape.contours) {
        if (contour.edges.empty()) {
            continue;
        }
        msdfgen::Point2 start = contour.edges.front().point(0);
        os << "M" << fmt(start.x) << " " << fmt(start.y);
        for (const msdfgen::EdgeSegment& e : contour.edges) {
            switch (e.actualType) {
            case msdfgen::EdgeSegment::ActualType::Linear:
                os << "L" << fmt(e.segments.linear.p[1].x) << " " << fmt(e.segments.linear.p[1].y);
                break;
            case msdfgen::EdgeSegment::ActualType::Quadratic:
                os << "Q" << fmt(e.segments.quadratic.p[1].x) << " " << fmt(e.segments.quadratic.p[1].y)
                   << " " << fmt(e.segments.quadratic.p[2].x) << " " << fmt(e.segments.quadratic.p[2].y);
                break;
            case msdfgen::EdgeSegment::ActualType::Cubic:
                os << "C" << fmt(e.segments.cubic.p[1].x) << " " << fmt(e.segments.cubic.p[1].y)
                   << " " << fmt(e.segments.cubic.p[2].x) << " " << fmt(e.segments.cubic.p[2].y)
                   << " " << fmt(e.segments.cubic.p[3].x) << " " << fmt(e.segments.cubic.p[3].y);
                break;
            default:
                break;
            }
        }
        os << "Z";
    }
    os << "\"";
}

namespace {
struct SvgContext {
    FontsEngine* fontsEngine = nullptr;
    // one def per distinct glyph; keyed by face identity + glyph index
    std::map<std::pair<const IFontFace*, glyph_idx_t>, std::string> glyphIds;
    std::ostringstream defs;
    bool pixmapSeen = false;

    std::string glyphDef(const IFontFace* face, glyph_idx_t idx)
    {
        auto key = std::make_pair(face, idx);
        auto it = glyphIds.find(key);
        if (it != glyphIds.end()) {
            return it->second;
        }
        std::string id = "g" + std::to_string(glyphIds.size());
        const msdfgen::Shape& shape = face->glyphShape(idx);
        defs << "<path id=\"" << id << "\" ";
        shapeData(defs, shape);
        defs << "/>\n";
        glyphIds.emplace(key, id);
        return id;
    }
};
}

static bool isIdentity(const Transform& t)
{
    return t.m11() == 1.0 && t.m12() == 0.0 && t.m21() == 0.0 && t.m22() == 1.0
           && t.dx() == 0.0 && t.dy() == 0.0;
}

static void writeTransformAttr(std::ostream& os, const Transform& t)
{
    if (isIdentity(t)) {
        return;
    }
    os << " transform=\"matrix(" << fmt(t.m11()) << " " << fmt(t.m12()) << " "
       << fmt(t.m21()) << " " << fmt(t.m22()) << " "
       << fmt(t.dx()) << " " << fmt(t.dy()) << ")\"";
}

static void writeData(std::ostream& os, SvgContext& ctx, const DrawData::Data& data, const DrawData::State& state)
{
    for (const DrawPath& p : data.paths) {
        os << "<path ";
        pathData(os, p.path);
        if (p.path.fillRule() == PainterPath::FillRule::OddEvenFill) {
            os << " fill-rule=\"evenodd\"";
        }
        if (p.mode == DrawMode::Stroke) {
            os << " fill=\"none\"";
            penAttrs(os, p.pen);
        } else if (p.mode == DrawMode::Fill) {
            brushAttrs(os, p.brush);
            os << " stroke=\"none\"";
        } else {
            brushAttrs(os, p.brush);
            penAttrs(os, p.pen);
        }
        os << "/>\n";
    }

    for (const DrawPolygon& p : data.polygons) {
        if (p.polygon.empty()) {
            continue;
        }
        bool isLine = p.mode == PolygonMode::Polyline;
        os << (isLine ? "<polyline points=\"" : "<polygon points=\"");
        for (size_t i = 0; i < p.polygon.size(); ++i) {
            const PointF& pt = p.polygon[i];
            os << (i ? " " : "") << fmt(pt.x()) << "," << fmt(pt.y());
        }
        os << "\"";
        if (isLine) {
            os << " fill=\"none\"";
            penAttrs(os, state.pen);
        } else {
            if (p.mode == PolygonMode::OddEven) {
                os << " fill-rule=\"evenodd\"";
            }
            brushAttrs(os, state.brush);
            penAttrs(os, state.pen);
        }
        os << "/>\n";
    }

    for (const DrawText& t : data.texts) {
        // DrawText's point (rect.topLeft() in Point mode) is the baseline
        // origin, exactly as the layout positioned it.
        PointF origin = t.rect.topLeft();
        // The SvgPaintProvider reports engraving's DPI, so the recorded font
        // still carries the layout's point size; convert for the 360-DPI
        // FontsEngine here, exactly once (the metrics-side twin lives in the
        // shadow fontprovider).
        Font f = state.font;
        static constexpr double MU_ENGRAVING_DPI = 1200; // same as mu::engraving::DPI
        static constexpr double DRAW_DPI = 360;          // muse::draw DPI (fontstypes.h)
        if (f.pixelSize() > 0) {
            f.setPixelSize(static_cast<int>(f.pixelSize() * MU_ENGRAVING_DPI / DRAW_DPI + 0.5));
        } else {
            f.setPointSizeF(f.pointSizeF() * MU_ENGRAVING_DPI / DRAW_DPI);
        }
        std::vector<FontsEngine::GlyphRun> runs = ctx.fontsEngine->glyphRuns(f, t.text.toStdU32String());
        std::string fill = colorStr(state.pen.color());
        for (const FontsEngine::GlyphRun& run : runs) {
            std::string id = ctx.glyphDef(run.face, run.glyphIdx);
            os << "<use xlink:href=\"#" << id << "\" transform=\"translate("
               << fmt(origin.x() + run.x) << " " << fmt(origin.y() + run.y) << ") scale("
               << fmt(run.scale) << " " << fmt(-run.scale) << ")\" fill=\"" << fill << "\"";
            if (state.pen.color().alpha() != 255) {
                os << " fill-opacity=\"" << fmt(state.pen.color().alpha() / 255.0) << "\"";
            }
            os << "/>\n";
        }
    }

    if (!data.pixmaps.empty()) {
        ctx.pixmapSeen = true;
        os << "<!-- " << data.pixmaps.size() << " pixmap(s) not rendered -->\n";
    }
}

static void writeItem(std::ostream& os, SvgContext& ctx, const DrawData& dd, const DrawData::Item& item, bool isRoot)
{
    bool named = !isRoot && !item.name.empty();
    if (named) {
        os << "<g class=\"" << xmlEscape(item.name) << "\">\n";
    }

    for (const DrawData::Data& data : item.datas) {
        if (data.empty()) {
            continue;
        }
        const DrawData::State& state = dd.states.at(data.state);
        bool hasTransform = !isIdentity(state.transform);
        if (hasTransform) {
            os << "<g";
            writeTransformAttr(os, state.transform);
            os << ">\n";
        }
        writeData(os, ctx, data, state);
        if (hasTransform) {
            os << "</g>\n";
        }
    }

    for (const DrawData::Item& child : item.chilren) {
        writeItem(os, ctx, dd, child, false);
    }

    if (named) {
        os << "</g>\n";
    }
}

ByteArray DrawDataSvg::toSvg(const DrawDataPtr& data, const std::string& title)
{
    SvgContext ctx;
    auto engine = modularity::globalIoc()->resolve<IFontsEngine>("scoreview-engine");
    // The registered engine is our shadow FontsEngine — the very instance the
    // layout measured with.
    ctx.fontsEngine = static_cast<FontsEngine*>(engine.get());

    std::ostringstream body;
    writeItem(body, ctx, *data, data->item, true);

    const RectF& vp = data->viewport;
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       // physical size in mm, as the Qt generator writes it — viewport units
       // are engraving units (DPI 1200)
       << "<svg width=\"" << fmt(vp.width() / 1200.0 * 25.4) << "mm\" height=\"" << fmt(vp.height() / 1200.0 * 25.4) << "mm\""
       << " viewBox=\"" << fmt(vp.x()) << " " << fmt(vp.y()) << " " << fmt(vp.width()) << " " << fmt(vp.height()) << "\""
       << " xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\""
       << " version=\"1.2\" baseProfile=\"tiny\">\n"
       << "<title>" << xmlEscape(title) << "</title>\n"
       << "<desc>Generated by scoreview-engine</desc>\n";
    std::string defs = ctx.defs.str();
    if (!defs.empty()) {
        os << "<defs>\n" << defs << "</defs>\n";
    }
    os << body.str();
    os << "</svg>\n";

    std::string s = os.str();
    return ByteArray(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
}
