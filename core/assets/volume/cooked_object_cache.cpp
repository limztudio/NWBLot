// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooked_object_cache.h"

#include "arena_names.h"
#include "built_asset.h"

#include <core/assets/module.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooked_object_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = AssetsVolumeCookDetail::ScratchArena;
using ScratchBytes = Vector<u8, ScratchArena>;
using ScratchPath = ::Path<ScratchArena>;

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

struct CookedNwbAssetCacheItem{
    usize nwbFileIndex = Limit<usize>::s_Max;
    u32 nwbSubIndex = 0u;
    NameHash assetTypeHash = {};
    usize manifestEntryIndex = 0u;
};

using CookedNwbAssetCacheItemVector = Core::Assets::CookVector<CookedNwbAssetCacheItem>;

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
        return AssetsVolumeCookDetail::AppendPayloadBytesToManifest(m_manifest, virtualPath, payloadBytes, cookKeyHash);
    }


private:
    AStringView m_configurationSafeName;
    AssetsVolumeCookDetail::AssetVolumePackManifest& m_manifest;
    CookedNwbAssetCacheItemVector& m_cookedCacheItems;
    Core::Assets::AssetBytes m_payloadBinary;
};

struct RegistryObjectBucketManifest{
    AssetsVolumeCookDetail::AssetVolumePackManifest manifest;
    CookedNwbAssetCacheItemVector cookedCacheItems;


public:
    explicit RegistryObjectBucketManifest(Core::Assets::AssetArena& arena)noexcept
        : manifest(arena)
        , cookedCacheItems(arena)
    {}
};

using RegistryObjectBucketManifestVector = Vector<RegistryObjectBucketManifest, Core::Assets::ScratchArena>;

