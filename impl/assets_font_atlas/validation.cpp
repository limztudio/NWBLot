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
    if(!payload.font || !IsFontAtlasDigestNonZero(Span<const u8>(payload.fontSha256.bytes, sizeof(payload.fontSha256.bytes))) || payload.faceIndex != 0u){
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
        if(!ValidateFontAtlasGroupPixelChunk(
            group.width,
            group.height,
            group.channelCount,
            group.pixels,
            Span<const u8>(group.sha256.bytes, sizeof(group.sha256.bytes)),
            pixelBytes
        ))
            return false;
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
        if(!ValidateFontAtlasPositioningTableEntry(
            table.tag,
            table.bytes,
            Span<const u8>(table.sha256.bytes, sizeof(table.sha256.bytes)),
            payload.sourceGlyphCount,
            previousTag,
            positioningBytes
        ))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

