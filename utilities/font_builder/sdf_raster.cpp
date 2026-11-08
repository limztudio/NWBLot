// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font_source.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RasterGlyphs> Rasterize(const BakeOptions& options, FontSource& inOutFont){
    RasterGlyphs glyphs(inOutFont.arena());
    FT_Face face = inOutFont.freeType().face();
    const f32 designPerPixel = Impl::ComputeFontAtlasUnitsPerPixel(face->units_per_EM, options.ppem);
    const u64 capacity = ComputeBakePixelCapacity(options.extent, options.maxGroups);
    u64 rasterBytes = 0u;
    glyphs.reserve(static_cast<usize>(face->num_glyphs));
    for(u32 glyphId = 0u; glyphId < static_cast<u32>(face->num_glyphs); ++glyphId){
        RasterGlyph glyph(glyphs.get_allocator().arena());
        glyph.record.glyphId = glyphId;
        const FT_Error loaded = FT_Load_Glyph(face, glyphId, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
        if(loaded != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: design metrics failed for glyph {}, FreeType error {}"), glyphId, loaded);
            return MakeUnexpected(Failure{});
        }
        glyph.record.advanceUnits = static_cast<f32>(face->glyph->advance.x);
        if(face->glyph->format != FT_GLYPH_FORMAT_OUTLINE){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: glyph {} is not a supported scalable outline"), glyphId);
            return MakeUnexpected(Failure{});
        }
        if(face->glyph->outline.n_points == 0){
            glyphs.push_back(Move(glyph));
            continue;
        }
        FT_Error result = FT_Load_Glyph(face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
        if(result == 0 && !options.outline)
            result = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        if(result == 0)
            result = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF);
        if(result != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: {} SDF failed for glyph {} (FreeType error {}); choose bitmap mode for intersecting outlines"), options.outline ? NWB_TEXT("outline") : NWB_TEXT("bitmap"), glyphId, result);
            return MakeUnexpected(Failure{});
        }
        const FT_Bitmap& bitmap = face->glyph->bitmap;
        if(bitmap.width == 0u || bitmap.rows == 0u){
            glyphs.push_back(Move(glyph));
            continue;
        }
        usize byteCount = 0u;
        if(!ValidateBakeGlyphBitmapFootprint(glyphId, bitmap.pixel_mode == FT_PIXEL_MODE_GRAY, bitmap.width, bitmap.rows, options.extent, options.maxGroups, capacity, rasterBytes, byteCount))
            return MakeUnexpected(Failure{});
        glyph.pixels.resize(byteCount);
        const i32 pitch = bitmap.pitch;
        const usize absolutePitch = static_cast<usize>(pitch < 0 ? -pitch : pitch);
        if(!bitmap.buffer || absolutePitch < bitmap.width){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: invalid native bitmap for glyph {}"), glyphId);
            return MakeUnexpected(Failure{});
        }
        for(u32 y = 0u; y < bitmap.rows; ++y){
            const u32 sourceY = pitch < 0 ? bitmap.rows - 1u - y : y;
            NWB_MEMCPY(glyph.pixels.data() + static_cast<usize>(y) * bitmap.width, bitmap.width, bitmap.buffer + static_cast<usize>(sourceY) * absolutePitch, bitmap.width);
        }
        PopulateBakeGlyphRecord(bitmap.width, bitmap.rows, face->glyph->bitmap_left, face->glyph->bitmap_top, designPerPixel, glyph.record);
        glyphs.push_back(Move(glyph));
    }
    return glyphs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

