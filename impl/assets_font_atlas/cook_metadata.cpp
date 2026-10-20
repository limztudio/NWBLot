// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_glyph_metadata.h"

#include <impl/assets_font/prepared_source.h>
#include <impl/assets_font/source_directory.h>

#include <core/assets/paths.h>

#include <global/binary_big_endian.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Metascript::Value;
static constexpr AStringView s_DiagnosticPrefix = "Font atlas meta";
static constexpr AStringView s_FontField = "font";
static constexpr AStringView s_BakePpemField = "bake_ppem";
static constexpr AStringView s_SpreadPixelsField = "spread_pixels";
static constexpr AStringView s_RasterModeField = "raster_mode";
static constexpr AStringView s_AscenderUnitsField = "ascender_units";
static constexpr AStringView s_DescenderUnitsField = "descender_units";
static constexpr AStringView s_LineGapUnitsField = "line_gap_units";
static constexpr AStringView s_RasterModeBitmap = "bitmap";
static constexpr AStringView s_RasterModeOutline = "outline";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<u32> ReadU32(const Path& path, const Value& asset, const AStringView field, const u32 minimum, const u32 maximum){
    const Value* value = asset.findField(field);
    if(value && value->isInteger() && value->asInteger() >= minimum && static_cast<u64>(value->asInteger()) <= maximum){
        return static_cast<u32>(value->asInteger());
    }
    NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': field '{}' must be an integer in {}..{}"), PathToString<tchar>(path), StringConvert(field), minimum, maximum);
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
            const auto unitsPerEmResult = ReadU16BigEndian(tableResult->bytes, s_FontSfntHeadUnitsPerEmOffset);
            if(!unitsPerEmResult)
                return false;
            payload.unitsPerEm = *unitsPerEmResult;
        }
        else if(tableResult->tag == s_FontSfntMaxpTag){
            const auto glyphCountResult = ReadU16BigEndian(tableResult->bytes, s_FontSfntMaxpGlyphCountOffset);
            if(!glyphCountResult)
                return false;
            payload.sourceGlyphCount = *glyphCountResult;
        }
    }
    return payload.unitsPerEm >= s_FontAtlasMinUnitsPerEm && payload.unitsPerEm <= s_FontAtlasMaxUnitsPerEm && payload.sourceGlyphCount > 0u;
}

[[nodiscard]] static bool ReadSettings(const Path& path, const Value& asset, FontAtlasPayload& payload){
    auto bakePpemResult = ReadU32(path, asset, s_BakePpemField, s_FontAtlasMinBakePpem, s_FontAtlasMaxBakePpem);
    if(!bakePpemResult)
        return false;
    payload.bakePpem = *bakePpemResult;
    auto spreadPixelsResult = ReadU32(path, asset, s_SpreadPixelsField, s_FontAtlasMinSpreadPixels, s_FontAtlasMaxSpreadPixels);
    if(!spreadPixelsResult)
        return false;
    payload.spreadPixels = *spreadPixelsResult;
    auto ascenderUnitsResult = Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, s_AscenderUnitsField, true);
    if(!ascenderUnitsResult)
        return false;
    payload.ascenderUnits = *ascenderUnitsResult;
    auto descenderUnitsResult = Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, s_DescenderUnitsField, true);
    if(!descenderUnitsResult)
        return false;
    payload.descenderUnits = *descenderUnitsResult;
    auto lineGapUnitsResult = Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, s_LineGapUnitsField, true);
    if(!lineGapUnitsResult)
        return false;
    payload.lineGapUnits = *lineGapUnitsResult;
    AStringView rasterMode;
    auto rasterModeResult = Core::Assets::ReadMetadataStringField(path, asset, s_DiagnosticPrefix, s_RasterModeField, true);
    if(!rasterModeResult)
        return false;
    rasterMode = rasterModeResult->text;
    if(rasterMode == s_RasterModeBitmap)
        payload.rasterMode = FontAtlasRasterMode::Bitmap;
    else if(rasterMode == s_RasterModeOutline)
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


Expected<FontAtlasCookEntry> ParseFontAtlasCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    Name virtualPath = s_NameNone;
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    virtualPath = *virtualPathResult;
    return ParseFontAtlasCookMetadataValue(virtualPath, nwbFilePath, doc.asset(), arena, scratchArena);
}

Expected<FontAtlasCookEntry> ParseFontAtlasCookMetadataValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_font_atlas_cook_metadata;
    if(
        !virtualPath || !Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix)
        || !Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_DiagnosticPrefix, { s_FontField, s_BakePpemField, s_SpreadPixelsField, s_RasterModeField, s_AscenderUnitsField, s_DescenderUnitsField, s_LineGapUnitsField, s_FontAtlasGlyphsField })
    )
        return MakeUnexpected(Failure{});
    FontAtlasCookEntry candidate(arena);
    auto fontResult = Core::Assets::ReadMetadataAssetRefField<Font>(nwbFilePath, asset, s_DiagnosticPrefix, s_FontField, true);
    if(!fontResult)
        return MakeUnexpected(Failure{});
    candidate.payload.font = *fontResult;
    if(!ReadSettings(nwbFilePath, asset, candidate.payload))
        return MakeUnexpected(Failure{});
    Path fontPath = nwbFilePath;
    fontPath.replaceExtension(s_PreparedFontExtension);
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
    if(!ValidateFontAtlasPayload(candidate.payload, scratchArena))
        return MakeUnexpected(Failure{});
    candidate.virtualPath = virtualPath;
    return candidate;
}

Expected<FontAtlas> BuildFontAtlasAsset(const FontAtlasCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    FontAtlas candidate(arena, entry.virtualPath);
    const FontAtlasPayload& source = entry.payload;
    FontAtlasPayload payload(arena);
    payload.font = source.font;
    payload.fontSha256 = source.fontSha256;
    payload.faceIndex = source.faceIndex;
    payload.unitsPerEm = source.unitsPerEm;
    payload.sourceGlyphCount = source.sourceGlyphCount;
    payload.bakePpem = source.bakePpem;
    payload.spreadPixels = source.spreadPixels;
    payload.guardTexels = source.guardTexels;
    payload.rasterMode = source.rasterMode;
    payload.ascenderUnits = source.ascenderUnits;
    payload.descenderUnits = source.descenderUnits;
    payload.lineGapUnits = source.lineGapUnits;
    payload.glyphs.assign(source.glyphs.begin(), source.glyphs.end());
    payload.groups.reserve(source.groups.size());
    for(const FontAtlasGroup& sourceGroup : source.groups){
        FontAtlasGroup group(arena);
        group.width = sourceGroup.width;
        group.height = sourceGroup.height;
        group.channelCount = sourceGroup.channelCount;
        group.sha256 = sourceGroup.sha256;
        group.pixels.assign(sourceGroup.pixels.begin(), sourceGroup.pixels.end());
        payload.groups.push_back(Move(group));
    }
    payload.positioningTables.reserve(source.positioningTables.size());
    for(const FontAtlasPositioningTable& sourceTable : source.positioningTables){
        FontAtlasPositioningTable table(arena);
        table.tag = sourceTable.tag;
        table.sha256 = sourceTable.sha256;
        table.bytes.assign(sourceTable.bytes.begin(), sourceTable.bytes.end());
        payload.positioningTables.push_back(Move(table));
    }
    candidate.setPayload(Move(payload));
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

