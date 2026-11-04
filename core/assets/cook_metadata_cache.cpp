// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_internal.h"

#include <global/binary.h>
#include <global/sha1.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_metadata_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DecodedCachedAssetEntry{
    u32 nwbSubIndex = 0u;
    Name assetType = s_NameNone;
    Name virtualPath = s_NameNone;
    Span<const u8> payloadBytes;
};


static void BuildNwbRelativePathText(const Path& repoRoot, const DiscoveredNwbFile& discoveredNwbFile, ScratchArena& scratchArena, ScratchString& outRelativePathText){
    const ::Path<ScratchArena> sourcePath(scratchArena, discoveredNwbFile.filePath.lexicallyNormal());
    if(!repoRoot.empty()){
        const ::Path<ScratchArena> rootPath(scratchArena, repoRoot.lexicallyNormal());
        const ::Path<ScratchArena> relativePath = sourcePath.lexicallyRelative(rootPath);
        if(AssetPathsDetail::BuildRelativeAssetPathText(relativePath, outRelativePathText))
            return;
    }

    const ::Path<ScratchArena> assetRootPath(scratchArena, discoveredNwbFile.assetRoot.lexicallyNormal());
    const ::Path<ScratchArena> assetRelativePath = sourcePath.lexicallyRelative(assetRootPath);
    if(AssetPathsDetail::BuildRelativeAssetPathText(assetRelativePath, outRelativePathText))
        return;

    outRelativePathText = discoveredNwbFile.normalizedPathText;
    for(char& ch : outRelativePathText){
        if(ch == '\\')
            ch = '/';
        else
            ch = Canonicalize(ch);
    }
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
    const AStringView relativePathText,
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
    shaInput.reserve(sizeof(u16) + relativePathText.size() + discoveredNwbFile.virtualRoot.view().size() + derivedVirtualPathView.size() + sizeof(Sha256Digest) * 3u + 16u);
    AppendPOD(shaInput, s_NwbAssetCacheVersion);
    AppendPOD(shaInput, producerDigest);
    if(
        !AppendString(shaInput, relativePathText)
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
    const Path& repoRoot,
    const Path& assetCacheDirectory,
    const DiscoveredNwbFile& discoveredNwbFile,
    const AStringView metaText,
    const Sha256Digest& producerDigest,
    ScratchArena& scratchArena,
    ScratchString& inOutRelativePathText,
    ScratchString& inOutCacheFileName,
    NwbFileAssetCacheRecord& outRecord
){
    __hidden_cook_metadata_cache::BuildNwbRelativePathText(repoRoot, discoveredNwbFile, scratchArena, inOutRelativePathText);
    outRecord.relativePathText.assign(inOutRelativePathText.data(), inOutRelativePathText.size());

    const Sha1Digest pathSha1 = ComputeSha1Text(AStringView(inOutRelativePathText));
    inOutCacheFileName.clear();
    inOutCacheFileName.reserve(Sha1Digest::s_HexTextLength);
    AppendSha1Hex(pathSha1, inOutCacheFileName);
    outRecord.cacheFilePath = assetCacheDirectory / inOutCacheFileName;

    b8 canUseCache = false;
    outRecord.paramSha256 = __hidden_cook_metadata_cache::ComputeNwbParamChecksum(
        discoveredNwbFile,
        AStringView(inOutRelativePathText),
        metaText,
        producerDigest,
        scratchArena,
        canUseCache
    );
    outRecord.canWriteCache = canUseCache;
}

NwbCacheLoadResult::Enum TryLoadNwbFileAssetCache(
    const NwbFileAssetCacheRecord& record,
    const usize nwbFileIndex,
    CookArena& cookArena,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    ParsedAssetMetadata& outMetadata
){
    Vector<u8, ScratchArena> cacheBytes(scratchArena);
    if(!ReadBinaryFile(record.cacheFilePath, cacheBytes))
        return NwbCacheLoadResult::Miss;

    usize cursor = 0u;
    const auto header = ReadPOD<NwbAssetCacheFileHeader>(cacheBytes, cursor);
    if(!header)
        return NwbCacheLoadResult::Miss;
    if(header->magic != s_NwbAssetCacheMagic || header->version != s_NwbAssetCacheVersion || header->headerSize != sizeof(NwbAssetCacheFileHeader))
        return NwbCacheLoadResult::Miss;
    if((header->flags & s_NwbAssetCacheFlagStandalone) == 0u || header->entryCount == 0u)
        return NwbCacheLoadResult::Miss;
    if(header->paramSha256 != record.paramSha256)
        return NwbCacheLoadResult::Miss;
    if(header->payloadSectionSize > static_cast<u64>(Limit<usize>::s_Max))
        return NwbCacheLoadResult::Miss;

    const usize payloadSectionSize = static_cast<usize>(header->payloadSectionSize);
    if(cursor > cacheBytes.size() || cacheBytes.size() - cursor != payloadSectionSize)
        return NwbCacheLoadResult::Miss;
    if(ComputeFnv64Bytes(cacheBytes.data() + cursor, payloadSectionSize) != header->payloadSectionHashFnv64)
        return NwbCacheLoadResult::Miss;

    const auto cachedRelativePath = ReadString(cacheBytes, cursor);
    if(!cachedRelativePath || *cachedRelativePath != AStringView(record.relativePathText))
        return NwbCacheLoadResult::Miss;

    constexpr usize minimumEntryBytes = sizeof(u32) * 2u + sizeof(NameHash) * 2u + sizeof(u64) * 2u;
    if(header->entryCount > (cacheBytes.size() - cursor) / minimumEntryBytes)
        return NwbCacheLoadResult::Miss;

    Vector<__hidden_cook_metadata_cache::DecodedCachedAssetEntry, ScratchArena> decodedEntries(scratchArena);
    decodedEntries.reserve(header->entryCount);
    for(u32 entryIndex = 0u; entryIndex < header->entryCount; ++entryIndex){
        const auto nwbSubIndex = ReadPOD<u32>(cacheBytes, cursor);
        const auto assetTypeHash = ReadPOD<NameHash>(cacheBytes, cursor);
        const auto virtualPathHash = ReadPOD<NameHash>(cacheBytes, cursor);
        const auto virtualPathText = ReadString(cacheBytes, cursor);
        const auto payloadSize = ReadPOD<u64>(cacheBytes, cursor);
        const auto payloadHashFnv64 = ReadPOD<u64>(cacheBytes, cursor);
        if(!nwbSubIndex || !assetTypeHash || !virtualPathHash || !virtualPathText || !payloadSize || !payloadHashFnv64)
            return NwbCacheLoadResult::Miss;
        if(*payloadSize > static_cast<u64>(Limit<usize>::s_Max))
            return NwbCacheLoadResult::Miss;

        const usize payloadByteCount = static_cast<usize>(*payloadSize);
        if(!CanReadBytes(cacheBytes, cursor, payloadByteCount))
            return NwbCacheLoadResult::Miss;
        const u8* const payloadData = cacheBytes.data() + cursor;
        cursor += payloadByteCount;
        if(ComputeFnv64Bytes(payloadData, payloadByteCount) != *payloadHashFnv64)
            return NwbCacheLoadResult::Miss;

        const Name assetType(*assetTypeHash);
        if(!assetType || IsNonStandaloneAssetType(assetType) || !outMetadata.entryRegistry.has(assetType))
            return NwbCacheLoadResult::Miss;

        Name virtualPath(*virtualPathHash);
        if(!virtualPathText->empty()){
            const auto parsedVirtualPath = Name::FromText(*virtualPathText);
            if(parsedVirtualPath && parsedVirtualPath->identityHash() == *virtualPathHash)
                virtualPath = *parsedVirtualPath;
        }
        if(!virtualPath)
            return NwbCacheLoadResult::Miss;

        decodedEntries.push_back(__hidden_cook_metadata_cache::DecodedCachedAssetEntry{
            .nwbSubIndex = *nwbSubIndex,
            .assetType = assetType,
            .virtualPath = virtualPath,
            .payloadBytes = Span<const u8>(payloadData, payloadByteCount),
        });
    }
    if(cursor != cacheBytes.size())
        return NwbCacheLoadResult::Miss;

    for(const __hidden_cook_metadata_cache::DecodedCachedAssetEntry& decoded : decodedEntries){
        CookEntryParseContext parseContext{ cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, nwbFileIndex, decoded.nwbSubIndex };
        if(!outMetadata.entryRegistry.appendCachedEntry(decoded.assetType, CookEntrySourceLocation{ nwbFileIndex, decoded.nwbSubIndex }, decoded.virtualPath, decoded.payloadBytes, parseContext))
            return NwbCacheLoadResult::Error;
    }
    return NwbCacheLoadResult::Hit;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

