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
    const Path& objectPath
){
    if(!ValidateManifestVirtualPath(virtualPath))
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
    entry.objectPath = objectPath;
    return true;
}

bool AppendPayloadBytesToManifest(
    AssetVolumePackManifest& manifest,
    const Name& virtualPath,
    const void* payloadBytes,
    const usize payloadByteCount
){
    if(!ValidateManifestVirtualPath(virtualPath))
        return false;
    if(payloadByteCount > 0u && payloadBytes == nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: payload manifest entry '{}' has null bytes")
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    Core::Assets::AssetArena& arena = manifest.entries.get_allocator().arena();
    manifest.entries.emplace_back(arena);
    AssetVolumePackEntry& entry = manifest.entries.back();
    entry.virtualPath = virtualPath;
    entry.source = AssetVolumePackEntrySource::PayloadBytes;
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
