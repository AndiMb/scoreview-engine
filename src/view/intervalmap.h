#pragma once

// Semitones to an index into mu::engraving::Interval::allIntervals - the
// spelling MuseScore's transpose dialog offers first for each distance, so a
// transposed view reads the way a musician would have written it: a whole
// tone is a major second (never a diminished third), five semitones a perfect
// fourth. The tritone is the one distance with two equally plain spellings;
// up it is an augmented fourth, down a diminished fifth (F to B, F to C flat
// would be the oddity in each direction).
//
// Header-only and free of the engine on purpose: the unit tests take it
// directly, without linking engraving (see tests/main.cpp).

namespace sve {
//! The index for |semitones|, 1 ... 12, as an upward interval; the caller
//! flips it for a downward transposition. -1 for 0 or anything out of range.
inline int intervalIndexFor(int semitones)
{
    const bool down = semitones < 0;
    const int n = down ? -semitones : semitones;
    switch (n) {
    case 1: return 3;    // minor second
    case 2: return 4;    // major second
    case 3: return 7;    // minor third
    case 4: return 8;    // major third
    case 5: return 11;   // perfect fourth
    case 6: return down ? 13 : 12;   // diminished fifth / augmented fourth
    case 7: return 14;   // perfect fifth
    case 8: return 17;   // minor sixth
    case 9: return 18;   // major sixth
    case 10: return 21;  // minor seventh
    case 11: return 22;  // major seventh
    case 12: return 25;  // perfect octave
    default: return -1;
    }
}
}
