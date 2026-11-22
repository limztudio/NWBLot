// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookMetadataDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using DiscoveredPathSet = HashSet<ScratchString, ScratchArena, Hasher<AStringView>, EqualTo<>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NwbCacheLoadResult{
    enum Enum : u8{
        Miss,
        Hit,
        Error
    };
};

void CopyPairedSidecarRules(Vector<AssetPairedSidecarRule, ScratchArena>& outRules);
[[nodiscard]] b8 ComputeNwbFileAssetCacheData(
    const DiscoveredNwbFile& discoveredNwbFile,
    AStringView metaText,
    const Sha256Digest& producerDigest,
    Span<const AssetPairedSidecarRule> pairedSidecarRules,
    ScratchArena& scratchArena,
    ScratchString& outSourcePathText,
    ScratchString& outCacheFileName,
    Sha256Digest& outParamSha256
);
void PopulateNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    AStringView sourcePathText,
    AStringView cacheFileName,
    const Sha256Digest& paramSha256,
    b8 canUseCache,
    NwbFileAssetCacheRecord& outRecord
);
void PrepareNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    const DiscoveredNwbFile& discoveredNwbFile,
    AStringView metaText,
    const Sha256Digest& producerDigest,
    Span<const AssetPairedSidecarRule> pairedSidecarRules,
    ScratchArena& scratchArena,
    ScratchString& inOutSourcePathText,
    ScratchString& inOutCacheFileName,
    NwbFileAssetCacheRecord& outRecord
);
void PrepareNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    const DiscoveredNwbFile& discoveredNwbFile,
    AStringView metaText,
    const Sha256Digest& producerDigest,
    ScratchArena& scratchArena,
    ScratchString& inOutSourcePathText,
    ScratchString& inOutCacheFileName,
    NwbFileAssetCacheRecord& outRecord
);
[[nodiscard]] NwbCacheLoadResult::Enum TryLoadNwbFileAssetCache(
    NwbFileAssetCacheRecord& inOutRecord,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    const ParsedAssetMetadata& metadata
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

