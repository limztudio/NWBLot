// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pack_manifest.h"

#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateManifestVirtualPath(const Name& virtualPath){
    if(virtualPath)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: tried to append an unnamed manifest entry"));
    return false;
}

static u64 BuildDefaultCookKeyHash(
    const Name& virtualPath,
    const u64 payloadSize,
    const u64 payloadHash
){
    static constexpr u32 s_DefaultPayloadCookKeyVersion = 1u;
    u64 hash = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(hash, s_DefaultPayloadCookKeyVersion);
    Fnv64AppendValue(hash, virtualPath.hash());
    Fnv64AppendValue(hash, payloadSize);
    Fnv64AppendValue(hash, payloadHash);
    return hash;
}

static AssetVolumePayloadIdentity BuildPayloadIdentity(
    const Name& virtualPath,
    const void* payloadBytes,
    const usize payloadByteCount,
    const u64 cookKeyHash
){
    AssetVolumePayloadIdentity identity;
    identity.payloadSize = static_cast<u64>(payloadByteCount);
    identity.payloadHash = ComputeFnv64Bytes(payloadBytes, payloadByteCount);
    identity.cookKeyHash = cookKeyHash != 0u
        ? cookKeyHash
        : BuildDefaultCookKeyHash(virtualPath, identity.payloadSize, identity.payloadHash);
    return identity;
}

static bool ValidatePayloadIdentity(
    const Name& virtualPath,
    const AssetVolumePayloadIdentity& identity
){
    if(identity.cookKeyHash != 0u)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: manifest entry '{}' has an empty cook key")
        , StringConvert(virtualPath.c_str())
    );
    return false;
}

bool ReserveAssetVolumePackManifest(AssetVolumePackManifest& manifest, const u64 plannedFileCount){
    manifest.plannedFileCount = plannedFileCount;
    if(plannedFileCount > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: planned manifest entry count exceeds container capacity"));
        return false;
    }

    manifest.entries.reserve(static_cast<usize>(plannedFileCount));
    return true;
}

bool AppendObjectFilePayloadToManifest(
    AssetVolumePackManifest& manifest,
    const Name& virtualPath,
    const Path& objectPath,
    const AssetVolumePayloadIdentity& identity
){
    if(!ValidateManifestVirtualPath(virtualPath))
        return false;
    if(!ValidatePayloadIdentity(virtualPath, identity))
        return false;
    if(objectPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: object manifest entry '{}' has an empty cache path")
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    Core::Assets::AssetArena& arena = manifest.entries.get_allocator().arena();
    manifest.entries.emplace_back(arena);
    AssetVolumePackEntry& entry = manifest.entries.back();
    entry.virtualPath = virtualPath;
    entry.source = AssetVolumePackEntrySource::ObjectFilePayload;
    entry.identity = identity;
    entry.objectPath = objectPath;
    return true;
}

bool AppendPayloadBytesToManifest(
    AssetVolumePackManifest& manifest,
    const Name& virtualPath,
    const void* payloadBytes,
    const usize payloadByteCount,
    const u64 cookKeyHash
){
    if(!ValidateManifestVirtualPath(virtualPath))
        return false;
    if(payloadByteCount > 0u && payloadBytes == nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: payload manifest entry '{}' has null bytes")
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    const AssetVolumePayloadIdentity identity = BuildPayloadIdentity(
        virtualPath,
        payloadBytes,
        payloadByteCount,
        cookKeyHash
    );
    if(!ValidatePayloadIdentity(virtualPath, identity))
        return false;

    Core::Assets::AssetArena& arena = manifest.entries.get_allocator().arena();
    manifest.entries.emplace_back(arena);
    AssetVolumePackEntry& entry = manifest.entries.back();
    entry.virtualPath = virtualPath;
    entry.source = AssetVolumePackEntrySource::PayloadBytes;
    entry.identity = identity;
    entry.payloadBytes.reserve(payloadByteCount);
    BinaryDetail::AppendBytesNoReserveUnchecked(entry.payloadBytes, payloadBytes, payloadByteCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
