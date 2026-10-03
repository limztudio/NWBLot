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
    NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': field '{}' must be an integer in {}..{}")
        , PathToString<tchar>(path)
        , StringConvert(field)
        , minimum
        , maximum
    );
    return false;
}

[[nodiscard]] static bool CheckGroupField(const Path& path, const Value& group, const AStringView field, const u32 expected){
    u32 value = 0u;
    if(!ReadU32(path, group, field, expected, expected, value))
        return false;
    return true;
}

[[nodiscard]] static bool ReadSettings(const Path& path, const Value& asset, FontAtlasPayload& payload){
    if(
        !ReadU32(path, asset, "face_index", 0u, 0u, payload.faceIndex)
        || !ReadU32(path, asset, "units_per_em", 16u, 16384u, payload.unitsPerEm)
        || !ReadU32(path, asset, "glyph_count", 1u, s_FontAtlasMaxGlyphCount, payload.sourceGlyphCount)
        || !ReadU32(path, asset, "bake_ppem", s_FontAtlasMinBakePpem, s_FontAtlasMaxBakePpem, payload.bakePpem)
        || !ReadU32(path, asset, "spread_pixels", s_FontAtlasMinSpreadPixels, s_FontAtlasMaxSpreadPixels, payload.spreadPixels)
        || !ReadU32(path, asset, "guard_texels", 1u, 1u, payload.guardTexels)
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
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': unsupported raster_mode"), PathToString<tchar>(path));
        return false;
    }
    return true;
}

[[nodiscard]] static bool ReadGroups(const Path& path, const Value& asset, PreparedFontSource& source, FontAtlasPayload& payload){
    const Value* groups = Core::Assets::FindMetadataListField(path, asset, s_DiagnosticPrefix, "groups");
    if(!groups)
        return false;
    if(groups->asList().size() != source.groups.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': group count differs from paired font source"), PathToString<tchar>(path));
        return false;
    }
    payload.groups.reserve(source.groups.size());
    for(usize index = 0u; index < source.groups.size(); ++index){
        const Value& map = groups->asList()[index];
        PreparedFontImageGroup& prepared = source.groups[index];
        if(
            !Core::Assets::CheckMetadataAssetMap(path, map, s_DiagnosticPrefix)
            || !Core::Assets::ValidateMetadataAssetFields(path, map, s_DiagnosticPrefix, { "width", "height", "channels" })
            || !CheckGroupField(path, map, "width", prepared.width) || !CheckGroupField(path, map, "height", prepared.height)
            || !CheckGroupField(path, map, "channels", prepared.channelCount)
        )
            return false;
        FontAtlasGroup group(payload.groups.get_allocator().arena());
        group.width = prepared.width;
        group.height = prepared.height;
        group.channelCount = prepared.channelCount;
        group.sha256 = prepared.sha256;
        group.pixels = Move(prepared.pixels);
        payload.groups.push_back(Move(group));
    }
    return true;
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
    Name virtualPath = NAME_NONE;
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
            { "font", "face_index", "units_per_em", "glyph_count", "bake_ppem", "spread_pixels", "guard_texels", "raster_mode",
                "ascender_units", "descender_units", "line_gap_units", "groups", "glyphs" }
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
    fontPath.replace_extension(".font");
    PreparedFontSource source(outEntry.arena);
    if(!ReadPreparedFontSource(fontPath, source, true) || !ReadGroups(nwbFilePath, asset, source, candidate.payload))
        return false;
    candidate.payload.fontSha256 = source.fontSha256;
    Font font(outEntry.arena, candidate.payload.font.name());
    font.setFontBytes(Move(source.fontBytes), source.faceIndex);
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

