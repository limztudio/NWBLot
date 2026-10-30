// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetVector<FontAtlasPositioningTable>> CopyFontAtlasPositioningTables(const Font& font, const u32 sourceGlyphCount, Core::Assets::AssetArena& arena){
    if(!font.validatePayload())
        return MakeUnexpected(Failure{});
    const Core::Assets::AssetBytes& source = font.fontBytes();
    FontSfntDirectory directory;
    if(!ReadFontSfntDirectory({ source.data(), source.size() }, directory))
        return MakeUnexpected(Failure{});
    static constexpr u32 s_Tags[] = { s_FontAtlasGdefTag, s_FontAtlasGposTag, s_FontAtlasKernTag };
    Core::Assets::AssetVector<FontAtlasPositioningTable> tables(arena);
    tables.reserve(s_FontAtlasMaxPositioningTableCount);
    u64 totalBytes = 0u;
    for(const u32 tag : s_Tags){
        for(u16 index = 0u; index < directory.tableCount(); ++index){
            u32 tableTag = 0u;
            if(!directory.tableTag(index, tableTag))
                return MakeUnexpected(Failure{});
            if(tableTag != tag)
                continue;
            FontSfntTable sourceTable;
            if(!directory.table(index, sourceTable))
                return MakeUnexpected(Failure{});
            const Span<const u8> sourceBytes = sourceTable.bytes;
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
            if(!ValidateFontAtlasPositioningTable(table, sourceGlyphCount))
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

