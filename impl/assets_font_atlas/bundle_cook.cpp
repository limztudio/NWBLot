// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bundle_cook.h"
#include "binary_payload.h"

#include <impl/assets_font/binary_payload.h>

#include <core/assets/paths.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_bundle_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_DiagnosticPrefix = "Font bundle meta";
static constexpr u64 s_MaxAtlasBinaryBytes = FontAtlasBinaryPayload::s_HeaderBytes
    + static_cast<u64>(s_FontAtlasMaxGlyphCount) * FontAtlasBinaryPayload::s_GlyphBytes
    + static_cast<u64>(s_FontAtlasMaxGroupCount) * FontAtlasBinaryPayload::s_GroupHeaderBytes
    + s_FontAtlasMaxPixelBytes + 3u * FontAtlasBinaryPayload::s_TableHeaderBytes + s_FontAtlasMaxPositioningBytes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReadPairedBinary(const Path& path, const u64 limit, Core::Assets::AssetBytes& outBytes){
    ErrorCode error;
    const u64 byteCount = FileSize(path, error);
    if(error || byteCount == 0u || byteCount > limit){
        NWB_LOGGER_ERROR(NWB_TEXT("Font bundle: missing, empty, or oversized paired file '{}'"), PathToString<tchar>(path));
        return false;
    }

    GlobalFilesystemDetail::InputFileStream stream(path, GlobalFilesystemDetail::InputFileStream::binary);
    if(!stream.is_open())
        return false;
    Core::Assets::AssetBytes candidate(outBytes.get_allocator().arena());
    candidate.resize(static_cast<usize>(byteCount));
    stream.read(reinterpret_cast<char*>(candidate.data()), static_cast<GlobalFilesystemDetail::StreamSize>(byteCount));
    if(stream.gcount() != static_cast<GlobalFilesystemDetail::StreamSize>(byteCount))
        return false;
    char extraByte = 0;
    stream.read(&extraByte, 1);
    if(stream.gcount() != 0 || !stream.eof())
        return false;
    outBytes = Move(candidate);
    return true;
}

[[nodiscard]] static bool ReadSchema(const Path& path, const Core::Metascript::Value& asset){
    if(
        !Core::Assets::CheckMetadataAssetMap(path, asset, s_DiagnosticPrefix)
        || !Core::Assets::ValidateMetadataAssetFields(path, asset, s_DiagnosticPrefix, { "schema_version" })
    )
        return false;
    const Core::Metascript::Value* schema = Core::Metascript::FindField(asset, "schema_version");
    if(!schema || !schema->isInteger() || schema->asInteger() != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Font bundle meta '{}': schema_version must be integer one"), PathToString<tchar>(path));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseFontBundleCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    FontBundleCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_font_bundle_cook;
    if(!ReadSchema(nwbFilePath, doc.asset()))
        return false;

    FontBundleCookEntry parsed(outEntry.arena);
    AString<Core::Alloc::ScratchArena> virtualPathText(scratchArena);
    if(!Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, virtualPathText))
        return false;
    parsed.fontVirtualPath = Name(AStringView(virtualPathText));
    virtualPathText.append("_atlas");
    parsed.atlasVirtualPath = Name(AStringView(virtualPathText));
    const AString<Core::Alloc::ScratchArena> stem = PathToString(scratchArena, nwbFilePath.stem());
    if(!parsed.fontVirtualPath || !parsed.atlasVirtualPath || stem.empty())
        return false;

    Path fontPath = nwbFilePath;
    fontPath.replace_extension(".font");
    Path atlasPath = nwbFilePath;
    atlasPath.replace_extension(".atlas");
    Core::Assets::AssetBytes fontBinary(outEntry.arena);
    Core::Assets::AssetBytes atlasBinary(outEntry.arena);
    if(
        !ReadPairedBinary(fontPath, sizeof(FontBinaryPayload::HeaderBinary) + s_FontMaxSourceBytes, fontBinary)
        || !ReadPairedBinary(atlasPath, s_MaxAtlasBinaryBytes, atlasBinary)
    )
        return false;

    Font font(outEntry.arena, parsed.fontVirtualPath);
    if(!font.loadBinary(fontBinary) || !DeserializeFontAtlasPayload(atlasBinary, parsed.atlasPayload))
        return false;
    if(parsed.atlasPayload.font.name() != Name(AStringView(stem))){
        NWB_LOGGER_ERROR(NWB_TEXT("Font bundle meta '{}': atlas font marker differs from paired basename"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    parsed.atlasPayload.font.virtualPath = parsed.fontVirtualPath;
    if(!ValidateFontAtlasSourceMatch(parsed.atlasPayload, font))
        return false;

    parsed.fontBytes.assign(font.fontBytes().begin(), font.fontBytes().end());
    outEntry.fontBytes = Move(parsed.fontBytes);
    outEntry.atlasPayload = Move(parsed.atlasPayload);
    outEntry.fontVirtualPath = parsed.fontVirtualPath;
    outEntry.atlasVirtualPath = parsed.atlasVirtualPath;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

