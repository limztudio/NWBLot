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

[[nodiscard]] static bool ReadU32(const Core::Assets::AssetBytes& bytes, usize& cursor, u32& outValue){
    if(cursor > bytes.size() || bytes.size() - cursor < 4u)
        return false;
    outValue = static_cast<u32>(bytes[cursor]) | (static_cast<u32>(bytes[cursor + 1u]) << 8u)
        | (static_cast<u32>(bytes[cursor + 2u]) << 16u) | (static_cast<u32>(bytes[cursor + 3u]) << 24u);
    cursor += 4u;
    return true;
}

[[nodiscard]] static bool ReadFloat(const Core::Assets::AssetBytes& bytes, usize& cursor, f32& outValue){
    u32 value = 0u;
    if(!ReadU32(bytes, cursor, value))
        return false;
    outValue = BitCast<f32>(value);
    return true;
}

[[nodiscard]] static bool ReadBytes(const Core::Assets::AssetBytes& bytes, usize& cursor, const usize count, void* destination){
    if(cursor > bytes.size() || count > bytes.size() - cursor)
        return false;
    if(count > 0u)
        GLOBAL_MEMCPY(destination, count, bytes.data() + cursor, count);
    cursor += count;
    return true;
}

[[nodiscard]] static bool ReadGlyph(const Core::Assets::AssetBytes& bytes, usize& cursor, FontAtlasGlyph& glyph){
    return
        ReadU32(bytes, cursor, glyph.glyphId) && ReadU32(bytes, cursor, glyph.group)
        && ReadU32(bytes, cursor, glyph.channel) && ReadU32(bytes, cursor, glyph.x) && ReadU32(bytes, cursor, glyph.y)
        && ReadU32(bytes, cursor, glyph.width) && ReadU32(bytes, cursor, glyph.height)
        && ReadFloat(bytes, cursor, glyph.planeLeft) && ReadFloat(bytes, cursor, glyph.planeTop)
        && ReadFloat(bytes, cursor, glyph.planeRight) && ReadFloat(bytes, cursor, glyph.planeBottom)
        && ReadFloat(bytes, cursor, glyph.advanceUnits) && ReadU32(bytes, cursor, glyph.drawable)
    ;
}

