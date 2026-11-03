// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 CheckGuardedOverlap(const FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratchArena){
    Vector<u64, Core::Alloc::ScratchArena> occupied(scratchArena);
    usize maxWords = 0u;
    for(const FontAtlasGroup& group : payload.groups){
        const Expected<usize> groupWords = TryComputeFontAtlasOccupancyWordCount(group.width, group.height, group.channelCount);
        if(!groupWords)
            return false;
        maxWords = Max(maxWords, *groupWords);
    }
    occupied.reserve(maxWords);
    for(usize groupIndex = 0u; groupIndex < payload.groups.size(); ++groupIndex){
        const FontAtlasGroup& group = payload.groups[groupIndex];
        const usize planePixels = static_cast<usize>(group.width) * group.height;
        const Expected<usize> groupWords = TryComputeFontAtlasOccupancyWordCount(group.width, group.height, group.channelCount);
        if(!groupWords)
            return false;
        occupied.assign(*groupWords, 0u);
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
                for(usize word = begin / s_BitsPerU64; word <= last / s_BitsPerU64; ++word){
                    if(!TryMarkFontAtlasGuardedWordOccupied(glyph.glyphId, begin, last, word, occupied[word]))
                        return false;
                }
            }
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateFontAtlasPayload(const FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratchArena){
    if(!payload.font || payload.fontSha256 == Sha256Digest{} || payload.faceIndex != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: missing font identity/hash or unsupported face"));
        return false;
    }
    if(payload.unitsPerEm < s_FontAtlasMinUnitsPerEm || payload.unitsPerEm > s_FontAtlasMaxUnitsPerEm || payload.sourceGlyphCount == 0u || payload.sourceGlyphCount > s_FontAtlasMaxGlyphCount || payload.glyphs.size() != payload.sourceGlyphCount){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: invalid face metrics or incomplete all-glyph policy"));
        return false;
    }
    if(!IsValidFontAtlasBakeParameters(payload.bakePpem, payload.spreadPixels, payload.guardTexels, payload.rasterMode)){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: unsupported bake size, spread, guard, or raster mode"));
        return false;
    }
    if(!IsValidFontAtlasMetrics(payload.ascenderUnits, payload.descenderUnits, payload.lineGapUnits)){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: nonfinite or invalid vertical metrics"));
        return false;
    }
    if(payload.groups.empty() || payload.groups.size() > s_FontAtlasMaxGroupCount){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: invalid image group count"));
        return false;
    }
    u64 pixelBytes = 0u;
    for(const FontAtlasGroup& group : payload.groups){
        if(!IsValidFontAtlasGroupDimensions(group.width, group.height, group.channelCount)){
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
    const f32 unitsPerPixel = ComputeFontAtlasUnitsPerPixel(payload.unitsPerEm, payload.bakePpem);
    for(usize index = 0u; index < payload.glyphs.size(); ++index){
        const FontAtlasGlyph& glyph = payload.glyphs[index];
        if(!ValidateFontAtlasGlyphRecord(glyph, index, payload.groups.size(), unitsPerPixel))
            return false;
        if(glyph.drawable == 0u)
            continue;
        const FontAtlasGroup& group = payload.groups[glyph.group];
        if(!IsValidFontAtlasGlyphGroupRect(glyph, group.width, group.height, group.channelCount, payload.guardTexels)){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: glyph {} exceeds guarded page bounds"), index);
            return false;
        }
    }
    if(!__hidden_font_atlas_validation::CheckGuardedOverlap(payload, scratchArena))
        return false;
    if(payload.positioningTables.size() > s_FontAtlasMaxPositioningTableCount){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: positioning table count exceeds kern/GPOS/GDEF"));
        return false;
    }
    u64 positioningBytes = 0u;
    u32 previousTag = 0u;
    for(const FontAtlasPositioningTable& table : payload.positioningTables){
        positioningBytes += table.bytes.size();
        if(table.tag <= previousTag || positioningBytes > s_FontAtlasMaxPositioningBytes || ComputeSha256({ table.bytes.data(), table.bytes.size() }) != table.sha256 || !ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, payload.sourceGlyphCount)){
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

