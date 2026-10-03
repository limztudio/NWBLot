// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <impl/assets_font/cook.h>

#include <core/assets/paths.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Metascript::Value;
static constexpr AStringView s_DiagnosticPrefix = "Font atlas meta";
static constexpr u64 s_MaxAtlasBinaryBytes = FontAtlasBinaryPayload::s_HeaderBytes
    + static_cast<u64>(s_FontAtlasMaxGlyphCount) * FontAtlasBinaryPayload::s_GlyphBytes
    + static_cast<u64>(s_FontAtlasMaxGroupCount) * FontAtlasBinaryPayload::s_GroupHeaderBytes
    + s_FontAtlasMaxPixelBytes + 3u * FontAtlasBinaryPayload::s_TableHeaderBytes + s_FontAtlasMaxPositioningBytes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReadPairedAtlas(const Path& nwbFilePath, Core::Assets::AssetBytes& outBytes){
    Path atlasPath = nwbFilePath;
    atlasPath.replace_extension(".atlas");
    ErrorCode error;
    const u64 byteCount = FileSize(atlasPath, error);
    if(error || byteCount == 0u || byteCount > s_MaxAtlasBinaryBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': missing, empty, or oversized paired .atlas file"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    GlobalFilesystemDetail::InputFileStream stream(atlasPath, GlobalFilesystemDetail::InputFileStream::binary);
    if(!stream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': failed to open paired .atlas file"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    Core::Assets::AssetBytes candidate(outBytes.get_allocator().arena());
    candidate.resize(static_cast<usize>(byteCount));
    stream.read(reinterpret_cast<char*>(candidate.data()), static_cast<GlobalFilesystemDetail::StreamSize>(byteCount));
    if(stream.gcount() != static_cast<GlobalFilesystemDetail::StreamSize>(byteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': paired .atlas read was truncated"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    char extraByte = 0;
    stream.read(&extraByte, 1);
    if(stream.gcount() != 0 || !stream.eof()){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': paired .atlas changed during read"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    outBytes = Move(candidate);
    return true;
}

[[nodiscard]] static bool CheckU32(const Path& path, const Value& asset, const AStringView field, const u32 expected){
    const Value* value = asset.findField(field);
    if(value && value->isInteger() && FitsU32(value->asInteger()) && static_cast<u32>(value->asInteger()) == expected)
        return true;
    NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': field '{}' must equal prepared atlas value {}")
        , PathToString<tchar>(path)
        , StringConvert(field)
        , expected
    );
    return false;
}

[[nodiscard]] static bool CheckF32(const Path& path, const Value& asset, const AStringView field, const f32 expected){
    f32 value = 0.f;
    if(!Core::Assets::ReadMetadataFiniteF32Field(path, asset, s_DiagnosticPrefix, field, true, value))
        return false;
    if(value == expected)
        return true;
    NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': metric '{}' differs from prepared atlas"), PathToString<tchar>(path), StringConvert(field));
    return false;
}

[[nodiscard]] static bool CheckHash(const Path& path, const Value& asset, const AStringView field, const Sha256Digest& expected){
    AStringView text;
    Sha256Digest hash;
    if(Core::Assets::ReadMetadataStringField(path, asset, s_DiagnosticPrefix, field, true, text) && ParseSha256(text, hash) && hash == expected)
        return true;
    NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': hash '{}' differs from prepared atlas"), PathToString<tchar>(path), StringConvert(field));
    return false;
}

[[nodiscard]] static bool CheckGroups(const Path& path, const Value& asset, const FontAtlasPayload& payload){
    const Value* groups = Core::Assets::FindMetadataListField(path, asset, s_DiagnosticPrefix, "groups");
    if(!groups)
        return false;
    if(groups->asList().size() != payload.groups.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': group count differs from prepared atlas"), PathToString<tchar>(path));
        return false;
    }
    for(usize index = 0u; index < payload.groups.size(); ++index){
        const Value& group = groups->asList()[index];
        const FontAtlasGroup& expected = payload.groups[index];
        if(
            !Core::Assets::CheckMetadataAssetMap(path, group, s_DiagnosticPrefix)
            || !Core::Assets::ValidateMetadataAssetFields(path, group, s_DiagnosticPrefix, { "width", "height", "channels", "sha256" })
            || !CheckU32(path, group, "width", expected.width) || !CheckU32(path, group, "height", expected.height)
            || !CheckU32(path, group, "channels", expected.channelCount) || !CheckHash(path, group, "sha256", expected.sha256)
        )
            return false;
    }
    return true;
}

[[nodiscard]] static bool CheckAtlasMetadata(const Path& path, const Value& asset, const FontAtlasPayload& payload){
    AStringView rasterMode;
    if(
        !CheckU32(path, asset, "schema_version", 1u) || !CheckU32(path, asset, "face_index", payload.faceIndex)
        || !CheckU32(path, asset, "units_per_em", payload.unitsPerEm) || !CheckU32(path, asset, "glyph_count", payload.sourceGlyphCount)
        || !CheckHash(path, asset, "source_sha256", payload.fontSha256)
        || !CheckU32(path, asset, "bake_ppem", payload.bakePpem) || !CheckU32(path, asset, "spread_pixels", payload.spreadPixels)
        || !CheckU32(path, asset, "guard_texels", payload.guardTexels)
        || !CheckF32(path, asset, "ascender_units", payload.ascenderUnits)
        || !CheckF32(path, asset, "descender_units", payload.descenderUnits)
        || !CheckF32(path, asset, "line_gap_units", payload.lineGapUnits)
        || !Core::Assets::ReadMetadataStringField(path, asset, s_DiagnosticPrefix, "raster_mode", true, rasterMode)
        || !CheckGroups(path, asset, payload)
    )
        return false;
    const AStringView expected = payload.rasterMode == FontAtlasRasterMode::Bitmap ? "bitmap" : "outline";
    if(rasterMode != expected){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': raster_mode differs from prepared atlas"), PathToString<tchar>(path));
        return false;
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
    if(
        !virtualPath || !Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix)
        || !Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath, asset, s_DiagnosticPrefix,
            { "schema_version", "font", "face_index", "units_per_em", "glyph_count", "source_sha256", "bake_ppem",
                "spread_pixels", "guard_texels", "raster_mode", "ascender_units", "descender_units", "line_gap_units", "groups" }
        )
    )
        return false;
    FontAtlasCookEntry candidate(outEntry.arena);
    Core::Assets::AssetRef<Font> fontReference;
    if(!Core::Assets::ReadMetadataAssetRefField(nwbFilePath, asset, s_DiagnosticPrefix, "font", true, fontReference))
        return false;
    Core::Assets::AssetBytes binary(outEntry.arena);
    if(!ReadPairedAtlas(nwbFilePath, binary) || !DeserializeFontAtlasPayload(binary, candidate.payload))
        return false;
    const AString<Core::Alloc::ScratchArena> stem = PathToString(scratchArena, nwbFilePath.stem());
    if(stem.empty() || candidate.payload.font.name() != Name(AStringView(stem))){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': atlas font marker differs from paired basename"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    candidate.payload.font = fontReference;
    Font font(outEntry.arena, fontReference.name());
    if(
        !LoadPairedFontCookSource(nwbFilePath, font) || !ValidateFontAtlasSourceMatch(candidate.payload, font)
        || !CheckAtlasMetadata(nwbFilePath, asset, candidate.payload)
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

