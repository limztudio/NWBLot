// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "built_asset.h"
#include "cooked_object_cache.h"

#include <core/common/log.h>
#include <global/binary.h>
#include <global/process.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


namespace __hidden_built_asset{

inline constexpr u32 s_Magic = 0x4142574eu;
inline constexpr u32 s_Version = 1u;
inline constexpr usize s_HeaderSize = sizeof(u32) * 2u + sizeof(NameHash) + sizeof(u64) * 2u;
Atomic<u64> g_TemporarySequence{0u};

static bool WriteIfChanged(const Path& path, const AssetBytes& bytes, AssetBytes& existing){
    ErrorCode error;
    if(ReadBinaryFile(path, existing, error) && existing == bytes)
        return true;

    AssetString temporaryName(".nwb_", path.arena());
    AppendHexU64(CurrentProcessId(), temporaryName);
    temporaryName += '_';
    AppendHexU64(g_TemporarySequence.fetch_add(1u, MemoryOrder::relaxed), temporaryName);
    temporaryName += ".tmp";
    const Path temporary = path.parent_path() / temporaryName;
    if(!WriteBinaryFile(temporary, bytes) || !ReadBinaryFile(temporary, existing, error)
        || existing != bytes || !RenamePath(temporary, path, error)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to publish built asset '{}'"), PathToString<tchar>(path));
        if(!RemoveFile(temporary, error) && error)
            NWB_LOGGER_WARNING(NWB_TEXT("AssetBuilder: failed to remove temporary output '{}'"), PathToString<tchar>(temporary));
        return false;
    }
    return true;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BuiltAssetDetail{

bool WriteBuiltAssets(const Path& outputDirectory, const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest){
    AssetArena& arena = outputDirectory.arena();
    ErrorCode error;
    if(manifest.entries.size() != manifest.plannedFileCount || !EnsureDirectories(outputDirectory, error)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: invalid build manifest or output directory '{}'"), PathToString<tchar>(outputDirectory));
        return false;
    }

    AssetBytes objectBytes(arena);
    AssetBytes artifactBytes(arena);
    AssetBytes existingBytes(arena);
    AssetString manifestText(arena);
    for(const auto& entry : manifest.entries){
        const u8* payloadData = entry.payloadBytes.data();
        usize payloadSize = entry.payloadBytes.size();
        if(entry.source == AssetsVolumeCookDetail::AssetVolumePackEntrySource::ObjectFilePayload){
            AssetsVolumeCookDetail::CookedObjectPayloadView payload;
            if(!AssetsVolumeCookDetail::ReadCookedObjectPayload(entry.objectPath, entry.virtualPath, objectBytes, payload))
                return false;
            payloadData = payload.data;
            payloadSize = payload.size;
        }
        if(entry.identity.payloadSize != payloadSize || entry.identity.payloadHash != ComputeFnv64Bytes(payloadData, payloadSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: invalid payload identity '{}'"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
        if(payloadSize > Limit<usize>::s_Max - __hidden_built_asset::s_HeaderSize){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: built asset payload is too large"));
            return false;
        }

        artifactBytes.clear();
        artifactBytes.reserve(__hidden_built_asset::s_HeaderSize + payloadSize);
        AppendPOD(artifactBytes, __hidden_built_asset::s_Magic);
        AppendPOD(artifactBytes, __hidden_built_asset::s_Version);
        AppendPOD(artifactBytes, entry.virtualPath.hash());
        AppendPOD(artifactBytes, entry.identity.payloadSize);
        AppendPOD(artifactBytes, entry.identity.payloadHash);
        BinaryDetail::AppendBytesNoReserveUnchecked(artifactBytes, payloadData, payloadSize);

        AssetString filename(arena);
        for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane)
            AppendHexU64(entry.virtualPath.hash().qwords[lane], filename);
        filename += '_';
        AppendHexU64(entry.identity.payloadHash, filename);
        filename += s_Extension;
        const Path path = outputDirectory / filename;
        if(!__hidden_built_asset::WriteIfChanged(path, artifactBytes, existingBytes))
            return false;
        manifestText += filename;
        manifestText += '\n';
    }

    artifactBytes.clear();
    artifactBytes.reserve(manifestText.size());
    BinaryDetail::AppendBytesNoReserveUnchecked(artifactBytes, manifestText.data(), manifestText.size());
    return __hidden_built_asset::WriteIfChanged(outputDirectory / s_ManifestFilename, artifactBytes, existingBytes);
}

bool ReadBuiltAsset(const Path& path, AssetBytes& bytes, Name& outVirtualPath, usize& outPayloadOffset){
    outVirtualPath = NAME_NONE;
    outPayloadOffset = 0u;
    ErrorCode error;
    u32 magic = 0u;
    u32 version = 0u;
    NameHash virtualPathHash = {};
    u64 payloadSize = 0u;
    u64 payloadHash = 0u;
    usize cursor = 0u;
    if(!ReadBinaryFile(path, bytes, error)
        || !ReadPOD(bytes, cursor, magic)
        || !ReadPOD(bytes, cursor, version)
        || !ReadPOD(bytes, cursor, virtualPathHash)
        || !ReadPOD(bytes, cursor, payloadSize)
        || !ReadPOD(bytes, cursor, payloadHash)
        || magic != __hidden_built_asset::s_Magic
        || version != __hidden_built_asset::s_Version
        || payloadSize != bytes.size() - cursor
        || payloadHash != ComputeFnv64Bytes(bytes.data() + cursor, bytes.size() - cursor)
        || !Name(virtualPathHash)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: invalid built asset '{}'"), PathToString<tchar>(path));
        return false;
    }

    outVirtualPath = Name(virtualPathHash);
    outPayloadOffset = cursor;
    return true;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

