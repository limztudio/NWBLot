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
        const Expected<Span<const u8>> sourceBytesResult = FindFontAtlasSourcePositioningTableBytes(*directory, tag, sourceGlyphCount, totalBytes);
        if(!sourceBytesResult)
            return MakeUnexpected(Failure{});
        if(sourceBytesResult->empty())
            continue;
        FontAtlasPositioningTable table(arena);
        table.tag = tag;
        table.bytes.assign(sourceBytesResult->begin(), sourceBytesResult->end());
        table.sha256 = ComputeSha256({ table.bytes.data(), table.bytes.size() });
        tables.push_back(Move(table));
    }
    return tables;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

