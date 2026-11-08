// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooked_object_cache.h"

#include "built_asset.h"
#include "nwb_file_asset_cache_writer.h"

#include <core/assets/module.h>
#include <core/assets/volume/arena_names.adhd.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooked_object_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 BuildCookedAssetKeyHash(const Core::Assets::IAssetCodec& codec, const Span<const u8> payload, const AStringView configurationSafeName)noexcept{
    static constexpr u32 s_ObjectCookKeyVersion = 1u;

    const NameHash assetTypeHash = codec.assetType().hash();
    const u64 payloadSize = static_cast<u64>(payload.size());
    const u64 payloadHash = ComputeFnv64Bytes(payload.data(), payload.size());
    const u64 configurationHash = ComputeFnv64Text(configurationSafeName);
    u64 cookKeyHash = s_Fnv64OffsetBasis;
    Fnv64AppendValue(cookKeyHash, s_ObjectCookKeyVersion);
    Fnv64AppendValue(cookKeyHash, configurationHash);
    Fnv64AppendValue(cookKeyHash, assetTypeHash);
    Fnv64AppendValue(cookKeyHash, payloadSize);
    Fnv64AppendValue(cookKeyHash, payloadHash);
    return cookKeyHash;
}

class AssetVolumeObjectCacheWriter final : public Core::Assets::ICookedAssetWriter{
public:
    AssetVolumeObjectCacheWriter(
        Core::Assets::AssetArena& arena,
        const AStringView configurationSafeName,
        AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
        CookedNwbAssetCacheItemVector& cookedCacheItems
    )noexcept
        : m_configurationSafeName(configurationSafeName)
        , m_manifest(manifest)
        , m_cookedCacheItems(cookedCacheItems)
        , m_payloadBinary(arena)
    {}


public:
    virtual b8 writeCookedAsset(
        const TStringView assetKind,
        const Name& virtualPath,
        const Core::Assets::IAsset& asset,
        const Core::Assets::IAssetCodec& codec,
        const Core::Assets::CookEntrySourceLocation& location
    )override{
        m_payloadBinary.clear();
        if(!codec.serialize(asset, m_payloadBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: failed to serialize {} '{}'"), assetKind, ConvertString(virtualPath.resolvedText()));
            return false;
        }

        if(!writeSerializedPayload(virtualPath, codec, m_payloadBinary))
            return false;

        if(location.nwbFileIndex != Limit<usize>::s_Max){
            m_cookedCacheItems.push_back(CookedNwbAssetCacheItem{
                .nwbFileIndex = location.nwbFileIndex,
                .nwbSubIndex = location.nwbSubIndex,
                .assetTypeHash = codec.assetType().hash(),
                .manifestEntryIndex = m_manifest.entries.size() - 1u,
            });
        }
        return true;
    }

    virtual b8 writeCachedCookedAsset(const TStringView, const Name& virtualPath, const Core::Assets::IAssetCodec& codec, const Span<const u8> payloadBytes)override{
        return writeSerializedPayload(virtualPath, codec, payloadBytes);
    }


private:
    b8 writeSerializedPayload(const Name& virtualPath, const Core::Assets::IAssetCodec& codec, const Span<const u8> payloadBytes){
        const u64 cookKeyHash = BuildCookedAssetKeyHash(codec, payloadBytes, m_configurationSafeName);
        return AssetsVolumeCookDetail::AppendPayloadBytesToManifest(virtualPath, payloadBytes, m_manifest, cookKeyHash);
    }


private:
    AStringView m_configurationSafeName;
    AssetsVolumeCookDetail::AssetVolumePackManifest& m_manifest;
    CookedNwbAssetCacheItemVector& m_cookedCacheItems;
    Core::Assets::AssetBytes m_payloadBinary;
};

static b8 BuildRegistryObjectBucketManifestEntries(
    const AStringView configurationSafeName,
    const usize bucketIndex,
    Core::Alloc::GlobalArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    Core::Assets::CookEntryRegistry& entryRegistry,
    RegistryObjectBucketManifest& bucketManifest
){
    Core::Assets::CookEntryPathHashSet bucketSeenVirtualPathHashes(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    AssetVolumeObjectCacheWriter cookedAssetWriter(arena, configurationSafeName, bucketManifest.manifest, bucketManifest.cookedCacheItems);
    Core::Assets::CookEntryWriteContext cookEntryWriteContext{ cookedAssetWriter, bucketSeenVirtualPathHashes, scratchArena };
    return entryRegistry.writeBucket(bucketIndex, cookEntryWriteContext);
}

static b8 RegisterMergedManifestVirtualPath(const Name& virtualPath, Core::Assets::CookEntryPathHashSet& seenVirtualPathHashes){
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: registry manifest produced an empty virtual path"));
        return false;
    }

    if(seenVirtualPathHashes.insert(virtualPath.hash()).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: duplicate registry manifest virtual path '{}'"), ConvertString(virtualPath.resolvedText()));
    return false;
}

static b8 MergeRegistryObjectBucketManifests(const Span<RegistryObjectBucketManifest> bucketManifests, AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, Core::Assets::CookEntryPathHashSet& seenVirtualPathHashes){
    for(RegistryObjectBucketManifest& bucketManifest : bucketManifests){
        const usize baseEntryIndex = manifest.entries.size();
        for(CookedNwbAssetCacheItem& item : bucketManifest.cookedCacheItems)
            item.manifestEntryIndex += baseEntryIndex;
        for(AssetsVolumeCookDetail::AssetVolumePackEntry& entry : bucketManifest.manifest.entries){
            if(!RegisterMergedManifestVirtualPath(entry.virtualPath, seenVirtualPathHashes))
                return false;

            manifest.entries.push_back(Move(entry));
        }
    }

    return true;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildRegistryObjectManifestEntries(
    const AStringView configurationSafeName,
    Core::Alloc::GlobalArena& arena,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    ParsedAssetMetadata& parsedMetadata,
    AssetVolumePackManifest& manifest,
    CookEntryPathHashSet& seenVirtualPathHashes
){
    const usize bucketCount = parsedMetadata.entryRegistry.bucketCount();
    if(bucketCount == 0u){
        const Span<const __hidden_cooked_object_cache::RegistryObjectBucketManifest> emptyBuckets;
        return __hidden_cooked_object_cache::NwbFileAssetCacheWriter::Write(parsedMetadata.fileCacheRecords, emptyBuckets, manifest, scratchArena);
    }

    __hidden_cooked_object_cache::RegistryObjectBucketManifestVector bucketManifests(scratchArena);
    bucketManifests.reserve(bucketCount);
    for(usize bucketIndex = 0u; bucketIndex < bucketCount; ++bucketIndex)
        bucketManifests.emplace_back(arena);

    Atomic<b8> failed{ false };
    const auto parallelWork = cpuScheduler.parallelFor(static_cast<usize>(0u), bucketCount, [&](const usize bucketIndex){
        if(failed.load(MemoryOrder::acquire))
            return;

        Core::Alloc::ScratchArena bucketScratchArena(AssetsVolumeArenaScope::s_CookArena);
        __hidden_cooked_object_cache::RegistryObjectBucketManifest& bucketManifest = bucketManifests[bucketIndex];
        if(__hidden_cooked_object_cache::BuildRegistryObjectBucketManifestEntries(configurationSafeName, bucketIndex, arena, bucketScratchArena, parsedMetadata.entryRegistry, bucketManifest))
            return;

        failed.store(true, MemoryOrder::release);
    });
    if(!parallelWork){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: registry object preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
        return false;
    }
    if(failed.load(MemoryOrder::acquire))
        return false;

    if(!__hidden_cooked_object_cache::MergeRegistryObjectBucketManifests(bucketManifests, manifest, seenVirtualPathHashes))
        return false;

    return __hidden_cooked_object_cache::NwbFileAssetCacheWriter::Write(parsedMetadata.fileCacheRecords, bucketManifests, manifest, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

