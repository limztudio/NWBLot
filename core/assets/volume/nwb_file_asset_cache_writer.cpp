// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "nwb_file_asset_cache_writer.h"

#include "asset_file_publisher.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookedObjectCacheDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchBytes = Vector<u8, ScratchArena>;
using ScratchPath = ::Path<ScratchArena>;


b8 NwbFileAssetCacheWriter::Write(
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
        if(!EnsureDirectories(scratchCachePath.parentPath()) || !AssetFilePublisher::Write(scratchCachePath, cacheBytes, true, existingBytes))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

