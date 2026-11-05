// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"

#include <global/binary_little_endian.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_binary{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<FontAtlasPayload> Deserialize(const Span<const u8> bytes, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    usize cursor = 0u;
    FontAtlasPayload payload(arena);
    const auto magic = ReadU32LittleEndian(bytes, cursor);
    if(!magic)
        return MakeUnexpected(magic.error());
    const auto version = ReadU32LittleEndian(bytes, cursor);
    if(!version)
        return MakeUnexpected(version.error());
    const auto encoding = ReadU32LittleEndian(bytes, cursor);
    if(!encoding)
        return MakeUnexpected(encoding.error());
    const auto reserved = ReadU32LittleEndian(bytes, cursor);
    if(!reserved)
        return MakeUnexpected(reserved.error());
    if(!IsValidFontAtlasBinaryPreamble(*magic, *version, *encoding, *reserved))
        return MakeUnexpected(Failure{});
    NameHash identity;
    for(u64& lane : identity.qwords){
        const auto low = ReadU32LittleEndian(bytes, cursor);
        if(!low)
            return MakeUnexpected(low.error());
        const auto high = ReadU32LittleEndian(bytes, cursor);
        if(!high)
            return MakeUnexpected(high.error());
        lane = PackFontAtlasQword(*low, *high);
    }
    payload.font.virtualPath = Name(identity);
    if(!ReadBytes(bytes, sizeof(payload.fontSha256.bytes), cursor, payload.fontSha256.bytes))
        return MakeUnexpected(Failure{});
    const auto faceIndex = ReadU32LittleEndian(bytes, cursor);
    if(!faceIndex)
        return MakeUnexpected(faceIndex.error());
    payload.faceIndex = *faceIndex;
    const auto unitsPerEm = ReadU32LittleEndian(bytes, cursor);
    if(!unitsPerEm)
        return MakeUnexpected(unitsPerEm.error());
    payload.unitsPerEm = *unitsPerEm;
    const auto sourceGlyphCount = ReadU32LittleEndian(bytes, cursor);
    if(!sourceGlyphCount)
        return MakeUnexpected(sourceGlyphCount.error());
    payload.sourceGlyphCount = *sourceGlyphCount;
    const auto bakePpem = ReadU32LittleEndian(bytes, cursor);
    if(!bakePpem)
        return MakeUnexpected(bakePpem.error());
    payload.bakePpem = *bakePpem;
    const auto spreadPixels = ReadU32LittleEndian(bytes, cursor);
    if(!spreadPixels)
        return MakeUnexpected(spreadPixels.error());
    payload.spreadPixels = *spreadPixels;
    const auto guardTexels = ReadU32LittleEndian(bytes, cursor);
    if(!guardTexels)
        return MakeUnexpected(guardTexels.error());
    payload.guardTexels = *guardTexels;
    const auto raster = ReadU32LittleEndian(bytes, cursor);
    if(!raster)
        return MakeUnexpected(raster.error());
    const auto ascenderUnits = ReadF32LittleEndian(bytes, cursor);
    if(!ascenderUnits)
        return MakeUnexpected(ascenderUnits.error());
    payload.ascenderUnits = *ascenderUnits;
    const auto descenderUnits = ReadF32LittleEndian(bytes, cursor);
    if(!descenderUnits)
        return MakeUnexpected(descenderUnits.error());
    payload.descenderUnits = *descenderUnits;
    const auto lineGapUnits = ReadF32LittleEndian(bytes, cursor);
    if(!lineGapUnits)
        return MakeUnexpected(lineGapUnits.error());
    payload.lineGapUnits = *lineGapUnits;
    const auto glyphCount = ReadU32LittleEndian(bytes, cursor);
    if(!glyphCount)
        return MakeUnexpected(glyphCount.error());
    const auto groupCount = ReadU32LittleEndian(bytes, cursor);
    if(!groupCount)
        return MakeUnexpected(groupCount.error());
    const auto tableCount = ReadU32LittleEndian(bytes, cursor);
    if(!tableCount)
        return MakeUnexpected(tableCount.error());
    if(!IsValidFontAtlasBinaryCounts(*glyphCount, payload.sourceGlyphCount, *groupCount, *tableCount, *raster, cursor, bytes.size()))
        return MakeUnexpected(Failure{});
    payload.rasterMode = static_cast<FontAtlasRasterMode::Enum>(*raster);
    payload.glyphs.resize(*glyphCount);
    for(FontAtlasGlyph& glyph : payload.glyphs){
        const auto decoded = TryReadFontAtlasGlyph(bytes, cursor);
        if(!decoded)
            return MakeUnexpected(decoded.error());
        glyph = *decoded;
    }
    payload.groups.reserve(*groupCount);
    u64 pixelBytes = 0u;
    for(u32 index = 0u; index < *groupCount; ++index){
        FontAtlasGroup group(arena);
        const auto width = ReadU32LittleEndian(bytes, cursor);
        if(!width)
            return MakeUnexpected(width.error());
        group.width = *width;
        const auto height = ReadU32LittleEndian(bytes, cursor);
        if(!height)
            return MakeUnexpected(height.error());
        group.height = *height;
        const auto channelCount = ReadU32LittleEndian(bytes, cursor);
        if(!channelCount)
            return MakeUnexpected(channelCount.error());
        group.channelCount = *channelCount;
        const auto byteCount = ReadU32LittleEndian(bytes, cursor);
        if(!byteCount)
            return MakeUnexpected(byteCount.error());
        if(!ReadBytes(bytes, sizeof(group.sha256.bytes), cursor, group.sha256.bytes))
            return MakeUnexpected(Failure{});
        if(!IsValidFontAtlasGroupDimensions(group.width, group.height, group.channelCount))
            return MakeUnexpected(Failure{});
        const u64 expectedBytes = static_cast<u64>(group.width) * group.height * group.channelCount;
        if(expectedBytes != *byteCount || pixelBytes + *byteCount > s_FontAtlasMaxPixelBytes || cursor > bytes.size() || *byteCount > bytes.size() - cursor)
            return MakeUnexpected(Failure{});
        group.pixels.resize(*byteCount);
        if(!ReadBytes(bytes, *byteCount, cursor, group.pixels.data()))
            return MakeUnexpected(Failure{});
        pixelBytes += *byteCount;
        payload.groups.push_back(Move(group));
    }
    payload.positioningTables.reserve(*tableCount);
    u64 tableBytes = 0u;
    for(u32 index = 0u; index < *tableCount; ++index){
        FontAtlasPositioningTable table(arena);
        const auto tag = ReadU32LittleEndian(bytes, cursor);
        if(!tag)
            return MakeUnexpected(tag.error());
        table.tag = *tag;
        const auto byteCount = ReadU32LittleEndian(bytes, cursor);
        if(!byteCount)
            return MakeUnexpected(byteCount.error());
        if(!ReadBytes(bytes, sizeof(table.sha256.bytes), cursor, table.sha256.bytes))
            return MakeUnexpected(Failure{});
        if(*byteCount == 0u || tableBytes + *byteCount > s_FontAtlasMaxPositioningBytes || cursor > bytes.size() || *byteCount > bytes.size() - cursor)
            return MakeUnexpected(Failure{});
        table.bytes.resize(*byteCount);
        if(!ReadBytes(bytes, *byteCount, cursor, table.bytes.data()))
            return MakeUnexpected(Failure{});
        tableBytes += *byteCount;
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
    AppendU32LittleEndian(binary, s_FontAtlasBinaryMagic);
    AppendU32LittleEndian(binary, s_FontAtlasBinaryVersion);
    AppendU32LittleEndian(binary, s_FontAtlasDistanceEncoding);
    AppendU32LittleEndian(binary, 0u);
    for(const u64 lane : payload.font.name().hash().qwords){
        AppendU32LittleEndian(binary, UnpackFontAtlasLowWord(lane));
        AppendU32LittleEndian(binary, UnpackFontAtlasHighWord(lane));
    }
    binary.insert(binary.end(), payload.fontSha256.bytes, payload.fontSha256.bytes + sizeof(payload.fontSha256.bytes));
    AppendU32LittleEndian(binary, payload.faceIndex);
    AppendU32LittleEndian(binary, payload.unitsPerEm);
    AppendU32LittleEndian(binary, payload.sourceGlyphCount);
    AppendU32LittleEndian(binary, payload.bakePpem);
    AppendU32LittleEndian(binary, payload.spreadPixels);
    AppendU32LittleEndian(binary, payload.guardTexels);
    AppendU32LittleEndian(binary, static_cast<u32>(payload.rasterMode));
    AppendF32LittleEndian(binary, payload.ascenderUnits);
    AppendF32LittleEndian(binary, payload.descenderUnits);
    AppendF32LittleEndian(binary, payload.lineGapUnits);
    AppendU32LittleEndian(binary, static_cast<u32>(payload.glyphs.size()));
    AppendU32LittleEndian(binary, static_cast<u32>(payload.groups.size()));
    AppendU32LittleEndian(binary, static_cast<u32>(payload.positioningTables.size()));
    for(const FontAtlasGlyph& glyph : payload.glyphs){
        AppendU32LittleEndian(binary, glyph.glyphId);
        AppendU32LittleEndian(binary, glyph.group);
        AppendU32LittleEndian(binary, glyph.channel);
        AppendU32LittleEndian(binary, glyph.x);
        AppendU32LittleEndian(binary, glyph.y);
        AppendU32LittleEndian(binary, glyph.width);
        AppendU32LittleEndian(binary, glyph.height);
        AppendF32LittleEndian(binary, glyph.planeLeft);
        AppendF32LittleEndian(binary, glyph.planeTop);
        AppendF32LittleEndian(binary, glyph.planeRight);
        AppendF32LittleEndian(binary, glyph.planeBottom);
        AppendF32LittleEndian(binary, glyph.advanceUnits);
        AppendU32LittleEndian(binary, glyph.drawable);
    }
    for(const FontAtlasGroup& group : payload.groups){
        AppendU32LittleEndian(binary, group.width);
        AppendU32LittleEndian(binary, group.height);
        AppendU32LittleEndian(binary, group.channelCount);
        AppendU32LittleEndian(binary, static_cast<u32>(group.pixels.size()));
        binary.insert(binary.end(), group.sha256.bytes, group.sha256.bytes + sizeof(group.sha256.bytes));
        binary.insert(binary.end(), group.pixels.begin(), group.pixels.end());
    }
    for(const FontAtlasPositioningTable& table : payload.positioningTables){
        AppendU32LittleEndian(binary, table.tag);
        AppendU32LittleEndian(binary, static_cast<u32>(table.bytes.size()));
        binary.insert(binary.end(), table.sha256.bytes, table.sha256.bytes + sizeof(table.sha256.bytes));
        binary.insert(binary.end(), table.bytes.begin(), table.bytes.end());
    }
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

