// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"
#include "binary_payload.h"

#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CheckFontIdentity(const Path& path, const Core::Metascript::Value& asset){
    AStringView identity;
    if(!Core::Assets::ReadMetadataStringField(path, asset, FontAtlasMetadata::s_DiagnosticPrefix, "font", true, identity))
        return false;
    if(identity.empty() || identity.size() > 255u || identity.front() == '/' || identity.back() == '/')
        return false;
    usize segmentBegin = 0u;
    for(usize index = 0u; index <= identity.size(); ++index){
        if(index < identity.size() && identity[index] != '/'){
            const char character = identity[index];
            if(character == '\\' || character == ':' || static_cast<u8>(character) < 32u)
                return false;
            continue;
        }
        const AStringView segment = identity.substr(segmentBegin, index - segmentBegin);
        if(segment.empty() || segment == "." || segment == "..")
            return false;
        segmentBegin = index + 1u;
    }
    return true;
}

[[nodiscard]] static bool CheckDistinctPayloadFiles(const Path& path, const Core::Metascript::Value& asset){
    AStringView seen[11u] = {};
    usize seenCount = 0u;
    static constexpr AStringView s_Sections[] = { "groups", "positioning_tables" };
    for(const AStringView section : s_Sections){
        const Core::Metascript::Value* list = Core::Assets::FindMetadataListField(path, asset, FontAtlasMetadata::s_DiagnosticPrefix, section);
        if(!list || list->asList().size() > (section == "groups" ? s_FontAtlasMaxGroupCount : 3u))
            return false;
        for(const Core::Metascript::Value& record : list->asList()){
            AStringView name;
            if(!Core::Assets::ReadMetadataStringField(path, record, FontAtlasMetadata::s_DiagnosticPrefix, "data", true, name))
                return false;
            for(usize index = 0u; index < seenCount; ++index){
                if(EqualsAsciiIgnoreCase(seen[index], name)){
                    NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': duplicate payload file"), PathToString<tchar>(path));
                    return false;
                }
            }
            seen[seenCount++] = name;
        }
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
    using namespace FontAtlasMetadata;
    const Core::Metascript::Value& asset = doc.asset();
    FontAtlasCookEntry parsed(outEntry.arena);
    if(
        !Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix)
        || !Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath, asset, s_DiagnosticPrefix,
            { "schema_version", "font", "font_sha256", "face_index", "units_per_em", "source_glyph_count", "glyph_policy",
              "bake_ppem", "sdf_renderer", "spread_pixels", "distance_encoding", "guard_texels", "payload_format", "mip_count",
              "ascender_units", "descender_units", "line_gap_units", "groups", "glyphs", "kerning_mode", "positioning_tables" }
        )
    )
        return false;
    u32 schema = 0u, mipCount = 0u;
    FontAtlasPayload& payload = parsed.payload;
    if(
        !ReadU32(nwbFilePath, asset, "schema_version", schema) || schema != FontAtlasBinaryPayload::s_Version
        || !ReadU32(nwbFilePath, asset, "mip_count", mipCount) || mipCount != 1u
        || !CheckToken(nwbFilePath, asset, "glyph_policy", "all")
        || !CheckToken(nwbFilePath, asset, "distance_encoding", "freetype_sdf_u8_v1")
        || !CheckToken(nwbFilePath, asset, "payload_format", "rgba8_linear")
        || !CheckToken(nwbFilePath, asset, "kerning_mode", "opentype_tables")
    )
        return false;
    if(
        !__hidden_font_atlas_cook_metadata::CheckFontIdentity(nwbFilePath, asset)
        || !Core::Assets::ReadMetadataAssetRefField(nwbFilePath, asset, s_DiagnosticPrefix, "font", true, payload.font)
        || !ReadHash(nwbFilePath, asset, "font_sha256", payload.fontSha256)
        || !ReadU32(nwbFilePath, asset, "face_index", payload.faceIndex)
        || !ReadU32(nwbFilePath, asset, "units_per_em", payload.unitsPerEm)
        || !ReadU32(nwbFilePath, asset, "source_glyph_count", payload.sourceGlyphCount)
        || !ReadU32(nwbFilePath, asset, "bake_ppem", payload.bakePpem)
        || !ReadU32(nwbFilePath, asset, "spread_pixels", payload.spreadPixels)
        || !ReadU32(nwbFilePath, asset, "guard_texels", payload.guardTexels)
    )
        return false;
    AStringView renderer;
    if(!Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, "sdf_renderer", true, renderer))
        return false;
    if(renderer == "outline")
        payload.rasterMode = FontAtlasRasterMode::Outline;
    else if(renderer == "bitmap")
        payload.rasterMode = FontAtlasRasterMode::Bitmap;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': unsupported sdf_renderer"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(
        !Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_DiagnosticPrefix, "ascender_units", true, payload.ascenderUnits)
        || !Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_DiagnosticPrefix, "descender_units", true, payload.descenderUnits)
        || !Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_DiagnosticPrefix, "line_gap_units", true, payload.lineGapUnits)
    )
        return false;
    if(
        !__hidden_font_atlas_cook_metadata::CheckDistinctPayloadFiles(nwbFilePath, asset)
        || !ReadGroups(nwbFilePath, asset, payload) || !ReadGlyphs(nwbFilePath, asset, payload)
        || !ReadPositioning(nwbFilePath, asset, payload) || !ValidateFontAtlasPayload(payload)
        || !Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, parsed.virtualPath, scratchArena)
    )
        return false;
    outEntry.payload = Move(parsed.payload);
    outEntry.virtualPath = parsed.virtualPath;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

