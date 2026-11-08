// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"

#include <global/binary_little_endian.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_binary{

static_assert(s_NameHashLaneCount == s_FontAtlasIdentityLaneCount);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<FontAtlasPayload> Deserialize(const Span<const u8> bytes, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    usize cursor = 0u;
    FontAtlasPayload payload(arena);
    NameHash identity;
    Array<u64, s_FontAtlasIdentityLaneCount> identityQwords{};
    if(!TryReadFontAtlasBinaryPreambleAndIdentity(bytes, cursor, identityQwords))
        return MakeUnexpected(Failure{});
    for(usize lane = 0u; lane < s_NameHashLaneCount; ++lane)
        identity.qwords[lane] = identityQwords[lane];
    payload.font.virtualPath = Name(identity);
    if(!ReadBytes(bytes, sizeof(payload.fontSha256.bytes), cursor, payload.fontSha256.bytes))
        return MakeUnexpected(Failure{});
    const auto headerResult = TryReadFontAtlasBinaryHeaderTail(bytes, cursor);
    if(!headerResult)
        return MakeUnexpected(Failure{});
    const FontAtlasBinaryDecodedHeader& header = *headerResult;
    payload.faceIndex = header.faceIndex;
    payload.unitsPerEm = header.unitsPerEm;
    payload.sourceGlyphCount = header.sourceGlyphCount;
    payload.bakePpem = header.bakePpem;
    payload.spreadPixels = header.spreadPixels;
    payload.guardTexels = header.guardTexels;
    payload.ascenderUnits = header.ascenderUnits;
    payload.descenderUnits = header.descenderUnits;
    payload.lineGapUnits = header.lineGapUnits;
    payload.rasterMode = header.rasterMode;
    if(!TryReadFontAtlasGlyphs(bytes, header.glyphCount, cursor, payload.glyphs))
        return MakeUnexpected(Failure{});
    payload.groups.reserve(header.groupCount);
    u64 pixelBytes = 0u;
    for(u32 index = 0u; index < header.groupCount; ++index){
        FontAtlasGroup group(arena);
        u32 byteCount = 0u;
        if(!TryReadFontAtlasGroupHeader(bytes, cursor, group.width, group.height, group.channelCount, byteCount))
            return MakeUnexpected(Failure{});
        if(!ReadBytes(bytes, sizeof(group.sha256.bytes), cursor, group.sha256.bytes))
            return MakeUnexpected(Failure{});
        if(!ValidateFontAtlasBinaryGroupChunk(group.width, group.height, group.channelCount, byteCount, pixelBytes, cursor, bytes.size()))
            return MakeUnexpected(Failure{});
        if(!ReadFontAtlasBytesInto(bytes, byteCount, cursor, group.pixels))
            return MakeUnexpected(Failure{});
        pixelBytes += byteCount;
        payload.groups.push_back(Move(group));
    }
    payload.positioningTables.reserve(header.tableCount);
    u64 tableBytes = 0u;
    for(u32 index = 0u; index < header.tableCount; ++index){
        FontAtlasPositioningTable table(arena);
        u32 byteCount = 0u;
        if(!TryReadFontAtlasTableHeader(bytes, cursor, table.tag, byteCount))
            return MakeUnexpected(Failure{});
        if(!ReadBytes(bytes, sizeof(table.sha256.bytes), cursor, table.sha256.bytes))
            return MakeUnexpected(Failure{});
        if(!ValidateFontAtlasBinaryTableChunk(byteCount, tableBytes, cursor, bytes.size()))
            return MakeUnexpected(Failure{});
        if(!ReadFontAtlasBytesInto(bytes, byteCount, cursor, table.bytes))
            return MakeUnexpected(Failure{});
        tableBytes += byteCount;
        payload.positioningTables.push_back(Move(table));
    }
    if(cursor != bytes.size() || !ValidateFontAtlasPayload(payload, scratchArena))
        return MakeUnexpected(Failure{});
    return payload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetBytes> SerializeFontAtlasPayload(const FontAtlasPayload& payload, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_font_atlas_binary;
    if(!ValidateFontAtlasPayload(payload, scratchArena))
        return MakeUnexpected(Failure{});
    Core::Assets::AssetBytes binary(arena);
    usize size = s_FontAtlasBinaryHeaderBytes + payload.glyphs.size() * s_FontAtlasBinaryGlyphBytes;
    for(const FontAtlasGroup& group : payload.groups)
        size += s_FontAtlasBinaryGroupHeaderBytes + group.pixels.size();
    for(const FontAtlasPositioningTable& table : payload.positioningTables)
        size += s_FontAtlasBinaryTableHeaderBytes + table.bytes.size();
    binary.reserve(size);
    Array<u64, s_FontAtlasIdentityLaneCount> identityQwords{};
    for(usize lane = 0u; lane < s_NameHashLaneCount; ++lane)
        identityQwords[lane] = payload.font.name().hash().qwords[lane];
    AppendFontAtlasBinaryPreambleAndIdentity(identityQwords, binary);
    binary.insert(binary.end(), payload.fontSha256.bytes, payload.fontSha256.bytes + sizeof(payload.fontSha256.bytes));
    AppendFontAtlasBinaryHeaderTail(
        payload.faceIndex,
        payload.unitsPerEm,
        payload.sourceGlyphCount,
        payload.bakePpem,
        payload.spreadPixels,
        payload.guardTexels,
        payload.rasterMode,
        payload.ascenderUnits,
        payload.descenderUnits,
        payload.lineGapUnits,
        static_cast<u32>(payload.glyphs.size()),
        static_cast<u32>(payload.groups.size()),
        static_cast<u32>(payload.positioningTables.size()),
        binary
    );
    AppendFontAtlasGlyphsBinary(payload.glyphs, binary);
    for(const FontAtlasGroup& group : payload.groups)
        AppendFontAtlasGroupChunk(group.width, group.height, group.channelCount, Span<const u8>(group.sha256.bytes, sizeof(group.sha256.bytes)), group.pixels, binary);
    for(const FontAtlasPositioningTable& table : payload.positioningTables)
        AppendFontAtlasTableChunk(table.tag, Span<const u8>(table.sha256.bytes, sizeof(table.sha256.bytes)), table.bytes, binary);
    return binary;
}

Expected<FontAtlasPayload> DeserializeFontAtlasPayload(const Span<const u8> binary, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    auto payload = __hidden_font_atlas_binary::Deserialize(binary, arena, scratchArena);
    if(!payload){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas binary failed: malformed, noncanonical, unsupported, or invalid content"));
        return MakeUnexpected(payload.error());
    }
    return payload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

