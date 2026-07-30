// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetVolumePackEntrySource{
    enum Enum : u8{
        PayloadBytes,
        ObjectFilePayload
    };
};

struct AssetVolumePayloadIdentity{
    u64 payloadSize = 0u;
    u64 payloadHash = 0u;
    u64 cookKeyHash = 0u;
};

struct AssetVolumePackEntry{
    Name virtualPath = NAME_NONE;
    AssetVolumePackEntrySource::Enum source = AssetVolumePackEntrySource::PayloadBytes;
    AssetVolumePayloadIdentity identity;
    Path objectPath;
    Core::Assets::AssetBytes payloadBytes;

    explicit AssetVolumePackEntry(Core::Assets::AssetArena& arena)
        : objectPath(arena)
        , payloadBytes(arena)
    {}
};

using AssetVolumePackEntryVector = CookVector<AssetVolumePackEntry>;

struct AssetVolumePackManifest{
    AssetVolumePackEntryVector entries;
    u64 plannedFileCount = 0u;

    explicit AssetVolumePackManifest(Core::Assets::AssetArena& arena)
        : entries(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ReserveAssetVolumePackManifest(AssetVolumePackManifest& manifest, u64 plannedFileCount);
[[nodiscard]] bool AppendObjectFilePayloadToManifest(
    AssetVolumePackManifest& manifest,
    const Name& virtualPath,
    const Path& objectPath,
    const AssetVolumePayloadIdentity& identity
);
[[nodiscard]] bool AppendPayloadBytesToManifest(
    AssetVolumePackManifest& manifest,
    const Name& virtualPath,
    const void* payloadBytes,
    usize payloadByteCount,
    u64 cookKeyHash = 0u
);

template<typename ByteContainer>
[[nodiscard]] bool AppendPayloadBytesToManifest(
    AssetVolumePackManifest& manifest,
    const Name& virtualPath,
    const ByteContainer& payloadBytes,
    const u64 cookKeyHash = 0u
){
    return AppendPayloadBytesToManifest(
        manifest,
        virtualPath,
        payloadBytes.empty() ? nullptr : payloadBytes.data(),
        payloadBytes.size(),
        cookKeyHash
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

