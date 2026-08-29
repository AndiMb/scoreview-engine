/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited and others
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

// shadow copy of src/framework/draw/internal/fontprovider.cpp — one marked
// diff: every Font is rescaled for engraving's DPI before it reaches the
// FontsEngine.
//
// Why: engraving lays out in units of DPI 1200, but pixelSizeForFont()
// converts point sizes with muse::draw's DPI of 360. The *drawing* side
// compensates — Painter::applyFontSizeScaling() multiplies every font by
// 1200/deviceDpi before drawText — but the *metrics* side (FontMetrics ->
// IFontProvider -> FontsEngine), which the text layout measures with, never
// did: text was measured 10/3 too narrow, which shifted every aligned text
// and let too much text into a system (the Duckwerk page-count class).
// This applies the same factor to the metrics side, so layout and drawing
// agree again.

#include "fontprovider.h"

#include "types/fontstypes.h"

#include "log.h"

using namespace muse;
using namespace muse::draw;

// shadow diff: see the header comment
static Font scaledForEngravingDpi(const Font& f)
{
    static constexpr double MU_ENGRAVING_DPI = 1200; // same as mu::engraving::DPI (cf. Painter::applyFontSizeScaling)
    Font sf = f;
    if (f.pixelSize() > 0) {
        sf.setPixelSize(static_cast<int>(f.pixelSize() * MU_ENGRAVING_DPI / DPI + 0.5));
    } else {
        sf.setPointSizeF(f.pointSizeF() * MU_ENGRAVING_DPI / DPI);
    }
    return sf;
}

int FontProvider::addSymbolFont(const muse::String& family, const io::path_t& path)
{
    UNUSED(family);
    UNUSED(path);
    return 1;
}

double FontProvider::lineSpacing(const muse::draw::Font& f) const
{
    return fontsEngine()->lineSpacing(scaledForEngravingDpi(f));
}

double FontProvider::xHeight(const muse::draw::Font& f) const
{
    return fontsEngine()->xHeight(scaledForEngravingDpi(f));
}

double FontProvider::height(const muse::draw::Font& f) const
{
    return fontsEngine()->height(scaledForEngravingDpi(f));
}

double FontProvider::ascent(const muse::draw::Font& f) const
{
    return fontsEngine()->ascent(scaledForEngravingDpi(f));
}

double FontProvider::capHeight(const muse::draw::Font& f) const
{
    return fontsEngine()->capHeight(scaledForEngravingDpi(f));
}

double FontProvider::descent(const muse::draw::Font& f) const
{
    return fontsEngine()->descent(scaledForEngravingDpi(f));
}

bool FontProvider::inFont(const muse::draw::Font& f, char32_t ucs4) const
{
    return fontsEngine()->inFont(scaledForEngravingDpi(f), ucs4);
}

// Text
double FontProvider::horizontalAdvance(const muse::draw::Font& f, const muse::String& string) const
{
    return fontsEngine()->horizontalAdvance(scaledForEngravingDpi(f), string.toStdU32String());
}

double FontProvider::horizontalAdvance(const muse::draw::Font& f, char32_t ucs4) const
{
    return fontsEngine()->horizontalAdvance(scaledForEngravingDpi(f), ucs4);
}

RectF FontProvider::boundingRect(const muse::draw::Font& f, const muse::String& string) const
{
    return fontsEngine()->boundingRect(scaledForEngravingDpi(f), string.toStdU32String());
}

RectF FontProvider::boundingRect(const muse::draw::Font& f, char32_t ucs4) const
{
    return fontsEngine()->boundingRect(scaledForEngravingDpi(f), ucs4);
}

RectF FontProvider::tightBoundingRect(const muse::draw::Font& f, const muse::String& string) const
{
    return fontsEngine()->tightBoundingRect(scaledForEngravingDpi(f), string.toStdU32String());
}
