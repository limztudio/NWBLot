// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_glyph_metadata.h"

#include <impl/assets_font/prepared_source.h>
#include <impl/assets_font/source_directory.h>

#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Metascript::Value;
static constexpr AStringView s_DiagnosticPrefix = "Font atlas meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<u32> ReadU32(
    const Path& path,
    const Value& asset,
    const AStringView field,
    const u32 minimum,
    const u32 maximum
){
    const Value* value = asset.findField(field);
    if(value && value->isInteger() && value->asInteger() >= minimum && static_cast<u64>(value->asInteger()) <= maximum){
        return static_cast<u32>(value->asInteger());
    }
    NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': field '{}' must be an integer in {}..{}")
        , PathToString<tchar>(path)
        , StringConvert(field)
        , minimum
        , maximum
    );
    return MakeUnexpected(Failure{});
}

[[nodiscard]] static bool ReadSourceFaceMetrics(const Font& font, FontAtlasPayload& payload)noexcept{
    const Core::Assets::AssetBytes& bytes = font.fontBytes();
    const auto directoryResult = FontSfntDirectory::Read({ bytes.data(), bytes.size() });
    if(!directoryResult)
        return false;
    const FontSfntDirectory& directory = *directoryResult;
    payload.faceIndex = font.faceIndex();
    for(u16 index = 0u; index < directory.tableCount(); ++index){
        const auto tableResult = directory.table(index);
        if(!tableResult)
            return false;
        if(tableResult->tag == s_FontSfntHeadTag){
            const auto unitsPerEmResult = ReadFontSfntU16(tableResult->bytes, 18u);
            if(!unitsPerEmResult)
                return false;
            payload.unitsPerEm = *unitsPerEmResult;
        }
        else if(tableResult->tag == s_FontSfntMaxpTag){
            const auto glyphCountResult = ReadFontSfntU16(tableResult->bytes, 4u);
            if(!glyphCountResult)
                return false;
            payload.sourceGlyphCount = *glyphCountResult;
        }
    }
    return payload.unitsPerEm >= 16u && payload.unitsPerEm <= 16384u && payload.sourceGlyphCount > 0u;
}

[[nodiscard]] static bool ReadSettings(const Path& path, const Value& asset, FontAtlasPayload& payload){
    auto bakePpemResult = ReadU32(path, asset, "bake_ppem", s_FontAtlasMinBakePpem, s_FontAtlasMaxBakePpem);
    if(!bakePpemResult)
        return false;
    payload.bakePpem = *bakePpemResult;
    auto spreadPixelsResult = ReadU32(path, asset, "spread_pixels", s_FontAtlasMinSpreadPixels, s_FontAtlasMaxSpreadPixels);
    if(!spreadPixelsResult)
        return false;
    payload.spreadPixels = *spreadPixelsResult;
    auto ascenderUnitsResult = Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, "ascender_units", true);
    if(!ascenderUnitsResult)
        return false;
    payload.ascenderUnits = *ascenderUnitsResult;
    auto descenderUnitsResult = Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, "descender_units", true);
    if(!descenderUnitsResult)
        return false;
    payload.descenderUnits = *descenderUnitsResult;
    auto lineGapUnitsResult = Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, "line_gap_units", true);
    if(!lineGapUnitsResult)
        return false;
    payload.lineGapUnits = *lineGapUnitsResult;
    AStringView rasterMode;
    auto rasterModeResult = Core::Assets::ReadMetadataStringField(path, asset, s_DiagnosticPrefix, "raster_mode", true);
    if(!rasterModeResult)
        return false;
    rasterMode = rasterModeResult->text;
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


Expected<FontAtlasCookEntry> ParseFontAtlasCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    Name virtualPath = s_NameNone;
    auto virtualPathResult = Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    virtualPath = *virtualPathResult;
    return ParseFontAtlasCookMetadataValue(virtualPath, nwbFilePath, doc.asset(), arena, scratchArena);
}

Expected<FontAtlasCookEntry> ParseFontAtlasCookMetadataValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
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
        return MakeUnexpected(Failure{});
    FontAtlasCookEntry candidate(arena);
    auto fontResult = Core::Assets::ReadMetadataAssetRefField<Font>(nwbFilePath, asset, s_DiagnosticPrefix, "font", true);
    if(!fontResult)
        return MakeUnexpected(Failure{});
    candidate.payload.font = *fontResult;
    if(!ReadSettings(nwbFilePath, asset, candidate.payload))
        return MakeUnexpected(Failure{});
    Path fontPath = nwbFilePath;
    fontPath.replaceExtension(".font");
    auto sourceResult = ReadPreparedFontSource(fontPath, arena, true);
    if(!sourceResult)
        return MakeUnexpected(Failure{});
    PreparedFontSource& source = *sourceResult;
    CopySourceGroups(source, candidate.payload);
    candidate.payload.fontSha256 = source.fontSha256;
    Font font(arena, candidate.payload.font.name());
    font.setFontBytes(Move(source.fontBytes), source.faceIndex);
    if(!ReadSourceFaceMetrics(font, candidate.payload)){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': paired font source has invalid face metrics"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    auto positioningTablesResult = CopyFontAtlasPositioningTables(font, candidate.payload.sourceGlyphCount, arena);
    if(!positioningTablesResult)
        return MakeUnexpected(Failure{});
    candidate.payload.positioningTables = Move(*positioningTablesResult);
    if(!ValidateFontAtlasSourceMatch(candidate.payload, font))
        return MakeUnexpected(Failure{});
    auto glyphsResult = ParseFontAtlasGlyphMetadata(nwbFilePath, asset, candidate.payload);
    if(!glyphsResult)
        return MakeUnexpected(Failure{});
    candidate.payload.glyphs = Move(*glyphsResult);
    if(!ValidateFontAtlasPayload(candidate.payload))
        return MakeUnexpected(Failure{});
    candidate.virtualPath = virtualPath;
    return candidate;
}

Expected<FontAtlas> BuildFontAtlasAsset(const FontAtlasCookEntry& entry, Core::Assets::AssetArena& arena){
    FontAtlas candidate(arena, entry.virtualPath);
    FontAtlasPayload payload(entry.payload);
    candidate.setPayload(Move(payload));
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

