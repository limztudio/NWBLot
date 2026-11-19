// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_internal.h"
#include "built_asset_format.h"

#include <core/common/log.h>

#include <global/algorithm.h>
#include <global/binary.h>
#include <global/filesystem/operations.h>
#include <global/scope_exit.h>
#include <global/sha1.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_metadata_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 BuildCanonicalAbsolutePathText(const Path& filePath, ScratchArena& scratchArena, ScratchString& outPathText){
    const auto absolutePath = AbsolutePath(filePath);
    if(!absolutePath){
        outPathText.clear();
        return false;
    }

    const ::Path<ScratchArena> canonicalPath(scratchArena, absolutePath->lexicallyNormal());
    outPathText = PathToString(scratchArena, canonicalPath);
    CanonicalizeTextInPlace(outPathText);
    return !outPathText.empty();
}

[[nodiscard]] static b8 AppendPairedSidecarChecksums(const Path& nwbFilePath, ScratchArena& scratchArena, Vector<u8, ScratchArena>& shaInput){
    Vector<AssetPairedSidecarRule, ScratchArena> rules(scratchArena);
    CookMetadataDetail::CopyPairedSidecarRules(rules);
    for(const AssetPairedSidecarRule& rule : rules){
        if(!AppendString(shaInput, rule.sidecarExtension))
            return false;
        ::Path<ScratchArena> sidecarPath(scratchArena, nwbFilePath);
        sidecarPath.replaceExtension(rule.sidecarExtension);
        const auto exists = FileExists(sidecarPath);
        if(!exists)
            return false;
        const u8 present = *exists ? 1u : 0u;
        AppendPOD(shaInput, present);
        if(!*exists)
            continue;
        Vector<u8, ScratchArena> sidecarBytes(scratchArena);
        if(!ReadBinaryFile(sidecarPath, sidecarBytes))
            return false;
        const Sha256Digest digest = ComputeSha256(BinaryByteView{ sidecarBytes.data(), sidecarBytes.size() });
        AppendPOD(shaInput, digest);
    }
    return true;
}

[[nodiscard]] static Sha256Digest ComputeNwbParamChecksum(
    const DiscoveredNwbFile& discoveredNwbFile,
    const AStringView sourcePathText,
    const AStringView metaText,
    const Sha256Digest& producerDigest,
    ScratchArena& scratchArena,
    b8& outCanUseCache
){
    outCanUseCache = true;
    const auto derivedVirtualPath = AssetPathsDetail::BuildDerivedAssetVirtualPathText(
        discoveredNwbFile.assetRoot,
        discoveredNwbFile.virtualRoot.view(),
        discoveredNwbFile.filePath,
        scratchArena,
        scratchArena
    );
    if(!derivedVirtualPath)
        outCanUseCache = false;
    const AStringView derivedVirtualPathView = derivedVirtualPath ? AStringView(*derivedVirtualPath) : AStringView{};

    Vector<u8, ScratchArena> metaBytes(scratchArena);
    AppendBytesUnchecked(metaBytes, metaText.data(), metaText.size());
    const Sha256Digest metaSha256 = ComputeSha256(BinaryByteView{ metaBytes.data(), metaBytes.size() });

    Vector<u8, ScratchArena> shaInput(scratchArena);
    shaInput.reserve(sizeof(u16) + sourcePathText.size() + discoveredNwbFile.virtualRoot.view().size() + derivedVirtualPathView.size() + sizeof(Sha256Digest) * 3u + 16u);
    AppendPOD(shaInput, s_BuiltAssetVersion);
    AppendPOD(shaInput, producerDigest);
    if(
        !AppendString(shaInput, sourcePathText)
        || !AppendString(shaInput, discoveredNwbFile.virtualRoot.view())
        || !AppendString(shaInput, derivedVirtualPathView)
    )
        outCanUseCache = false;
    AppendPOD(shaInput, metaSha256);
    if(!AppendPairedSidecarChecksums(discoveredNwbFile.filePath, scratchArena, shaInput))
        outCanUseCache = false;

    return ComputeSha256(BinaryByteView{ shaInput.data(), shaInput.size() });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookMetadataDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PrepareNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    const DiscoveredNwbFile& discoveredNwbFile,
    const AStringView metaText,
    const Sha256Digest& producerDigest,
    ScratchArena& scratchArena,
    ScratchString& inOutSourcePathText,
    ScratchString& inOutCacheFileName,
    NwbFileAssetCacheRecord& outRecord
){
    const b8 canonicalPathOk = __hidden_cook_metadata_cache::BuildCanonicalAbsolutePathText(discoveredNwbFile.filePath, scratchArena, inOutSourcePathText);
    outRecord.sourcePathText.assign(inOutSourcePathText.data(), inOutSourcePathText.size());

    const Sha1Digest pathSha1 = ComputeSha1Text(AStringView(inOutSourcePathText));
    inOutCacheFileName.clear();
    inOutCacheFileName.reserve(Sha1Digest::s_HexTextLength + s_BuiltAssetExtension.size());
    AppendSha1Hex(pathSha1, inOutCacheFileName);
    inOutCacheFileName += s_BuiltAssetExtension;

    const Path combinedCachePath = assetCacheDirectory / inOutCacheFileName;
    const auto absoluteCachePath = AbsolutePath(combinedCachePath);
    outRecord.cacheFilePath = (absoluteCachePath ? *absoluteCachePath : combinedCachePath).lexicallyNormal();

    b8 canUseCache = false;
    outRecord.paramSha256 = __hidden_cook_metadata_cache::ComputeNwbParamChecksum(
        discoveredNwbFile,
        AStringView(inOutSourcePathText),
        metaText,
        producerDigest,
        scratchArena,
        canUseCache
    );
    outRecord.canWriteCache = canonicalPathOk && canUseCache;
}

