#pragma once

// Renders one page of a laid-out score to SVG. The element iteration is a
// Qt-free port of upstream's iex_imagesexport SvgWriter (staff-lines pass,
// then elements in paint order); each element's drawing is recorded into a
// BufferedPaintProvider under a group whose class attribute carries the same
// identity tokens the webmscore fork stamps: the element's type name, the
// segment number the position export uses (segmentindex.h), staff and voice.
// DrawDataSvg then serializes the recording.

#include "global/types/bytearray.h"

namespace mu::engraving {
class Score;
}

namespace sve {
class SvgWriter
{
public:
    struct Options {
        //! White rectangle over the page rect, before any element is drawn.
        //! Off by default: pages are composited by the consumer, and
        //! webmscore's saveSvg() defaults the same way. The wasm export passes
        //! the caller's value through; the CLI takes this default.
        bool drawPageBackground = false;
    };

    static muse::ByteArray write(mu::engraving::Score* score, size_t pageNumber);
    static muse::ByteArray write(mu::engraving::Score* score, size_t pageNumber, const Options& opt);
};
}
