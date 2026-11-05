// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 CheckGuardedOverlap(const FontAtlasPayload& payload){
    for(usize groupIndex = 0u; groupIndex < payload.groups.size(); ++groupIndex){
        const FontAtlasGroup& group = payload.groups[groupIndex];
        if(!ValidateFontAtlasGroupGuardedOverlap(
            payload.glyphs,
            static_cast<u32>(groupIndex),
            group.width,
            group.height,
            group.channelCount,
            payload.guardTexels
        ))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateFontAtlasPayload(const FontAtlasPayload& payload, Core::Alloc::ScratchArena&){
    if(!payload.font || payload.fontSha256 == Sha256Digest{} || payload.faceIndex != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas validation failed: missing font identity/hash or unsupported face"));
        return false;
    }
    if(!ValidateFontAtlasHeaderParameters(
        payload.unitsPerEm,
        payload.sourceGlyphCount,
        payload.glyphs.size(),
        payload.bakePpem,
        payload.spreadPixels,
        payload.guardTexels,
        payload.rasterMode,
        payload.ascenderUnits,
        payload.descenderUnits,
        payload.lineGapUnits,
        payload.groups.size()
    ))
        return false;
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
        if(!ValidateFontAtlasGlyphGroupRect(glyph, index, group.width, group.height, group.channelCount, payload.guardTexels))
            return false;
    }
    if(!__hidden_font_atlas_validation::CheckGuardedOverlap(payload))
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

