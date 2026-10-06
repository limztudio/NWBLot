// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_glyph_metadata.h"

#include <impl/assets_font/prepared_source.h>

#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Metascript::Value;
static constexpr AStringView s_DiagnosticPrefix = "Font atlas meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReadU32(
    const Path& path,
    const Value& asset,
    const AStringView field,
    const u32 minimum,
    const u32 maximum,
    u32& outValue){
    const Value* value = asset.findField(field);
    if(value && value->isInteger() && value->asInteger() >= minimum && static_cast<u64>(value->asInteger()) <= maximum){
        outValue = static_cast<u32>(value->asInteger());
        return true;
    }
    NWB_LOGGER_ERROR(GLB_TEXT("Font atlas meta '{}': field '{}' must be an integer in {}..{}")
        , PathToString<tchar>(path)
        , StringConvert(field)
        , minimum
        , maximum
    );
    return false;
}

[[nodiscard]] static u32 ReadBigU32(const u8* bytes)noexcept{
    return
        (static_cast<u32>(bytes[0u]) << 24u) | (static_cast<u32>(bytes[1u]) << 16u)
        | (static_cast<u32>(bytes[2u]) << 8u) | static_cast<u32>(bytes[3u])
    ;
}

[[nodiscard]] static bool ReadSourceFaceMetrics(const Font& font, FontAtlasPayload& payload){
    const Core::Assets::AssetBytes& bytes = font.fontBytes();
    if(bytes.size() < 12u)
        return false;
    const u32 tableCount = (static_cast<u32>(bytes[4u]) << 8u) | bytes[5u];
    if(tableCount == 0u || tableCount > s_FontMaxTableCount || static_cast<u64>(tableCount) * 16u > bytes.size() - 12u)
        return false;
    payload.faceIndex = font.faceIndex();
    for(u32 index = 0u; index < tableCount; ++index){
        const u8* record = bytes.data() + 12u + static_cast<usize>(index) * 16u;
        const u32 tag = ReadBigU32(record);
        const u32 offset = ReadBigU32(record + 8u);
        const u32 length = ReadBigU32(record + 12u);
        if(offset > bytes.size() || length > bytes.size() - offset)
            return false;
        if(tag == 0x68656164u){
            if(length < 20u)
                return false;
            payload.unitsPerEm = (static_cast<u32>(bytes[offset + 18u]) << 8u) | bytes[offset + 19u];
        }
        else if(tag == 0x6d617870u){
            if(length < 6u)
                return false;
            payload.sourceGlyphCount = (static_cast<u32>(bytes[offset + 4u]) << 8u) | bytes[offset + 5u];
        }
    }
    return payload.unitsPerEm >= 16u && payload.unitsPerEm <= 16384u && payload.sourceGlyphCount > 0u;
}

[[nodiscard]] static bool ReadSettings(const Path& path, const Value& asset, FontAtlasPayload& payload){
    if(
        !ReadU32(path, asset, "bake_ppem", s_FontAtlasMinBakePpem, s_FontAtlasMaxBakePpem, payload.bakePpem)
        || !ReadU32(path, asset, "spread_pixels", s_FontAtlasMinSpreadPixels, s_FontAtlasMaxSpreadPixels, payload.spreadPixels)
        || !Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, "ascender_units", true, payload.ascenderUnits)
        || !Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, "descender_units", true, payload.descenderUnits)
        || !Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, "line_gap_units", true, payload.lineGapUnits)
    )
        return false;
    AStringView rasterMode;
    if(!Core::Assets::ReadMetadataStringField(path, asset, s_DiagnosticPrefix, "raster_mode", true, rasterMode))
        return false;
    if(rasterMode == "bitmap")
        payload.rasterMode = FontAtlasRasterMode::Bitmap;
    else if(rasterMode == "outline")
        payload.rasterMode = FontAtlasRasterMode::Outline;
    else{
        NWB_LOGGER_ERROR(GLB_TEXT("Font atlas meta '{}': unsupported raster_mode"), PathToString<tchar>(path));
        return false;
    }
    return true;
}

static void CopySourceGroups(PreparedFontSource& source, FontAtlasPayload& payload){
    payload.groups.reserve(source.groups.size());
    for(PreparedFontImageGroup& prepared : source.groups){
        FontAtlasGroup group(payload.groups.get_allocator().arena());
        group.width = prepared.width;
        group.height = prepared.height;
        group.channelCount = prepared.channelCount;
        group.sha256 = prepared.sha256;
        group.pixels = Move(prepared.pixels);
        payload.groups.push_back(Move(group));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseFontAtlasCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    FontAtlasCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena){
    Name virtualPath = s_NameNone;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, virtualPath, scratchArena))
        return false;
    return ParseFontAtlasCookMetadataValue(virtualPath, nwbFilePath, doc.asset(), outEntry, scratchArena);
}

bool ParseFontAtlasCookMetadataValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    FontAtlasCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_font_atlas_cook_metadata;
    static_cast<void>(scratchArena);
    if(
        !virtualPath || !Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix)
        || !Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath, asset, s_DiagnosticPrefix,
            { "font", "bake_ppem", "spread_pixels", "raster_mode",
                "ascender_units", "descender_units", "line_gap_units", "glyphs" }
        )
    )
        return false;
    FontAtlasCookEntry candidate(outEntry.arena);
    if(
        !Core::Assets::ReadMetadataAssetRefField(nwbFilePath, asset, s_DiagnosticPrefix, "font", true, candidate.payload.font)
        || !ReadSettings(nwbFilePath, asset, candidate.payload)
    )
        return false;
    Path fontPath = nwbFilePath;
    fontPath.replaceExtension(".font");
    PreparedFontSource source(outEntry.arena);
    if(!ReadPreparedFontSource(fontPath, source, true))
        return false;
    CopySourceGroups(source, candidate.payload);
    candidate.payload.fontSha256 = source.fontSha256;
    Font font(outEntry.arena, candidate.payload.font.name());
    font.setFontBytes(Move(source.fontBytes), source.faceIndex);
    if(!ReadSourceFaceMetrics(font, candidate.payload)){
        NWB_LOGGER_ERROR(GLB_TEXT("Font atlas meta '{}': paired font source has invalid face metrics"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(
        !CopyFontAtlasPositioningTables(font, candidate.payload)
        || !ValidateFontAtlasSourceMatch(candidate.payload, font)
        || !ParseFontAtlasGlyphMetadata(nwbFilePath, asset, candidate.payload)
        || !ValidateFontAtlasPayload(candidate.payload)
    )
        return false;
    outEntry.payload = Move(candidate.payload);
    outEntry.virtualPath = virtualPath;
    return true;
}

bool BuildFontAtlasAsset(const FontAtlasCookEntry& entry, FontAtlas& outAtlas){
    FontAtlas candidate(entry.arena, entry.virtualPath);
    FontAtlasPayload payload(entry.payload);
    candidate.setPayload(Move(payload));
    if(!candidate.validatePayload())
        return false;
    outAtlas = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

