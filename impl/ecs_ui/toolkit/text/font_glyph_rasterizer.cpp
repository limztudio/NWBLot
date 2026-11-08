// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font_glyph_rasterizer.h"

#include <core/common/log.h>
#include <ft2build.h>
#include FT_OUTLINE_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


GlyphCoverageBounds FontGlyphRasterizer::coverageBounds(u32 glyphId, f32 fontSize)const{
    if(!m_allowOutlineBounds || m_coverageInkReliable)
        return {};
    if(FT_Load_Glyph(m_face, glyphId, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT) != 0){
        NWB_LOGGER_WARNING(NWB_TEXT("UI glyph outline bounds unavailable; retaining conservative coverage candidate"));
        return {};
    }
    const FT_GlyphSlot slot = m_face->glyph;
    if(slot->format != FT_GLYPH_FORMAT_OUTLINE)
        return {};
    if(slot->outline.n_points == 0)
        return { {}, true };
    FT_BBox box{};
    FT_Outline_Get_CBox(&slot->outline, &box);
    // Native outline bounds include CFF font/subfont transforms; leave HarfBuzz ink and placement unchanged.
    return ComputeOutlineCoverageBounds(box.xMin, box.yMin, box.xMax, box.yMax, fontSize, m_face->units_per_EM);
}


Expected<Rect> FontGlyphRasterizer::rasterBounds(const u32 glyphId, const u32 pixelSize){
    if(pixelSize == 0u || pixelSize > s_TextMaxRasterPpem)
        return MakeUnexpected(Failure{});
    if(FT_Set_Pixel_Sizes(m_face, 0u, pixelSize) != 0 || FT_Load_Glyph(m_face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("UI glyph raster measurement failed"));
        return MakeUnexpected(Failure{});
    }
    // FT_Load_Glyph presets this box through the same ft_glyphslot_preset_bitmap path as the normal renderer, without allocating coverage pixels.
    const FT_GlyphSlot slot = m_face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if(slot->format != FT_GLYPH_FORMAT_OUTLINE || (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.width != 0u && bitmap.rows != 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("UI glyph measurement requires a grayscale outline"));
        return MakeUnexpected(Failure{});
    }
    return MakeRasterGlyphBoundsRect(slot->bitmap_left, slot->bitmap_top, bitmap.width, bitmap.rows);
}

b8 FontGlyphRasterizer::rasterize(u32 glyphId, u32 pixelSize, GlyphBitmap& output){
    if(pixelSize == 0u || pixelSize > s_TextMaxRasterPpem)
        return false;
    if(FT_Set_Pixel_Sizes(m_face, 0u, pixelSize) != 0 || FT_Load_Glyph(m_face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0 || FT_Render_Glyph(m_face->glyph, FT_RENDER_MODE_NORMAL) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("UI glyph rasterization failed"));
        return false;
    }
    const FT_GlyphSlot slot = m_face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if(bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.width != 0u && bitmap.rows != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("UI glyph rasterizer requires grayscale coverage"));
        return false;
    }
    output.width = bitmap.width;
    output.height = bitmap.rows;
    output.bearingX = slot->bitmap_left;
    output.bearingY = slot->bitmap_top;
    if(bitmap.width > s_GlyphAtlasPageExtent - s_GlyphAtlasGlyphGutter || bitmap.rows > s_GlyphAtlasPageExtent - s_GlyphAtlasGlyphGutter)
        return false;
    output.pixels.resize(static_cast<usize>(bitmap.width) * bitmap.rows);
    const usize pitch = static_cast<usize>(Abs(bitmap.pitch));
    for(u32 row = 0u; row < bitmap.rows; ++row){
        const u32 sourceRow = bitmap.pitch >= 0 ? row : bitmap.rows - row - 1u;
        if(bitmap.width != 0u)
            NWB_MEMCPY(output.pixels.data() + static_cast<usize>(row) * bitmap.width, bitmap.width, bitmap.buffer + static_cast<usize>(sourceRow) * pitch, bitmap.width);
    }
    return true;
}


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
