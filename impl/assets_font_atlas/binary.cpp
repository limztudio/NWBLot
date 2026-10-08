// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binary_payload.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_binary{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendU32(Core::Assets::AssetBytes& bytes, const u32 value){
    for(u32 index = 0u; index < 4u; ++index)
        bytes.push_back(static_cast<u8>(value >> (index * 8u)));
}

static void AppendFloat(Core::Assets::AssetBytes& bytes, const f32 value){
    AppendU32(bytes, BitCast<u32>(value));
}

[[nodiscard]] static Expected<u32> ReadU32(const Core::Assets::AssetBytes& bytes, usize& cursor)noexcept{
    if(cursor > bytes.size() || bytes.size() - cursor < 4u)
        return MakeUnexpected(Failure{});
    const u32 value = static_cast<u32>(bytes[cursor]) | (static_cast<u32>(bytes[cursor + 1u]) << 8u)
        | (static_cast<u32>(bytes[cursor + 2u]) << 16u) | (static_cast<u32>(bytes[cursor + 3u]) << 24u)
    ;
    cursor += 4u;
    return value;
}

[[nodiscard]] static Expected<f32> ReadFloat(const Core::Assets::AssetBytes& bytes, usize& cursor)noexcept{
    const auto value = ReadU32(bytes, cursor);
    if(!value)
        return MakeUnexpected(value.error());
    return BitCast<f32>(*value);
}

[[nodiscard]] static bool ReadBytes(const Core::Assets::AssetBytes& bytes, usize& cursor, const usize count, void* destination){
    if(cursor > bytes.size() || count > bytes.size() - cursor)
        return false;
    if(count > 0u)
        NWB_MEMCPY(destination, count, bytes.data() + cursor, count);
    cursor += count;
    return true;
}

[[nodiscard]] static Expected<FontAtlasGlyph> ReadGlyph(const Core::Assets::AssetBytes& bytes, usize& cursor)noexcept{
    FontAtlasGlyph glyph;
    const auto glyphId = ReadU32(bytes, cursor);
    if(!glyphId)
        return MakeUnexpected(glyphId.error());
    glyph.glyphId = *glyphId;
    const auto group = ReadU32(bytes, cursor);
    if(!group)
        return MakeUnexpected(group.error());
    glyph.group = *group;
    const auto channel = ReadU32(bytes, cursor);
    if(!channel)
        return MakeUnexpected(channel.error());
    glyph.channel = *channel;
    const auto x = ReadU32(bytes, cursor);
    if(!x)
        return MakeUnexpected(x.error());
    glyph.x = *x;
    const auto y = ReadU32(bytes, cursor);
    if(!y)
        return MakeUnexpected(y.error());
    glyph.y = *y;
    const auto width = ReadU32(bytes, cursor);
    if(!width)
        return MakeUnexpected(width.error());
    glyph.width = *width;
    const auto height = ReadU32(bytes, cursor);
    if(!height)
        return MakeUnexpected(height.error());
    glyph.height = *height;
    const auto planeLeft = ReadFloat(bytes, cursor);
    if(!planeLeft)
        return MakeUnexpected(planeLeft.error());
    glyph.planeLeft = *planeLeft;
    const auto planeTop = ReadFloat(bytes, cursor);
    if(!planeTop)
        return MakeUnexpected(planeTop.error());
    glyph.planeTop = *planeTop;
    const auto planeRight = ReadFloat(bytes, cursor);
    if(!planeRight)
        return MakeUnexpected(planeRight.error());
    glyph.planeRight = *planeRight;
    const auto planeBottom = ReadFloat(bytes, cursor);
    if(!planeBottom)
        return MakeUnexpected(planeBottom.error());
    glyph.planeBottom = *planeBottom;
    const auto advanceUnits = ReadFloat(bytes, cursor);
    if(!advanceUnits)
        return MakeUnexpected(advanceUnits.error());
    glyph.advanceUnits = *advanceUnits;
    const auto drawable = ReadU32(bytes, cursor);
    if(!drawable)
        return MakeUnexpected(drawable.error());
    glyph.drawable = *drawable;
    return glyph;
}