static b8 BuildRegistryObjectBucketManifestEntries(
    Core::Alloc::GlobalArena& arena,
    const AStringView configurationSafeName,
    const usize bucketIndex,
    Core::Alloc::ScratchArena& scratchArena,
    Core::Assets::CookEntryRegistry& entryRegistry,
    RegistryObjectBucketManifest& bucketManifest
){
    Core::Assets::CookEntryPathHashSet bucketSeenVirtualPathHashes(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    AssetVolumeObjectCacheWriter cookedAssetWriter(arena, configurationSafeName, bucketManifest.manifest, bucketManifest.cookedCacheItems);
    Core::Assets::CookEntryWriteContext cookEntryWriteContext{ cookedAssetWriter, bucketSeenVirtualPathHashes, scratchArena };
    return entryRegistry.writeBucket(bucketIndex, cookEntryWriteContext);
}

static b8 RegisterMergedManifestVirtualPath(const Name& virtualPath, AssetsVolumeCookDetail::VirtualPathHashSet& seenVirtualPathHashes){
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: registry manifest produced an empty virtual path"));
        return false;
    }

    if(seenVirtualPathHashes.insert(virtualPath.hash()).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: duplicate registry manifest virtual path '{}'"), ConvertString(virtualPath.resolvedText()));
    return false;
}

static b8 MergeRegistryObjectBucketManifests(const Span<RegistryObjectBucketManifest> bucketManifests, AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, AssetsVolumeCookDetail::VirtualPathHashSet& seenVirtualPathHashes){
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

[[nodiscard]] static b8 WriteNwbFileAssetCacheEntries(
    const Span<const Core::Assets::NwbFileAssetCacheRecord> fileCacheRecords,
    const Span<const RegistryObjectBucketManifest> bucketManifests,
    const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
    ScratchArena& scratchArena
){
    if(fileCacheRecords.empty())
        return true;

    usize totalCookedItems = 0u;
    for(const RegistryObjectBucketManifest& bucketManifest : bucketManifests)
        totalCookedItems = AddSize(totalCookedItems, bucketManifest.cookedCacheItems.size());

    Vector<NotNull<const CookedNwbAssetCacheItem*>, ScratchArena> sortedItems(scratchArena);
    sortedItems.reserve(totalCookedItems);
    for(const RegistryObjectBucketManifest& bucketManifest : bucketManifests){
        for(const CookedNwbAssetCacheItem& item : bucketManifest.cookedCacheItems)
            sortedItems.push_back(MakeNotNull(&item));
    }

    Sort(sortedItems.begin(), sortedItems.end(), [](const NotNull<const CookedNwbAssetCacheItem*> lhs, const NotNull<const CookedNwbAssetCacheItem*> rhs)noexcept{
        if(lhs->nwbFileIndex != rhs->nwbFileIndex)
            return lhs->nwbFileIndex < rhs->nwbFileIndex;
        return lhs->nwbSubIndex < rhs->nwbSubIndex;
    });

    ScratchBytes payloadSection(scratchArena);
    ScratchBytes cacheBytes(scratchArena);
    ScratchBytes existingBytes(scratchArena);

    usize itemCursor = 0u;
    for(usize fileIndex = 0u; fileIndex < fileCacheRecords.size(); ++fileIndex){
        while(itemCursor < sortedItems.size() && sortedItems[itemCursor]->nwbFileIndex < fileIndex)
            ++itemCursor;
        const usize rangeBegin = itemCursor;
        while(itemCursor < sortedItems.size() && sortedItems[itemCursor]->nwbFileIndex == fileIndex)
            ++itemCursor;
        const usize rangeEnd = itemCursor;

        const Core::Assets::NwbFileAssetCacheRecord& record = fileCacheRecords[fileIndex];
        if(record.loadedFromCache || !record.canWriteCache || record.cacheFilePath.empty())
            continue;

        const usize itemCount = rangeEnd - rangeBegin;
        if(itemCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;

        payloadSection.clear();
        if(!AppendString(payloadSection, AStringView(record.relativePathText)))
            return false;

        for(usize index = rangeBegin; index < rangeEnd; ++index){
            const CookedNwbAssetCacheItem& item = *sortedItems[index];
            const AssetsVolumeCookDetail::AssetVolumePackEntry& entry = manifest.entries[item.manifestEntryIndex];
            AppendPOD(payloadSection, item.nwbSubIndex);
            AppendPOD(payloadSection, item.assetTypeHash);
            AppendPOD(payloadSection, entry.virtualPath.hash());
            if(!AppendString(payloadSection, entry.virtualPath.resolvedText()))
                return false;
            AppendPOD(payloadSection, entry.identity.payloadSize);
            AppendPOD(payloadSection, entry.identity.payloadHash);
            AppendBytesUnchecked(payloadSection, entry.payloadBytes.data(), entry.payloadBytes.size());
        }

        Core::Assets::NwbAssetCacheFileHeader cacheHeader;
        cacheHeader.flags = (record.standalone && itemCount > 0u) ? Core::Assets::s_NwbAssetCacheFlagStandalone : 0u;
        cacheHeader.entryCount = static_cast<u32>(itemCount);
        cacheHeader.paramSha256 = record.paramSha256;
        cacheHeader.payloadSectionSize = static_cast<u64>(payloadSection.size());
        cacheHeader.payloadSectionHashFnv64 = ComputeFnv64Bytes(payloadSection.data(), payloadSection.size());

        cacheBytes.clear();
        cacheBytes.reserve(sizeof(Core::Assets::NwbAssetCacheFileHeader) + payloadSection.size());
        AppendPOD(cacheBytes, cacheHeader);
        AppendBytesNoReserveUnchecked(cacheBytes, payloadSection.data(), payloadSection.size());

        const ScratchPath scratchCachePath(scratchArena, record.cacheFilePath);
        if(!EnsureDirectories(scratchCachePath.parentPath()) || !BuiltAssetDetail::WriteBinaryFileIfChanged(existingBytes, scratchCachePath, cacheBytes))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildRegistryObjectManifestEntries(
    Core::Alloc::GlobalArena& arena,
    Core::CpuTaskScheduler& cpuScheduler,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena,
    ParsedAssetMetadata& parsedMetadata,
    AssetVolumePackManifest& manifest,
    VirtualPathHashSet& seenVirtualPathHashes
){
    const usize bucketCount = parsedMetadata.entryRegistry.bucketCount();
    if(bucketCount == 0u){
        const Span<const __hidden_cooked_object_cache::RegistryObjectBucketManifest> emptyBuckets;
        return __hidden_cooked_object_cache::WriteNwbFileAssetCacheEntries(parsedMetadata.fileCacheRecords, emptyBuckets, manifest, scratchArena);
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
        if(__hidden_cooked_object_cache::BuildRegistryObjectBucketManifestEntries(arena, configurationSafeName, bucketIndex, bucketScratchArena, parsedMetadata.entryRegistry, bucketManifest))
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

    return __hidden_cooked_object_cache::WriteNwbFileAssetCacheEntries(parsedMetadata.fileCacheRecords, bucketManifests, manifest, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

