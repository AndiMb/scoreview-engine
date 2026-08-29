#pragma once

// Serializes a DrawData buffer (the recording a BufferedPaintProvider makes
// of one page) into an SVG document. Text becomes glyph outlines: each run
// is shaped through the same FontsEngine the layout measured with
// (shadow fontsengine's glyphRuns), every distinct glyph is emitted once as
// a <path> in <defs> and placed with <use> — self-contained SVG, no font
// delivery (the Phase-0 decision).

#include <string>

#include "global/types/bytearray.h"
#include "types/drawdata.h"

namespace sve {
class DrawDataSvg
{
public:
    static muse::ByteArray toSvg(const muse::draw::DrawDataPtr& data, const std::string& title);
};
}