[[nodiscard]] static bool Deserialize(const Core::Assets::AssetBytes& bytes, FontAtlasPayload& payload){
    usize cursor = 0u;
    u32 magic = 0u, version = 0u, encoding = 0u, reserved = 0u;
    if(
        !ReadU32(bytes, cursor, magic) || !ReadU32(bytes, cursor, version) || !ReadU32(bytes, cursor, encoding)
        || !ReadU32(bytes, cursor, reserved) || magic != FontAtlasBinaryPayload::s_Magic
        || version != FontAtlasBinaryPayload::s_Version || encoding != s_FontAtlasDistanceEncoding || reserved != 0u
    )
        return false;
    NameHash identity = {};
    for(u64& lane : identity.qwords){
        u32 low = 0u, high = 0u;
        if(!ReadU32(bytes, cursor, low) || !ReadU32(bytes, cursor, high))
            return false;
        lane = static_cast<u64>(low) | (static_cast<u64>(high) << 32u);
    }
    payload.font.virtualPath = Name(identity);
    u32 raster = 0u, glyphCount = 0u, groupCount = 0u, tableCount = 0u;
    if(
        !ReadBytes(bytes, cursor, sizeof(payload.fontSha256.bytes), payload.fontSha256.bytes)
        || !ReadU32(bytes, cursor, payload.faceIndex) || !ReadU32(bytes, cursor, payload.unitsPerEm)
        || !ReadU32(bytes, cursor, payload.sourceGlyphCount) || !ReadU32(bytes, cursor, payload.bakePpem)
        || !ReadU32(bytes, cursor, payload.spreadPixels) || !ReadU32(bytes, cursor, payload.guardTexels)
        || !ReadU32(bytes, cursor, raster) || !ReadFloat(bytes, cursor, payload.ascenderUnits)
        || !ReadFloat(bytes, cursor, payload.descenderUnits) || !ReadFloat(bytes, cursor, payload.lineGapUnits)
        || !ReadU32(bytes, cursor, glyphCount) || !ReadU32(bytes, cursor, groupCount) || !ReadU32(bytes, cursor, tableCount)
    )
        return false;
    if(
        glyphCount == 0u || glyphCount > s_FontAtlasMaxGlyphCount || glyphCount != payload.sourceGlyphCount
        || groupCount == 0u || groupCount > s_FontAtlasMaxGroupCount || tableCount > 3u || raster > FontAtlasRasterMode::Bitmap
    )
        return false;
    payload.rasterMode = static_cast<FontAtlasRasterMode::Enum>(raster);
    const u64 minimumRemaining = static_cast<u64>(glyphCount) * FontAtlasBinaryPayload::s_GlyphBytes
        + static_cast<u64>(groupCount) * FontAtlasBinaryPayload::s_GroupHeaderBytes
        + static_cast<u64>(tableCount) * FontAtlasBinaryPayload::s_TableHeaderBytes;
    if(cursor > bytes.size() || minimumRemaining > bytes.size() - cursor)
        return false;
    payload.glyphs.resize(glyphCount);
    for(FontAtlasGlyph& glyph : payload.glyphs){
        if(!ReadGlyph(bytes, cursor, glyph))
            return false;
    }
    Core::Assets::AssetArena& arena = payload.glyphs.get_allocator().arena();
    payload.groups.reserve(groupCount);
    u64 pixelBytes = 0u;
    for(u32 index = 0u; index < groupCount; ++index){
        FontAtlasGroup group(arena);
        u32 byteCount = 0u;
        if(
            !ReadU32(bytes, cursor, group.width) || !ReadU32(bytes, cursor, group.height)
            || !ReadU32(bytes, cursor, group.channelCount)
            || !ReadU32(bytes, cursor, byteCount) || !ReadBytes(bytes, cursor, sizeof(group.sha256.bytes), group.sha256.bytes)
        )
            return false;
        if(
            group.width == 0u || group.height == 0u || group.width > s_FontAtlasMaxExtent || group.height > s_FontAtlasMaxExtent
            || group.channelCount == 0u || group.channelCount > 4u
        )
            return false;
        const u64 expectedBytes = static_cast<u64>(group.width) * group.height * group.channelCount;
        if(
            expectedBytes != byteCount || pixelBytes + byteCount > s_FontAtlasMaxPixelBytes
            || cursor > bytes.size() || byteCount > bytes.size() - cursor
        )
            return false;
        group.pixels.resize(byteCount);
        if(!ReadBytes(bytes, cursor, byteCount, group.pixels.data()))
            return false;
        pixelBytes += byteCount;
        payload.groups.push_back(Move(group));
    }
    payload.positioningTables.reserve(tableCount);
    u64 tableBytes = 0u;
    for(u32 index = 0u; index < tableCount; ++index){
        FontAtlasPositioningTable table(arena);
        u32 byteCount = 0u;
        if(
            !ReadU32(bytes, cursor, table.tag) || !ReadU32(bytes, cursor, byteCount)
            || !ReadBytes(bytes, cursor, sizeof(table.sha256.bytes), table.sha256.bytes)
        )
            return false;
        if(
            byteCount == 0u || tableBytes + byteCount > s_FontAtlasMaxPositioningBytes
            || cursor > bytes.size() || byteCount > bytes.size() - cursor
        )
            return false;
        table.bytes.resize(byteCount);
        if(!ReadBytes(bytes, cursor, byteCount, table.bytes.data()))
            return false;
        tableBytes += byteCount;
        payload.positioningTables.push_back(Move(table));
    }
    return cursor == bytes.size() && ValidateFontAtlasPayload(payload);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SerializeFontAtlasPayload(const FontAtlasPayload& payload, Core::Assets::AssetBytes& outBinary){
    using namespace __hidden_font_atlas_binary;
    if(!ValidateFontAtlasPayload(payload))
        return false;
    Core::Assets::AssetBytes binary(outBinary.get_allocator().arena());
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
    outBinary = Move(binary);
    return true;
}

bool DeserializeFontAtlasPayload(const Core::Assets::AssetBytes& binary, FontAtlasPayload& outPayload){
    FontAtlasPayload candidate(outPayload.glyphs.get_allocator().arena());
    if(!__hidden_font_atlas_binary::Deserialize(binary, candidate)){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("FontAtlas binary failed: malformed, noncanonical, unsupported, or invalid content"));
        return false;
    }
    outPayload = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

