#pragma once

// Qt-free port of the webmscore fork's PositionJsonWriter
// (web/importexport/positionjsonwriter.{h,cpp}): spos (SEGMENT) and mpos
// (MEASURE) JSON from layout data, on muse's serialization instead of
// QJson. The INotationWriter plumbing is not carried over — callers take
// the bytes. Keys are written in the alphabetical order QJsonObject used,
// so existing consumers see the same document shape.

#include "global/types/bytearray.h"

namespace mu::engraving {
class Score;
}

namespace sve {
class PositionsWriter
{
public:
    enum class ElementType {
        SEGMENT,
        MEASURE
    };

    explicit PositionsWriter(ElementType elementType)
        : m_elementType(elementType) {}

    //! measureReference: for MEASURE, the score whose measure chain numbers
    //! the elements and events (the main score, for a view); null = score.
    muse::ByteArray json(const mu::engraving::Score* score,
                         const mu::engraving::Score* measureReference = nullptr) const;

private:
    ElementType m_elementType;
};
}
