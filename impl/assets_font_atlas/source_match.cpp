// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"

#include <impl/assets_font/source_directory.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateFontAtlasSourceMatch(const FontAtlasPayload& payload, const Font& font){
    const Core::Assets::AssetBytes& source = font.fontBytes();
    if(
        payload.font.name() != font.virtualPath() || payload.faceIndex != font.faceIndex()
        || ComputeSha256({ source.data(), source.size() }) != payload.fontSha256 || source.size() < s_FontSfntHeaderBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas source match failed: font identity, source hash, or face index differs"));
        return false;
    }
    const auto directoryResult = FontSfntDirectory::Read({ source.data(), source.size() });
    if(!directoryResult)
        return false;
    const FontSfntDirectory& directory = *directoryResult;
    usize matched = 0u;
    bool hasHead = false;
    bool hasMaxp = false;
    for(u16 index = 0u; index < directory.tableCount(); ++index){
        const auto sourceTableResult = directory.table(index);
        if(!sourceTableResult)
            return false;
        const FontSfntTable& sourceTable = *sourceTableResult;
        const u32 tag = sourceTable.tag;
        if(tag == s_FontSfntHeadTag){
            hasHead = true;
            const auto unitsPerEmResult = ReadFontSfntU16(sourceTable.bytes, 18u);
            if(!unitsPerEmResult || *unitsPerEmResult != payload.unitsPerEm)
                return false;
        }
        else if(tag == s_FontSfntMaxpTag){
            hasMaxp = true;
            const auto glyphCountResult = ReadFontSfntU16(sourceTable.bytes, 4u);
            if(!glyphCountResult || *glyphCountResult != payload.sourceGlyphCount)
                return false;
        }
        if((tag != s_FontAtlasKernTag && tag != s_FontAtlasGposTag && tag != s_FontAtlasGdefTag) || sourceTable.bytes.empty())
            continue;
        const FontAtlasPositioningTable* exported = nullptr;
        for(const FontAtlasPositioningTable& table : payload.positioningTables){
            if(table.tag == tag){
                exported = &table;
                break;
            }
        }
        if(
            !exported || exported->bytes.size() != sourceTable.bytes.size()
            || NWB_MEMCMP(exported->bytes.data(), sourceTable.bytes.data(), sourceTable.bytes.size()) != 0
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas source match failed: original positioning tables differ or are missing"));
            return false;
        }
        ++matched;
    }
    if(!hasHead || !hasMaxp || matched != payload.positioningTables.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas source match failed: atlas contains a positioning table absent from the font"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

