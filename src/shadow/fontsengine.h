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
#pragma once

// shadow copy of src/framework/draw/internal/fontsengine.h — one marked diff:
// the GlyphRun struct and glyphRuns() method, which give the SVG writer the
// glyph identities and pen positions that render() computes internally but
// only exposes as SDF bitmaps.

#include <vector>
#include <functional>

#include "ifontsengine.h"

#include "global/modularity/ioc.h"
#include "ifontsdatabase.h"

//#include "fontrendercache.h"

namespace muse::draw {
class IFontFace;
class FontsEngine : public IFontsEngine, public Contextable
{
    GlobalInject<IFontsDatabase> fontsDatabase;

public:
    FontsEngine(const modularity::ContextPtr& iocCtx)
        : Contextable(iocCtx) {}
    ~FontsEngine();

    void init();

    double lineSpacing(const Font& f) const override;
    double xHeight(const Font& f) const override;
    double height(const Font& f) const override;
    double capHeight(const Font& f) const override;
    double ascent(const Font& f) const override;
    double descent(const Font& f) const override;

    bool inFont(const Font& f, char32_t ucs4) const override;

    double horizontalAdvance(const Font& f, const char32_t& ch) const override;
    double horizontalAdvance(const Font& f, const std::u32string& text) const override;

    RectF boundingRect(const Font& f, const char32_t& ch) const override;
    RectF boundingRect(const Font& f, const std::u32string& text) const override;
    RectF tightBoundingRect(const Font& f, const std::u32string& text) const override;

    // For draw
    std::vector<GlyphImage> render(const Font& f, const std::u32string& text) const override;

    // shadow diff: glyph-run access for the SVG writer — same face selection,
    // substitution and advance arithmetic as render(), but yielding glyph
    // identities and pen positions instead of SDF bitmaps.
    struct GlyphRun {
        const IFontFace* face = nullptr;
        glyph_idx_t glyphIdx = 0;
        double x = 0.0;        // pen position relative to the text origin, px
        double y = 0.0;        // baseline of the run's line, px (y-down)
        double scale = 0.0;    // glyph shape units (face's loaded pixel size) -> px
    };
    std::vector<GlyphRun> glyphRuns(const Font& f, const std::u32string& text) const;

    // For dev
    using FontFaceFactory = std::function<IFontFace* (const io::path_t&)>;
    void setFontFaceFactory(const FontFaceFactory& f);

private:

    struct TextBlock {
        const char32_t* text = nullptr;
        int lenght = 0;
    };

    struct RequireFace {
        IFontFace* face = nullptr;   // real loaded face
        std::vector<IFontFace*> subtitutionFaces;
        FaceKey requireKey;          // require face

        bool isSymbolMode() const;
        double pixelScale() const;
    };

    IFontFace* createFontFace(const io::path_t& path) const;
    RequireFace* fontFace(const Font& f, bool isSymbolMode = false) const;

    std::vector<TextBlock> splitTextByLines(const std::u32string& text) const;
    std::vector<TextBlock> splitTextByFontFaces(const RequireFace* rf, const TextBlock& text) const;

    FontFaceFactory m_fontFaceFactory;

    mutable std::vector<IFontFace*> m_loadedFaces;
    mutable std::vector<RequireFace*> m_requiredFaces;

    //mutable FontRenderCache m_renderCache;
};
}
