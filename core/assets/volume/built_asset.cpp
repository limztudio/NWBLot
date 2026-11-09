// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "built_asset.h"
#include "asset_file_publisher.h"

#include <core/common/log.h>

#include <global/binary.h>
#include <global/numeric_text.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_built_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchBytes = Vector<u8, ScratchArena>;
using ScratchPath = ::Path<ScratchArena>;

inline constexpr u32 s_Magic = 0x4142574eu;
inline constexpr u32 s_Version = 1u;
inline constexpr usize s_HeaderSize = sizeof(u32) * 2u + sizeof(NameHash) + sizeof(u64) * 2u;
inline constexpr usize s_FilenameHexFieldCount = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BuiltAssetDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 WriteBuiltAssets(const Path& outputDirectory, const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, Alloc::ScratchArena& scratchArena){
    if(manifest.entries.size() != manifest.plannedFileCount || !EnsureDirectories(outputDirectory)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid build manifest or output directory '{}'"), PathToString<tchar>(outputDirectory));
        return false;
    }

    __hidden_built_asset::ScratchBytes artifactBytes(scratchArena);
    __hidden_built_asset::ScratchBytes existingBytes(scratchArena);
    __hidden_built_asset::ScratchBytes manifestBytes(scratchArena);
    ScratchString filename(scratchArena);
    filename.reserve(s_HexU64DigitCount * __hidden_built_asset::s_FilenameHexFieldCount + s_Extension.size());
    const __hidden_built_asset::ScratchPath scratchOutputDirectory(scratchArena, outputDirectory);
    __hidden_built_asset::ScratchPath path(scratchArena);
    usize manifestLineBytes = s_HexU64DigitCount * __hidden_built_asset::s_FilenameHexFieldCount + s_Extension.size();
    usize manifestReserveBytes = 0u;
    if(AddBinaryReserveBytes(manifestLineBytes, 1u) && AddBinaryRepeatedReserveBytes(manifestReserveBytes, manifest.entries.size(), manifestLineBytes))
        manifestBytes.reserve(manifestReserveBytes);
    u64 artifactIndex = 0u;
    for(const auto& entry : manifest.entries){
        const u8* const payloadData = entry.payloadBytes.data();
        const usize payloadSize = entry.payloadBytes.size();
        if(entry.identity.cookKeyHash == 0u || entry.identity.payloadSize != payloadSize || entry.identity.payloadHash != ComputeFnv64Bytes(payloadData, payloadSize)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid payload identity '{}'"), ConvertString(entry.virtualPath.resolvedText()));
            return false;
        }
        if(payloadSize > Limit<usize>::s_Max - __hidden_built_asset::s_HeaderSize){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: built asset payload is too large"));
            return false;
        }

        artifactBytes.clear();
        artifactBytes.reserve(__hidden_built_asset::s_HeaderSize + payloadSize);
        AppendPOD(artifactBytes, __hidden_built_asset::s_Magic);
        AppendPOD(artifactBytes, __hidden_built_asset::s_Version);
        AppendPOD(artifactBytes, entry.virtualPath.hash());
        AppendPOD(artifactBytes, entry.identity.payloadSize);
        AppendPOD(artifactBytes, entry.identity.payloadHash);
        AppendBytesNoReserveUnchecked(artifactBytes, payloadData, payloadSize);

        // Preserve files referenced by the previous manifest if a later entry fails during this publication.
        for(;;){
            if(artifactIndex == Limit<u64>::s_Max){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: built asset filename slots are exhausted"));
                return false;
            }
            filename.clear();
            AppendHexU64(artifactIndex++, filename);
            AppendHexU64(entry.identity.payloadHash, filename);
            AppendHexU64(entry.identity.cookKeyHash, filename);
            filename += s_Extension;
            path = scratchOutputDirectory;
            path /= filename;
            const auto read = ReadBinaryFile(path, existingBytes);
            if(read){
                if(existingBytes == artifactBytes)
                    break;
                continue;
            }
            if(!IsMissingPathError(read.error())){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: failed to inspect built asset '{}'"), PathToString<tchar>(path));
                return false;
            }
            if(!AssetFilePublisher::Write(path, artifactBytes, false, existingBytes))
                return false;
            break;
        }
        AppendTextBytesNoReserveUnchecked(manifestBytes, AStringView(filename));
        manifestBytes.push_back(static_cast<u8>('\n'));
    }

    path = scratchOutputDirectory;
    path /= s_ManifestFilename;
    return AssetFilePublisher::Write(path, manifestBytes, true, existingBytes);
}

Expected<BuiltAssetPayload> ReadBuiltAsset(const Path& path, AssetBytes& inOutBytes){
    if(!ReadBinaryFile(path, inOutBytes) || inOutBytes.size() < __hidden_built_asset::s_HeaderSize){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid built asset '{}'"), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }

    usize cursor = 0u;
    const u32 magic = *ReadPOD<u32>(inOutBytes, cursor);
    const u32 version = *ReadPOD<u32>(inOutBytes, cursor);
    const NameHash virtualPathHash = *ReadPOD<NameHash>(inOutBytes, cursor);
    const u64 payloadSize = *ReadPOD<u64>(inOutBytes, cursor);
    const u64 payloadHash = *ReadPOD<u64>(inOutBytes, cursor);
    if(magic != __hidden_built_asset::s_Magic || version != __hidden_built_asset::s_Version || payloadSize != inOutBytes.size() - cursor || payloadHash != ComputeFnv64Bytes(inOutBytes.data() + cursor, inOutBytes.size() - cursor) || !Name(virtualPathHash)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid built asset '{}'"), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }
    return BuiltAssetPayload{ .virtualPath = Name(virtualPathHash), .payloadOffset = cursor };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

