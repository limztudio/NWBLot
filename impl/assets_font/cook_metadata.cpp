// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"
#include "font_validation.h"

#include <core/assets/paths.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_DiagnosticPrefix = "Font meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReadBoundedSource(const Path& sourcePath, Core::Assets::AssetBytes& outBytes){
    ErrorCode error;
    const u64 byteCount = FileSize(sourcePath, error);
    if(error || byteCount == 0u || byteCount > s_FontMaxSourceBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': source must be a readable file of 1..{} bytes")
            , PathToString<tchar>(sourcePath)
            , s_FontMaxSourceBytes
        );
        return false;
    }

    GlobalFilesystemDetail::InputFileStream stream(sourcePath, GlobalFilesystemDetail::InputFileStream::binary);
    if(!stream.is_open())
        return false;
    outBytes.resize(static_cast<usize>(byteCount));
    stream.read(reinterpret_cast<char*>(outBytes.data()), static_cast<GlobalFilesystemDetail::StreamSize>(byteCount));
    if(stream.gcount() != static_cast<GlobalFilesystemDetail::StreamSize>(byteCount))
        return false;
    char extraByte = 0;
    stream.read(&extraByte, 1);
    if(stream.gcount() != 0 || !stream.eof()){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': source size changed during read"), PathToString<tchar>(sourcePath));
        return false;
    }
    return true;
}

[[nodiscard]] static bool ReadFaceIndex(const Path& path, const Core::Metascript::Value& asset, u32& outFaceIndex){
    const Core::Metascript::Value* faceIndex = Core::Metascript::FindField(asset, "face_index");
    if(!faceIndex)
        return true;
    if(!faceIndex->isInteger() || faceIndex->asInteger() != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': face_index must be integer zero for schema 1"), PathToString<tchar>(path));
        return false;
    }
    outFaceIndex = 0u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseFontCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    FontCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_font_cook_metadata;

    FontCookEntry parsed(outEntry.arena);
    const Core::Metascript::Value& asset = doc.asset();
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return false;
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_DiagnosticPrefix, { "schema_version", "source", "face_index" }))
        return false;
    const Core::Metascript::Value* schema = Core::Metascript::FindField(asset, "schema_version");
    if(!schema || !schema->isInteger() || schema->asInteger() != FontBinaryPayload::s_FontVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': schema_version must be integer {}")
            , PathToString<tchar>(nwbFilePath)
            , FontBinaryPayload::s_FontVersion
        );
        return false;
    }
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, parsed.virtualPath, scratchArena))
        return false;
    if(!ReadFaceIndex(nwbFilePath, asset, parsed.faceIndex))
        return false;

    AStringView source;
    if(!Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, "source", true, source))
        return false;
    const Path sourceName(outEntry.arena, source);
    static constexpr AStringView s_Extensions[] = { ".ttf", ".otf" };
    if(sourceName.empty() || sourceName.is_absolute() || sourceName.filename() != sourceName || !PathHasListedExtension(sourceName, s_Extensions)){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': source must name an adjacent .ttf or .otf file"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    const Path sourcePath = nwbFilePath.parent_path() / sourceName;
    if(!ReadBoundedSource(sourcePath, parsed.fontBytes) || !ValidateFontSource(parsed.fontBytes, parsed.faceIndex))
        return false;

    outEntry.fontBytes = Move(parsed.fontBytes);
    outEntry.virtualPath = parsed.virtualPath;
    outEntry.faceIndex = parsed.faceIndex;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

