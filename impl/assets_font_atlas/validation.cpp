// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"

#include <global/math/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Admission working storage is bounded by one group and uses the supplied asset domain arena.
[[nodiscard]] static bool CheckGuardedOverlap(const FontAtlasPayload& payload){
    Core::Assets::AssetVector<u64> occupied(payload.glyphs.get_allocator().arena());
    usize maxWords = 0u;
    for(const FontAtlasGroup& group : payload.groups)
        maxWords = Max(maxWords, (static_cast<usize>(group.width) * group.height * group.channelCount + 63u) / 64u);
    occupied.reserve(maxWords);
    for(usize groupIndex = 0u; groupIndex < payload.groups.size(); ++groupIndex){
        const FontAtlasGroup& group = payload.groups[groupIndex];
        const usize planePixels = static_cast<usize>(group.width) * group.height;
        occupied.assign((planePixels * group.channelCount + 63u) / 64u, 0u);
        for(const FontAtlasGlyph& glyph : payload.glyphs){
            if(glyph.drawable == 0u || glyph.group != groupIndex)
                continue;
            const u32 firstX = glyph.x - payload.guardTexels;
            const u32 endX = glyph.x + glyph.width + payload.guardTexels;
            const u32 firstY = glyph.y - payload.guardTexels;
            const u32 endY = glyph.y + glyph.height + payload.guardTexels;
            for(u32 row = firstY; row < endY; ++row){
                const usize begin = planePixels * glyph.channel + static_cast<usize>(row) * group.width + firstX;
                const usize last = begin + endX - firstX - 1u;
                for(usize word = begin / 64u; word <= last / 64u; ++word){
                    u64 mask = Limit<u64>::s_Max;
                    if(word == begin / 64u)
                        mask &= Limit<u64>::s_Max << (begin % 64u);
                    if(word == last / 64u)
                        mask &= Limit<u64>::s_Max >> (63u - last % 64u);
                    if((occupied[word] & mask) != 0u){
                        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: glyph {} overlaps another guarded region"), glyph.glyphId);
                        return false;
                    }
                    occupied[word] |= mask;
                }
            }
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateFontAtlasPayload(const FontAtlasPayload& payload){
    if(!payload.font || payload.fontSha256 == Sha256Digest{} || payload.faceIndex != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: missing font identity/hash or unsupported face"));
        return false;
    }
    if(
        payload.unitsPerEm < 16u || payload.unitsPerEm > 16384u || payload.sourceGlyphCount == 0u
        || payload.sourceGlyphCount > s_FontAtlasMaxGlyphCount || payload.glyphs.size() != payload.sourceGlyphCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: invalid face metrics or incomplete all-glyph policy"));
        return false;
    }
    if(
        payload.bakePpem < s_FontAtlasMinBakePpem || payload.bakePpem > s_FontAtlasMaxBakePpem
        || payload.spreadPixels < s_FontAtlasMinSpreadPixels || payload.spreadPixels > s_FontAtlasMaxSpreadPixels
        || payload.guardTexels != 1u || payload.rasterMode > FontAtlasRasterMode::Bitmap
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: unsupported bake size, spread, guard, or raster mode"));
        return false;
    }
    if(
        !IsFinite(payload.ascenderUnits) || !IsFinite(payload.descenderUnits) || !IsFinite(payload.lineGapUnits)
        || payload.ascenderUnits <= payload.descenderUnits || payload.lineGapUnits < 0.f
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: nonfinite or invalid vertical metrics"));
        return false;
    }
    if(payload.groups.empty() || payload.groups.size() > s_FontAtlasMaxGroupCount){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: invalid image group count"));
        return false;
    }
    u64 pixelBytes = 0u;
    for(const FontAtlasGroup& group : payload.groups){
        if(
            group.width == 0u || group.height == 0u || group.width > s_FontAtlasMaxExtent || group.height > s_FontAtlasMaxExtent
            || group.channelCount == 0u || group.channelCount > 4u
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: invalid extent or channel count"));
            return false;
        }
        const u64 byteCount = static_cast<u64>(group.width) * group.height * group.channelCount;
        if(group.pixels.size() != byteCount || pixelBytes + byteCount > s_FontAtlasMaxPixelBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: invalid extent, payload size, or byte budget"));
            return false;
        }
        if(ComputeSha256({ group.pixels.data(), group.pixels.size() }) != group.sha256){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: pixel content hash mismatch"));
            return false;
        }
        pixelBytes += byteCount;
    }
    for(usize index = 0u; index < payload.glyphs.size(); ++index){
        const FontAtlasGlyph& glyph = payload.glyphs[index];
        if(
            glyph.glyphId != index || glyph.drawable > 1u || !IsFinite(glyph.advanceUnits)
            || !IsFinite(glyph.planeLeft) || !IsFinite(glyph.planeTop) || !IsFinite(glyph.planeRight) || !IsFinite(glyph.planeBottom)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: invalid glyph ID, flags, or metrics at {}"), index);
            return false;
        }
        if(glyph.drawable == 0u){
            if(
                glyph.group != 0u || glyph.channel != 0u || glyph.x != 0u || glyph.y != 0u || glyph.width != 0u || glyph.height != 0u
                || glyph.planeLeft != 0.f || glyph.planeTop != 0.f || glyph.planeRight != 0.f || glyph.planeBottom != 0.f
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: nondrawable glyph {} carries a bitmap"), index);
                return false;
            }
            continue;
        }
        if(
            glyph.group >= payload.groups.size() || glyph.width == 0u || glyph.height == 0u
            || glyph.planeLeft >= glyph.planeRight || glyph.planeTop >= glyph.planeBottom
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: invalid glyph {} page or padded bounds"), index);
            return false;
        }
        const f32 unitsPerPixel = static_cast<f32>(payload.unitsPerEm) / static_cast<f32>(payload.bakePpem);
        const SIMDVector expectedSize = VectorScale(
            VectorSet(static_cast<f32>(glyph.width), static_cast<f32>(glyph.height), 0.0f, 0.0f), unitsPerPixel
        );
        const SIMDVector planeSize = VectorSubtract(VectorSet(glyph.planeRight, glyph.planeBottom, 0.0f, 0.0f),
            VectorSet(glyph.planeLeft, glyph.planeTop, 0.0f, 0.0f));
        const SIMDVector difference = VectorSubtract(planeSize, expectedSize);
        const SIMDVector magnitude = VectorSelect(difference, VectorNegate(difference), VectorLess(difference, VectorZero()));
        const SIMDVector tolerance = VectorScale(expectedSize, 0.0001f);
        if((VectorMoveMask(VectorGreater(magnitude, tolerance)) & VectorComponentMask::s_XY) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: glyph {} plane bounds disagree with bitmap size"), index);
            return false;
        }
        const FontAtlasGroup& group = payload.groups[glyph.group];
        if(
            glyph.channel >= group.channelCount || glyph.x < payload.guardTexels || glyph.y < payload.guardTexels
            || glyph.x >= group.width || glyph.y >= group.height
            || glyph.width >= group.width - glyph.x || glyph.height >= group.height - glyph.y
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: glyph {} exceeds guarded page bounds"), index);
            return false;
        }
    }
    if(!__hidden_font_atlas_validation::CheckGuardedOverlap(payload))
        return false;
    if(payload.positioningTables.size() > 3u){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: positioning table count exceeds kern/GPOS/GDEF"));
        return false;
    }
    u64 positioningBytes = 0u;
    u32 previousTag = 0u;
    for(const FontAtlasPositioningTable& table : payload.positioningTables){
        positioningBytes += table.bytes.size();
        if(
            table.tag <= previousTag || positioningBytes > s_FontAtlasMaxPositioningBytes
            || ComputeSha256({ table.bytes.data(), table.bytes.size() }) != table.sha256
            || !ValidateFontAtlasPositioningTable(table, payload.sourceGlyphCount)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: positioning order, bounds, hash, or structure"));
            return false;
        }
        previousTag = table.tag;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

