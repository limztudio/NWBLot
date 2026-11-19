// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooked_object_cache.h"

#include "arena_names.h"

#include <core/assets/module.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookedObjectCacheDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RegistryObjectBucketManifest{
    AssetsVolumeCookDetail::AssetVolumePackManifest manifest;


public:
    explicit RegistryObjectBucketManifest(Core::Assets::AssetArena& arena)noexcept
        : manifest(arena)
    {}
};

using RegistryObjectBucketManifestVector = Vector<RegistryObjectBucketManifest, ScratchArena>;


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
        AssetsVolumeCookDetail::AssetVolumePackManifest& manifest
    )noexcept
        : m_configurationSafeName(configurationSafeName)
        , m_manifest(manifest)
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
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: failed to serialize {} '{}'"), assetKind, ConvertString(virtualPath.resolvedText()));
            return false;
        }

        const u64 cookKeyHash = BuildCookedAssetKeyHash(codec, m_payloadBinary, m_configurationSafeName);
        return AssetsVolumeCookDetail::AppendPayloadBytesToManifest(virtualPath, m_payloadBinary, m_manifest, cookKeyHash, location, codec.assetType().hash());
    }


private:
    AStringView m_configurationSafeName;
    AssetsVolumeCookDetail::AssetVolumePackManifest& m_manifest;
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
    AssetVolumeObjectCacheWriter cookedAssetWriter(arena, configurationSafeName, bucketManifest.manifest);
    Core::Assets::CookEntryWriteContext cookEntryWriteContext{ cookedAssetWriter, bucketSeenVirtualPathHashes, scratchArena };
    return entryRegistry.writeBucket(bucketIndex, cookEntryWriteContext);
}

static b8 RegisterMergedManifestVirtualPath(const Name& virtualPath, Core::Assets::CookEntryPathHashSet& seenVirtualPathHashes){
    if(!virtualPath){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: registry manifest produced an empty virtual path"));
        return false;
    }

    if(seenVirtualPathHashes.insert(virtualPath.hash()).second)
        return true;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: duplicate registry manifest virtual path '{}'"), ConvertString(virtualPath.resolvedText()));
    return false;
}

static b8 MergeRegistryObjectBucketManifests(const Span<RegistryObjectBucketManifest> bucketManifests, AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, Core::Assets::CookEntryPathHashSet& seenVirtualPathHashes){
    for(RegistryObjectBucketManifest& bucketManifest : bucketManifests){
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
    if(bucketCount == 0u)
        return true;

    CookedObjectCacheDetail::RegistryObjectBucketManifestVector bucketManifests(scratchArena);
    bucketManifests.reserve(bucketCount);
    for(usize bucketIndex = 0u; bucketIndex < bucketCount; ++bucketIndex)
        bucketManifests.emplace_back(arena);

    Atomic<b8> failed{ false };
    const auto parallelWork = cpuScheduler.parallelFor(static_cast<usize>(0u), bucketCount, [&](const usize bucketIndex){
        if(failed.load(MemoryOrder::acquire))
            return;

        Core::Alloc::ScratchArena bucketScratchArena(AssetsVolumeArenaScope::s_CookArena);
        CookedObjectCacheDetail::RegistryObjectBucketManifest& bucketManifest = bucketManifests[bucketIndex];
        if(CookedObjectCacheDetail::BuildRegistryObjectBucketManifestEntries(configurationSafeName, bucketIndex, arena, bucketScratchArena, parsedMetadata.entryRegistry, bucketManifest))
            return;

        failed.store(true, MemoryOrder::release);
    });
    if(!parallelWork){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: registry object preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
        return false;
    }
    if(failed.load(MemoryOrder::acquire))
        return false;

    return CookedObjectCacheDetail::MergeRegistryObjectBucketManifests(bucketManifests, manifest, seenVirtualPathHashes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

