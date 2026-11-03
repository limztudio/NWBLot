// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateFontAtlasSourceMatch(const FontAtlasPayload& payload, const Font& font){
    const Core::Assets::AssetBytes& source = font.fontBytes();
    if(payload.font.name() != font.virtualPath() || payload.faceIndex != font.faceIndex() || ComputeSha256({ source.data(), source.size() }) != payload.fontSha256 || source.size() < s_FontSfntHeaderBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas source match failed: font identity, source hash, or face index differs"));
        return false;
    }
    const Expected<FontFaceSfntMetrics> sfntMetrics = TryReadFontFaceMetricsFromSfnt(source);
    if(!sfntMetrics || sfntMetrics->unitsPerEm != payload.unitsPerEm || sfntMetrics->sourceGlyphCount != payload.sourceGlyphCount)
        return false;
    const Expected<FontSfntDirectory> directory = ReadFontSfntDirectory({ source.data(), source.size() });
    if(!directory)
        return false;
    usize matched = 0u;
    for(u16 index = 0u; index < directory->tableCount(); ++index){
        const Expected<FontSfntTable> sourceTable = directory->table(index);
        if(!sourceTable)
            return false;
        const u32 tag = sourceTable->tag;
        if(!IsFontAtlasPositioningTableTag(tag) || sourceTable->bytes.empty())
            continue;
        const FontAtlasPositioningTable* exported = nullptr;
        for(const FontAtlasPositioningTable& table : payload.positioningTables){
            if(table.tag == tag){
                exported = &table;
                break;
            }
        }
        if(!exported || exported->bytes.size() != sourceTable->bytes.size() || NWB_MEMCMP(exported->bytes.data(), sourceTable->bytes.data(), sourceTable->bytes.size()) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas source match failed: original positioning tables differ or are missing"));
            return false;
        }
        ++matched;
    }
    if(matched != payload.positioningTables.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas source match failed: atlas contains a positioning table absent from the font"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

