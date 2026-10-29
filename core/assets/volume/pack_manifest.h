// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_types.h"

#include <global/span.h>


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
    Name virtualPath = s_NameNone;
    AssetVolumePayloadIdentity identity;
    AssetVolumePackEntrySource::Enum source = AssetVolumePackEntrySource::PayloadBytes;
    Path objectPath;
    Core::Assets::AssetBytes payloadBytes;


public:
    explicit AssetVolumePackEntry(Core::Assets::AssetArena& arena)noexcept
        : objectPath(arena)
        , payloadBytes(arena)
    {}
};

using AssetVolumePackEntryVector = CookVector<AssetVolumePackEntry>;

struct AssetVolumePackManifest{
    AssetVolumePackEntryVector entries;
    u64 plannedFileCount = 0u;


public:
    explicit AssetVolumePackManifest(Core::Assets::AssetArena& arena)noexcept
        : entries(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ReserveAssetVolumePackManifest(AssetVolumePackManifest& manifest, u64 plannedFileCount);
[[nodiscard]] b8 AppendObjectFilePayloadToManifest(AssetVolumePackManifest& manifest, const Name& virtualPath, const Path& objectPath, const AssetVolumePayloadIdentity& identity);
[[nodiscard]] b8 AppendPayloadBytesToManifest(AssetVolumePackManifest& manifest, const Name& virtualPath, const void* payloadBytes, usize payloadByteCount, u64 cookKeyHash = 0u);

[[nodiscard]] inline b8 AppendPayloadBytesToManifest(AssetVolumePackManifest& manifest, const Name& virtualPath, const Span<const u8> payloadBytes, const u64 cookKeyHash = 0u){
    return AppendPayloadBytesToManifest(manifest, virtualPath, payloadBytes.empty() ? nullptr : payloadBytes.data(), payloadBytes.size(), cookKeyHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

