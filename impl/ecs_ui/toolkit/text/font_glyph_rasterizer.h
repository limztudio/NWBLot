// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "font.h"
#include <impl/ecs_ui/toolkit/text/shaper_types.adhd.h>

#include <ft2build.h>
#include FT_FREETYPE_H


NWB_IMPL_UI_BEGIN


class FontGlyphRasterizer final{
public:
    FontGlyphRasterizer(FT_Face face, b8 allowOutlineBounds, b8 coverageInkReliable)noexcept : m_face(face), m_allowOutlineBounds(allowOutlineBounds), m_coverageInkReliable(coverageInkReliable){}
    [[nodiscard]] GlyphCoverageBounds coverageBounds(u32 glyphId, f32 fontSize)const;
    [[nodiscard]] Expected<Rect> rasterBounds(u32 glyphId, u32 pixelSize);
    [[nodiscard]] b8 rasterize(u32 glyphId, u32 pixelSize, GlyphBitmap& output);

private:
    FT_Face m_face;
    b8 m_allowOutlineBounds;
    b8 m_coverageInkReliable;
};


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
