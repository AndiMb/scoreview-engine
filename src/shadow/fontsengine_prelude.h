#pragma once

// The #ifndef MUSE_MODULE_DRAW_USE_QTTEXTDRAW branch of fontsengine.cpp
// references four symbols that no longer exist in the tree — they were lost
// when the branch stopped being compiled (fontrendercache/MU_DRAW_TEXTRENDER
// cleanup). Values and scaleRect are taken verbatim from history commit
// 9a05283009 (fontsengine.cpp lines 44-60); "mu::" is the old namespace from
// before the muse rename.
//
// Force-included (per -include) only for the shadow copy of fontsengine.cpp
// (src/shadow/fontsengine.cpp), keeping the copy's diff against upstream
// minimal. The drift guard pins the upstream file's blob id: if upstream ever
// touches fontsengine.cpp, both the copy and this prelude must be re-checked.

#include "global/types/bytearray.h"
#include "draw/types/geometry.h"

namespace mu = muse;

static const double TEXT_LINE_SCALE = 1.2;

static const int SDF_WIDTH = 64;
static const int SDF_HEIGHT = 64;

static inline muse::RectF scaleRect(const muse::RectF& r, double scale)
{
    return muse::RectF(r.x() * scale, r.y() * scale, r.width() * scale, r.height() * scale);
}
