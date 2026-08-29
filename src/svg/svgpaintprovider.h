#pragma once

// BufferedPaintProvider that reports engraving's DPI (1200) instead of the
// hardcoded 360. Painter::applyFontSizeScaling multiplies the *current* font
// by 1200/deviceLogicalDpi before every drawText/drawSymbol — with 360 that
// is a factor 10/3 which COMPOUNDS when an element draws several texts
// without resetting the font (multi-symbol articulations blow up to 10x and
// more). With 1200 the factor is 1, the recorded fonts keep the layout's
// point sizes, and DrawDataSvg applies the engraving-DPI conversion itself,
// exactly once (the same conversion the shadow fontprovider applies on the
// metrics side).

#include "bufferedpaintprovider.h"

namespace sve {
class SvgPaintProvider : public muse::draw::BufferedPaintProvider
{
public:
    double deviceLogicalDpi() const override { return 1200; }   // mu::engraving::DPI
};
}
