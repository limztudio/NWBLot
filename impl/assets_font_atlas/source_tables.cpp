// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetVector<FontAtlasPositioningTable>> CopyFontAtlasPositioningTables(const Span<const u8> fontBytes, const u32 faceIndex, const u32 sourceGlyphCount, Core::Assets::AssetArena& arena){
    if(faceIndex != 0u)
        return MakeUnexpected(Failure{});
    const Expected<FontSfntDirectory> directory = ReadFontSfntDirectory(fontBytes);
    if(!directory)
        return MakeUnexpected(Failure{});
    static constexpr Array<u32, s_FontAtlasMaxPositioningTableCount> s_Tags = { s_FontAtlasGdefTag, s_FontAtlasGposTag, s_FontAtlasKernTag };
    Core::Assets::AssetVector<FontAtlasPositioningTable> tables(arena);
    tables.reserve(s_FontAtlasMaxPositioningTableCount);
    u64 totalBytes = 0u;
    for(const u32 tag : s_Tags){
        for(u16 index = 0u; index < directory->tableCount(); ++index){
            const Expected<u32> tableTag = directory->tableTag(index);
            if(!tableTag)
                return MakeUnexpected(Failure{});
            if(*tableTag != tag)
                continue;
            const Expected<FontSfntTable> sourceTable = directory->table(index);
            if(!sourceTable)
                return MakeUnexpected(Failure{});
            const Span<const u8> sourceBytes = sourceTable->bytes;
            const usize length = sourceBytes.size();
            if(length == 0u)
                break;
            if(totalBytes + length > s_FontAtlasMaxPositioningBytes){
                NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas positioning export failed: source table byte budget exceeded"));
                return MakeUnexpected(Failure{});
            }
            FontAtlasPositioningTable table(arena);
            table.tag = tag;
            table.bytes.assign(sourceBytes.begin(), sourceBytes.end());
            table.sha256 = ComputeSha256({ table.bytes.data(), table.bytes.size() });
            if(!ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, sourceGlyphCount))
                return MakeUnexpected(Failure{});
            tables.push_back(Move(table));
            totalBytes += length;
            break;
        }
    }
    return tables;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