[[nodiscard]] static Expected<FontAtlasPayload> Deserialize(
    const Core::Assets::AssetBytes& bytes,
    Core::Assets::AssetArena& arena
){
    usize cursor = 0u;
    FontAtlasPayload payload(arena);
    const auto magic = ReadU32(bytes, cursor);
    if(!magic)
        return MakeUnexpected(magic.error());
    const auto version = ReadU32(bytes, cursor);
    if(!version)
        return MakeUnexpected(version.error());
    const auto encoding = ReadU32(bytes, cursor);
    if(!encoding)
        return MakeUnexpected(encoding.error());
    const auto reserved = ReadU32(bytes, cursor);
    if(!reserved)
        return MakeUnexpected(reserved.error());
    if(
        *magic != FontAtlasBinaryPayload::s_Magic || *version != FontAtlasBinaryPayload::s_Version
        || *encoding != s_FontAtlasDistanceEncoding || *reserved != 0u
    )
        return MakeUnexpected(Failure{});
    NameHash identity = {};
    for(u64& lane : identity.qwords){
        const auto low = ReadU32(bytes, cursor);
        if(!low)
            return MakeUnexpected(low.error());
        const auto high = ReadU32(bytes, cursor);
        if(!high)
            return MakeUnexpected(high.error());
        lane = static_cast<u64>(*low) | (static_cast<u64>(*high) << 32u);
    }
    payload.font.virtualPath = Name(identity);
    if(!ReadBytes(bytes, cursor, sizeof(payload.fontSha256.bytes), payload.fontSha256.bytes))
        return MakeUnexpected(Failure{});
    const auto faceIndex = ReadU32(bytes, cursor);
    if(!faceIndex)
        return MakeUnexpected(faceIndex.error());
    payload.faceIndex = *faceIndex;
    const auto unitsPerEm = ReadU32(bytes, cursor);
    if(!unitsPerEm)
        return MakeUnexpected(unitsPerEm.error());
    payload.unitsPerEm = *unitsPerEm;
    const auto sourceGlyphCount = ReadU32(bytes, cursor);
    if(!sourceGlyphCount)
        return MakeUnexpected(sourceGlyphCount.error());
    payload.sourceGlyphCount = *sourceGlyphCount;
    const auto bakePpem = ReadU32(bytes, cursor);
    if(!bakePpem)
        return MakeUnexpected(bakePpem.error());
    payload.bakePpem = *bakePpem;
    const auto spreadPixels = ReadU32(bytes, cursor);
    if(!spreadPixels)
        return MakeUnexpected(spreadPixels.error());
    payload.spreadPixels = *spreadPixels;
    const auto guardTexels = ReadU32(bytes, cursor);
    if(!guardTexels)
        return MakeUnexpected(guardTexels.error());
    payload.guardTexels = *guardTexels;
    const auto raster = ReadU32(bytes, cursor);
    if(!raster)
        return MakeUnexpected(raster.error());
    const auto ascenderUnits = ReadFloat(bytes, cursor);
    if(!ascenderUnits)
        return MakeUnexpected(ascenderUnits.error());
    payload.ascenderUnits = *ascenderUnits;
    const auto descenderUnits = ReadFloat(bytes, cursor);
    if(!descenderUnits)
        return MakeUnexpected(descenderUnits.error());
    payload.descenderUnits = *descenderUnits;
    const auto lineGapUnits = ReadFloat(bytes, cursor);
    if(!lineGapUnits)
        return MakeUnexpected(lineGapUnits.error());
    payload.lineGapUnits = *lineGapUnits;
    const auto glyphCount = ReadU32(bytes, cursor);
    if(!glyphCount)
        return MakeUnexpected(glyphCount.error());
    const auto groupCount = ReadU32(bytes, cursor);
    if(!groupCount)
        return MakeUnexpected(groupCount.error());
    const auto tableCount = ReadU32(bytes, cursor);
    if(!tableCount)
        return MakeUnexpected(tableCount.error());
    if(
        *glyphCount == 0u || *glyphCount > s_FontAtlasMaxGlyphCount || *glyphCount != payload.sourceGlyphCount
        || *groupCount == 0u || *groupCount > s_FontAtlasMaxGroupCount || *tableCount > 3u
        || *raster > FontAtlasRasterMode::Bitmap
    )
        return MakeUnexpected(Failure{});
    payload.rasterMode = static_cast<FontAtlasRasterMode::Enum>(*raster);
    const u64 minimumRemaining = static_cast<u64>(*glyphCount) * FontAtlasBinaryPayload::s_GlyphBytes
        + static_cast<u64>(*groupCount) * FontAtlasBinaryPayload::s_GroupHeaderBytes
        + static_cast<u64>(*tableCount) * FontAtlasBinaryPayload::s_TableHeaderBytes
    ;
    if(cursor > bytes.size() || minimumRemaining > bytes.size() - cursor)
        return MakeUnexpected(Failure{});
    payload.glyphs.resize(*glyphCount);
    for(FontAtlasGlyph& glyph : payload.glyphs){
        const auto decoded = ReadGlyph(bytes, cursor);
        if(!decoded)
            return MakeUnexpected(decoded.error());
        glyph = *decoded;
    }
    payload.groups.reserve(*groupCount);
    u64 pixelBytes = 0u;
    for(u32 index = 0u; index < *groupCount; ++index){
        FontAtlasGroup group(arena);
        const auto width = ReadU32(bytes, cursor);
        if(!width)
            return MakeUnexpected(width.error());
        group.width = *width;
        const auto height = ReadU32(bytes, cursor);
        if(!height)
            return MakeUnexpected(height.error());
        group.height = *height;
        const auto channelCount = ReadU32(bytes, cursor);
        if(!channelCount)
            return MakeUnexpected(channelCount.error());
        group.channelCount = *channelCount;
        const auto byteCount = ReadU32(bytes, cursor);
        if(!byteCount)
            return MakeUnexpected(byteCount.error());
        if(!ReadBytes(bytes, cursor, sizeof(group.sha256.bytes), group.sha256.bytes))
            return MakeUnexpected(Failure{});
        if(
            group.width == 0u || group.height == 0u || group.width > s_FontAtlasMaxExtent || group.height > s_FontAtlasMaxExtent
            || group.channelCount == 0u || group.channelCount > 4u
        )
            return MakeUnexpected(Failure{});
        const u64 expectedBytes = static_cast<u64>(group.width) * group.height * group.channelCount;
        if(
            expectedBytes != *byteCount || pixelBytes + *byteCount > s_FontAtlasMaxPixelBytes
            || cursor > bytes.size() || *byteCount > bytes.size() - cursor
        )
            return MakeUnexpected(Failure{});
        group.pixels.resize(*byteCount);
        if(!ReadBytes(bytes, cursor, *byteCount, group.pixels.data()))
            return MakeUnexpected(Failure{});
        pixelBytes += *byteCount;
        payload.groups.push_back(Move(group));
    }
    payload.positioningTables.reserve(*tableCount);
    u64 tableBytes = 0u;
    for(u32 index = 0u; index < *tableCount; ++index){
        FontAtlasPositioningTable table(arena);
        const auto tag = ReadU32(bytes, cursor);
        if(!tag)
            return MakeUnexpected(tag.error());
        table.tag = *tag;
        const auto byteCount = ReadU32(bytes, cursor);
        if(!byteCount)
            return MakeUnexpected(byteCount.error());
        if(!ReadBytes(bytes, cursor, sizeof(table.sha256.bytes), table.sha256.bytes))
            return MakeUnexpected(Failure{});
        if(
            *byteCount == 0u || tableBytes + *byteCount > s_FontAtlasMaxPositioningBytes
            || cursor > bytes.size() || *byteCount > bytes.size() - cursor
        )
            return MakeUnexpected(Failure{});
        table.bytes.resize(*byteCount);
        if(!ReadBytes(bytes, cursor, *byteCount, table.bytes.data()))
            return MakeUnexpected(Failure{});
        tableBytes += *byteCount;
        payload.positioningTables.push_back(Move(table));
    }
    if(cursor != bytes.size() || !ValidateFontAtlasPayload(payload))
        return MakeUnexpected(Failure{});
    return payload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetBytes> SerializeFontAtlasPayload(
    const FontAtlasPayload& payload,
    Core::Assets::AssetArena& arena
){
    using namespace __hidden_font_atlas_binary;
    if(!ValidateFontAtlasPayload(payload))
        return MakeUnexpected(Failure{});
    Core::Assets::AssetBytes binary(arena);
    usize size = FontAtlasBinaryPayload::s_HeaderBytes + payload.glyphs.size() * FontAtlasBinaryPayload::s_GlyphBytes;
    for(const FontAtlasGroup& group : payload.groups)
        size += FontAtlasBinaryPayload::s_GroupHeaderBytes + group.pixels.size();
    for(const FontAtlasPositioningTable& table : payload.positioningTables)
        size += FontAtlasBinaryPayload::s_TableHeaderBytes + table.bytes.size();
    binary.reserve(size);
    AppendU32(binary, FontAtlasBinaryPayload::s_Magic);
    AppendU32(binary, FontAtlasBinaryPayload::s_Version);
    AppendU32(binary, s_FontAtlasDistanceEncoding);
    AppendU32(binary, 0u);
    for(const u64 lane : payload.font.name().hash().qwords){
        AppendU32(binary, static_cast<u32>(lane));
        AppendU32(binary, static_cast<u32>(lane >> 32u));
    }
    binary.insert(binary.end(), payload.fontSha256.bytes, payload.fontSha256.bytes + sizeof(payload.fontSha256.bytes));
    AppendU32(binary, payload.faceIndex);
    AppendU32(binary, payload.unitsPerEm);
    AppendU32(binary, payload.sourceGlyphCount);
    AppendU32(binary, payload.bakePpem);
    AppendU32(binary, payload.spreadPixels);
    AppendU32(binary, payload.guardTexels);
    AppendU32(binary, static_cast<u32>(payload.rasterMode));
    AppendFloat(binary, payload.ascenderUnits);
    AppendFloat(binary, payload.descenderUnits);
    AppendFloat(binary, payload.lineGapUnits);
    AppendU32(binary, static_cast<u32>(payload.glyphs.size()));
    AppendU32(binary, static_cast<u32>(payload.groups.size()));
    AppendU32(binary, static_cast<u32>(payload.positioningTables.size()));
    for(const FontAtlasGlyph& glyph : payload.glyphs){
        AppendU32(binary, glyph.glyphId); AppendU32(binary, glyph.group); AppendU32(binary, glyph.channel);
        AppendU32(binary, glyph.x); AppendU32(binary, glyph.y); AppendU32(binary, glyph.width); AppendU32(binary, glyph.height);
        AppendFloat(binary, glyph.planeLeft); AppendFloat(binary, glyph.planeTop);
        AppendFloat(binary, glyph.planeRight); AppendFloat(binary, glyph.planeBottom);
        AppendFloat(binary, glyph.advanceUnits); AppendU32(binary, glyph.drawable);
    }
    for(const FontAtlasGroup& group : payload.groups){
        AppendU32(binary, group.width); AppendU32(binary, group.height); AppendU32(binary, group.channelCount);
        AppendU32(binary, static_cast<u32>(group.pixels.size()));
        binary.insert(binary.end(), group.sha256.bytes, group.sha256.bytes + sizeof(group.sha256.bytes));
        binary.insert(binary.end(), group.pixels.begin(), group.pixels.end());
    }
    for(const FontAtlasPositioningTable& table : payload.positioningTables){
        AppendU32(binary, table.tag); AppendU32(binary, static_cast<u32>(table.bytes.size()));
        binary.insert(binary.end(), table.sha256.bytes, table.sha256.bytes + sizeof(table.sha256.bytes));
        binary.insert(binary.end(), table.bytes.begin(), table.bytes.end());
    }
    return binary;
}

Expected<FontAtlasPayload> DeserializeFontAtlasPayload(
    const Core::Assets::AssetBytes& binary,
    Core::Assets::AssetArena& arena
){
    auto payload = __hidden_font_atlas_binary::Deserialize(binary, arena);
    if(!payload){
        NWB_LOGGER_ERROR(NWB_TEXT("FontAtlas binary failed: malformed, noncanonical, unsupported, or invalid content"));
        return MakeUnexpected(payload.error());
    }
    return payload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

