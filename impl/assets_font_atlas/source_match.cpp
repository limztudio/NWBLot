// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateFontAtlasSourceMatch(const FontAtlasPayload& payload, const Font& font){
    const Core::Assets::AssetBytes& source = font.fontBytes();
    if(
        payload.font.name() != font.virtualPath() || payload.faceIndex != font.faceIndex()
        || ComputeSha256({ source.data(), source.size() }) != payload.fontSha256 || source.size() < 12u
    ){
        NWB_LOGGER_ERROR(GLB_TEXT("FontAtlas source match failed: font identity, source hash, or face index differs"));
        return false;
    }
    const u32 tableCount = (static_cast<u32>(source[4u]) << 8u) | source[5u];
    if(tableCount == 0u || tableCount > s_FontMaxTableCount || static_cast<u64>(tableCount) * 16u > source.size() - 12u)
        return false;
    usize matched = 0u;
    bool hasHead = false;
    bool hasMaxp = false;
    for(u32 index = 0u; index < tableCount; ++index){
        const u8* record = source.data() + 12u + static_cast<usize>(index) * 16u;
        const u32 tag = ReadFontAtlasBigU32(record);
        const u32 offset = ReadFontAtlasBigU32(record + 8u);
        const u32 length = ReadFontAtlasBigU32(record + 12u);
        if(offset > source.size() || length > source.size() - offset)
            return false;
        if(tag == 0x68656164u){
            hasHead = true;
            if(length < 20u || ((static_cast<u32>(source[offset + 18u]) << 8u) | source[offset + 19u]) != payload.unitsPerEm)
                return false;
        }
        else if(tag == 0x6d617870u){
            hasMaxp = true;
            if(length < 6u || ((static_cast<u32>(source[offset + 4u]) << 8u) | source[offset + 5u]) != payload.sourceGlyphCount)
                return false;
        }
        if((tag != s_FontAtlasKernTag && tag != s_FontAtlasGposTag && tag != s_FontAtlasGdefTag) || length == 0u)
            continue;
        const FontAtlasPositioningTable* exported = nullptr;
        for(const FontAtlasPositioningTable& table : payload.positioningTables){
            if(table.tag == tag){
                exported = &table;
                break;
            }
        }
        if(!exported || exported->bytes.size() != length || GLB_MEMCMP(exported->bytes.data(), source.data() + offset, length) != 0){
            NWB_LOGGER_ERROR(GLB_TEXT("FontAtlas source match failed: original positioning tables differ or are missing"));
            return false;
        }
        ++matched;
    }
    if(!hasHead || !hasMaxp || matched != payload.positioningTables.size()){
        NWB_LOGGER_ERROR(GLB_TEXT("FontAtlas source match failed: atlas contains a positioning table absent from the font"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

