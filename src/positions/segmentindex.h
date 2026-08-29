/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Taken over from the webmscore fork (there patched into engraving as
// dom/segmentindex.h); include paths adjusted. Header-only and Qt-free.

#ifndef MU_ENGRAVING_SEGMENTINDEX_H
#define MU_ENGRAVING_SEGMENTINDEX_H

#include <unordered_map>

#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"

namespace mu::engraving {
//---------------------------------------------------------
//   chordRestSegmentIndex
///   Numbers the ChordRest segments of a score in layout order.
///
///   Two exports have to agree on this numbering. The position export hands
///   the numbers to a player, which uses them to know what sounds when; the
///   SVG export stamps the same number on the elements it draws, so that the
///   player can find the notehead that belongs to a moment in time. They used
///   to count separately and agreed by coincidence - a picture and a timeline
///   that drift apart by one segment are close to undebuggable, so both now
///   count from here.
///
///   Multimeasure rests are followed (firstMeasureMM/next1MM) because that is
///   the layout both exports describe.
//---------------------------------------------------------

inline std::unordered_map<const Segment*, int> chordRestSegmentIndex(const Score* score)
{
    std::unordered_map<const Segment*, int> ids;
    if (!score) {
        return ids;
    }

    int id = 0;
    const Measure* measure = score->firstMeasureMM();
    for (const Segment* segment = (measure ? measure->first(SegmentType::ChordRest) : nullptr);
         segment; segment = segment->next1MM(SegmentType::ChordRest)) {
        ids[segment] = id++;
    }

    return ids;
}
}

#endif // MU_ENGRAVING_SEGMENTINDEX_H