NwbCacheLoadResult::Enum TryLoadNwbFileAssetCache(
    NwbFileAssetCacheRecord& inOutRecord,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    const ParsedAssetMetadata& metadata
){
    inOutRecord.cachedVirtualPaths.clear();

    Vector<u8, ScratchArena> cacheBytes(scratchArena);
    if(!ReadBinaryFile(inOutRecord.cacheFilePath, cacheBytes))
        return NwbCacheLoadResult::Miss;

    DecodedBuiltAssetFileView decodedFile;
    Vector<DecodedBuiltAssetEntryView, ScratchArena> decodedEntries(scratchArena);
    if(!BuiltAssetFormatDetail::DecodeBuiltAssetBytes(cacheBytes, decodedFile, decodedEntries))
        return NwbCacheLoadResult::Miss;

    if((decodedFile.header.flags & s_BuiltAssetFlagStandalone) == 0u)
        return NwbCacheLoadResult::Miss;
    if(decodedFile.header.paramSha256 != inOutRecord.paramSha256)
        return NwbCacheLoadResult::Miss;
    if(decodedFile.sourcePathText != AStringView(inOutRecord.sourcePathText))
        return NwbCacheLoadResult::Miss;

    for(const DecodedBuiltAssetEntryView& entry : decodedEntries){
        if(!entry.assetType || IsNonStandaloneAssetType(entry.assetType) || !metadata.entryRegistry.has(entry.assetType))
            return NwbCacheLoadResult::Miss;
    }

    Vector<NameHash, ScratchArena> insertedHashes(scratchArena);
    insertedHashes.reserve(decodedEntries.size());
    ScopeExit rollbackSeenPaths([&]()noexcept{
        for(const NameHash& insertedHash : insertedHashes)
            seenPropertyAssetPathHashes.erase(insertedHash);
    });

    for(const DecodedBuiltAssetEntryView& entry : decodedEntries){
        const NameHash pathHash = entry.virtualPath.hash();
        if(!seenPropertyAssetPathHashes.insert(pathHash).second){
            if(FindIf(insertedHashes.begin(), insertedHashes.end(), [&pathHash](const NameHash& existing)noexcept{ return existing == pathHash; }) != insertedHashes.end())
                return NwbCacheLoadResult::Miss;

            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: duplicate property asset virtual path '{}'"), ConvertString(entry.virtualPath.resolvedText()));
            return NwbCacheLoadResult::Error;
        }
        insertedHashes.push_back(pathHash);
    }

    inOutRecord.cachedVirtualPaths.reserve(decodedEntries.size());
    for(const DecodedBuiltAssetEntryView& entry : decodedEntries)
        inOutRecord.cachedVirtualPaths.push_back(entry.virtualPath);
    rollbackSeenPaths.release();
    return NwbCacheLoadResult::Hit;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

