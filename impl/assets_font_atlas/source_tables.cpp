// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CopyFontAtlasPositioningTables(const Font& font, FontAtlasPayload& outPayload){
    if(!font.validatePayload())
        return false;
    const Core::Assets::AssetBytes& source = font.fontBytes();
    const u32 tableCount = (static_cast<u32>(source[4u]) << 8u) | source[5u];
    static constexpr u32 s_Tags[] = { s_FontAtlasGdefTag, s_FontAtlasGposTag, s_FontAtlasKernTag };
    Core::Assets::AssetArena& arena = outPayload.positioningTables.get_allocator().arena();
    Core::Assets::AssetVector<FontAtlasPositioningTable> tables(arena);
    tables.reserve(3u);
    u64 totalBytes = 0u;
    for(const u32 tag : s_Tags){
        for(u32 index = 0u; index < tableCount; ++index){
            const u8* record = source.data() + 12u + static_cast<usize>(index) * 16u;
            if(ReadFontAtlasBigU32(record) != tag)
                continue;
            const u32 offset = ReadFontAtlasBigU32(record + 8u);
            const u32 length = ReadFontAtlasBigU32(record + 12u);
            if(length == 0u)
                break;
            if(totalBytes + length > s_FontAtlasMaxPositioningBytes){
                NWB_LOGGER_ERROR(GLB_TEXT("FontAtlas positioning export failed: source table byte budget exceeded"));
                return false;
            }
            FontAtlasPositioningTable table(arena);
            table.tag = tag;
            table.bytes.assign(source.begin() + offset, source.begin() + offset + length);
            table.sha256 = ComputeSha256({ table.bytes.data(), table.bytes.size() });
            if(!ValidateFontAtlasPositioningTable(table, outPayload.sourceGlyphCount))
                return false;
            tables.push_back(Move(table));
            totalBytes += length;
            break;
        }
    }
    outPayload.positioningTables = Move(tables);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

