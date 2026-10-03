// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <core/assets/paths.h>

#include <global/filesystem.h>
#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Metascript::Value;
static constexpr AStringView s_DiagnosticPrefix = "Font meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CheckU32(const Path& path, const Value& asset, const AStringView field, const u32 expected){
    const Value* value = asset.findField(field);
    if(value && value->isInteger() && FitsU32(value->asInteger()) && static_cast<u32>(value->asInteger()) == expected)
        return true;
    NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': field '{}' must equal prepared source value {}")
        , PathToString<tchar>(path)
        , StringConvert(field)
        , expected
    );
    return false;
}

[[nodiscard]] static u32 ReadBigU32(const u8* bytes){
    return
        (static_cast<u32>(bytes[0u]) << 24u) | (static_cast<u32>(bytes[1u]) << 16u)
        | (static_cast<u32>(bytes[2u]) << 8u) | static_cast<u32>(bytes[3u])
    ;
}

[[nodiscard]] static bool CheckSourceMetadata(const Path& path, const Value& asset, const Font& font){
    const Core::Assets::AssetBytes& bytes = font.fontBytes();
    u32 unitsPerEm = 0u;
    u32 glyphCount = 0u;
    const u32 tableCount = (static_cast<u32>(bytes[4u]) << 8u) | bytes[5u];
    for(u32 index = 0u; index < tableCount; ++index){
        const u8* record = bytes.data() + 12u + static_cast<usize>(index) * 16u;
        const u32 tag = ReadBigU32(record);
        const u32 offset = ReadBigU32(record + 8u);
        if(tag == 0x68656164u)
            unitsPerEm = (static_cast<u32>(bytes[offset + 18u]) << 8u) | bytes[offset + 19u];
        else if(tag == 0x6d617870u)
            glyphCount = (static_cast<u32>(bytes[offset + 4u]) << 8u) | bytes[offset + 5u];
    }
    AStringView hashText;
    Sha256Digest declaredHash;
    if(
        !CheckU32(path, asset, "schema_version", 1u) || !CheckU32(path, asset, "face_index", font.faceIndex())
        || !CheckU32(path, asset, "units_per_em", unitsPerEm) || !CheckU32(path, asset, "glyph_count", glyphCount)
        || !Core::Assets::ReadMetadataStringField(path, asset, s_DiagnosticPrefix, "source_sha256", true, hashText)
        || !ParseSha256(hashText, declaredHash) || declaredHash != ComputeSha256({ bytes.data(), bytes.size() })
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': visible metadata differs from the prepared font"), PathToString<tchar>(path));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LoadPairedFontCookSource(const Path& nwbFilePath, Font& outFont){
    Path fontPath = nwbFilePath;
    fontPath.replace_extension(".font");
    ErrorCode error;
    const u64 byteCount = FileSize(fontPath, error);
    if(error || byteCount < sizeof(FontBinaryPayload::HeaderBinary) || byteCount > sizeof(FontBinaryPayload::HeaderBinary) + s_FontMaxSourceBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': missing, empty, or oversized paired .font file"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    GlobalFilesystemDetail::InputFileStream stream(fontPath, GlobalFilesystemDetail::InputFileStream::binary);
    if(!stream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': failed to open paired .font file"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    Core::Assets::AssetBytes binary(outFont.fontBytes().get_allocator().arena());
    binary.resize(static_cast<usize>(byteCount));
    stream.read(reinterpret_cast<char*>(binary.data()), static_cast<GlobalFilesystemDetail::StreamSize>(byteCount));
    if(stream.gcount() != static_cast<GlobalFilesystemDetail::StreamSize>(byteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': paired .font read was truncated"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    char extraByte = 0;
    stream.read(&extraByte, 1);
    if(stream.gcount() != 0 || !stream.eof()){
        NWB_LOGGER_ERROR(NWB_TEXT("Font meta '{}': paired .font changed during read"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    return outFont.loadBinary(binary);
}

bool ParseFontCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    FontCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena){
    Name virtualPath = NAME_NONE;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, virtualPath, scratchArena))
        return false;
    return ParseFontCookMetadataValue(virtualPath, nwbFilePath, doc.asset(), outEntry);
}

bool ParseFontCookMetadataValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    FontCookEntry& outEntry){
    using namespace __hidden_font_cook_metadata;
    if(
        !virtualPath || !Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix)
        || !Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath, asset, s_DiagnosticPrefix,
            { "schema_version", "face_index", "units_per_em", "glyph_count", "source_sha256" }
        )
    )
        return false;
    Font font(outEntry.arena, virtualPath);
    if(!LoadPairedFontCookSource(nwbFilePath, font) || !CheckSourceMetadata(nwbFilePath, asset, font))
        return false;
    Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), outEntry.arena);
    outEntry.fontBytes = Move(bytes);
    outEntry.virtualPath = virtualPath;
    outEntry.faceIndex = font.faceIndex();
    return true;
}

bool BuildFontAsset(const FontCookEntry& entry, Font& outFont){
    Font candidate(entry.arena, entry.virtualPath);
    Core::Assets::AssetBytes bytes(entry.fontBytes.begin(), entry.fontBytes.end(), entry.arena);
    candidate.setFontBytes(Move(bytes), entry.faceIndex);
    if(!candidate.validatePayload())
        return false;
    outFont = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

