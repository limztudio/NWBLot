// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_internal.h"
#include "built_asset_format.h"

#include <core/common/log.h>
#include <core/metascript/parser.h>

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


template<typename PathArenaT>
[[nodiscard]] static b8 BuildCanonicalAbsolutePathText(const ::Path<PathArenaT>& filePath, ScratchArena& scratchArena, ScratchString& outPathText){
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

[[nodiscard]] static b8 AppendPairedSidecarChecksums(
    const Path& nwbFilePath,
    const Span<const AssetPairedSidecarRule> pairedSidecarRules,
    ScratchArena& scratchArena,
    Vector<u8, ScratchArena>& shaInput
){
    for(const AssetPairedSidecarRule& rule : pairedSidecarRules){
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
        Sha256Digest digest{};
        {
            Alloc::ScratchArena::Scope sidecarScope(scratchArena);
            Vector<u8, ScratchArena> sidecarBytes(scratchArena);
            if(!ReadBinaryFile(sidecarPath, sidecarBytes))
                return false;
            digest = ComputeSha256(BinaryByteView{ sidecarBytes.data(), sidecarBytes.size() });
        }
        AppendPOD(shaInput, digest);
    }
    return true;
}

[[nodiscard]] static b8 AppendExternalSourceChecksums(
    const DiscoveredNwbFile& discoveredNwbFile,
    const AStringView metaText,
    const Span<const NotNull<CookExternalSourceCollectorFunction>> externalSourceCollectors,
    CookMetadataDetail::CookExternalSourceDigestCache* const externalDigestCache,
    const CookMetadataDetail::EnsureCookParsedDocumentFunction ensureParsedDocument,
    void* const ensureParsedDocumentUser,
    ScratchArena& scratchArena,
    Vector<u8, ScratchArena>& shaInput
){
    if(externalSourceCollectors.empty())
        return true;

    Optional<Core::Metascript::MetaArena> parseArena;
    Optional<Core::Metascript::Document> parsedDoc;
    b8 attemptedParse = false;
    b8 parsedOk = false;
    struct LocalParseThunk final{
        AStringView metaText;
        Optional<Core::Metascript::MetaArena>* arena = nullptr;
        Optional<Core::Metascript::Document>* doc = nullptr;
        b8* attempted = nullptr;
        b8* ok = nullptr;
    };
    LocalParseThunk parseThunk{ metaText, &parseArena, &parsedDoc, &attemptedParse, &parsedOk };
    const auto localEnsureDoc = [](void* const user) -> const Core::Metascript::Document*{
        auto* const thunk = static_cast<LocalParseThunk*>(user);
        if(!*thunk->attempted){
            *thunk->attempted = true;
            thunk->arena->emplace(Name("cook_meta/ext_parse"));
            thunk->doc->emplace(**thunk->arena);
            *thunk->ok = (*thunk->doc)->parse(thunk->metaText);
        }
        return *thunk->ok ? &**thunk->doc : nullptr;
    };
    const CookMetadataDetail::EnsureCookParsedDocumentFunction effectiveEnsureDoc = (ensureParsedDocument != nullptr) ? ensureParsedDocument : localEnsureDoc;
    void* const effectiveEnsureUser = (ensureParsedDocument != nullptr) ? ensureParsedDocumentUser : &parseThunk;

    const CookExternalSourceCollectContext collectContext{
        discoveredNwbFile,
        metaText,
        scratchArena,
        effectiveEnsureDoc,
        effectiveEnsureUser
    };
    Vector<::Path<ScratchArena>, ScratchArena> rawPaths(scratchArena);
    for(const NotNull<CookExternalSourceCollectorFunction> collector : externalSourceCollectors){
        b8 collectorCanUseCache = true;
        if(!(*collector)(collectContext, rawPaths, collectorCanUseCache) || !collectorCanUseCache)
            return false;
    }
    if(rawPaths.empty())
        return true;

    struct ExternalSourceEntry final{
        ScratchString canonicalText;
        ::Path<ScratchArena> path;
        explicit ExternalSourceEntry(ScratchArena& arena)noexcept
            : canonicalText(arena)
            , path(arena)
        {}
    };

    Vector<ExternalSourceEntry, ScratchArena> entries(scratchArena);
    entries.reserve(rawPaths.size());
    for(const ::Path<ScratchArena>& rawPath : rawPaths){
        ExternalSourceEntry entry(scratchArena);
        if(!BuildCanonicalAbsolutePathText(rawPath, scratchArena, entry.canonicalText))
            return false;
        entry.path = rawPath;
        entries.push_back(Move(entry));
    }

    Sort(entries.begin(), entries.end(), [](const ExternalSourceEntry& lhs, const ExternalSourceEntry& rhs)noexcept{
        return AStringView(lhs.canonicalText) < AStringView(rhs.canonicalText);
    });
    usize uniqueCount = 0u;
    for(usize i = 0u; i < entries.size(); ++i){
        if(uniqueCount == 0u || AStringView(entries[i].canonicalText) != AStringView(entries[uniqueCount - 1u].canonicalText)){
            if(uniqueCount != i)
                entries[uniqueCount] = Move(entries[i]);
            ++uniqueCount;
        }
    }
    while(entries.size() > uniqueCount)
        entries.pop_back();

    if(!AppendString(shaInput, AStringView("EXTSRC1")))
        return false;
    const u64 sourceCount = static_cast<u64>(entries.size());
    AppendPOD(shaInput, sourceCount);
    for(const ExternalSourceEntry& entry : entries){
        const AStringView canonicalView(entry.canonicalText);
        if(!AppendString(shaInput, canonicalView))
            return false;

        CookMetadataDetail::CookExternalSourceDigestCache::Entry cachedEntry;
        if(externalDigestCache != nullptr && externalDigestCache->find(canonicalView, cachedEntry)){
            if(!cachedEntry.ok)
                return false;
            const u8 present = cachedEntry.exists ? 1u : 0u;
            AppendPOD(shaInput, present);
            if(cachedEntry.exists)
                AppendPOD(shaInput, cachedEntry.digest);
            continue;
        }

        const auto exists = FileExists(entry.path);
        if(!exists)
            return false;
        const u8 present = *exists ? 1u : 0u;
        AppendPOD(shaInput, present);
        if(!*exists){
            if(externalDigestCache != nullptr)
                externalDigestCache->insert(canonicalView, CookMetadataDetail::CookExternalSourceDigestCache::Entry{ {}, true, false });
            continue;
        }
        Sha256Digest digest{};
        {
            Alloc::ScratchArena::Scope extScope(scratchArena);
            Vector<u8, ScratchArena> extBytes(scratchArena);
            if(!ReadBinaryFile(entry.path, extBytes))
                return false;
            digest = ComputeSha256(BinaryByteView{ extBytes.data(), extBytes.size() });
        }
        if(externalDigestCache != nullptr)
            externalDigestCache->insert(canonicalView, CookMetadataDetail::CookExternalSourceDigestCache::Entry{ digest, true, true });
        AppendPOD(shaInput, digest);
    }
    return true;
}

[[nodiscard]] static Sha256Digest ComputeNwbParamChecksum(
    const DiscoveredNwbFile& discoveredNwbFile,
    const AStringView sourcePathText,
    const AStringView metaText,
    const Sha256Digest& producerDigest,
    const Span<const AssetPairedSidecarRule> pairedSidecarRules,
    const Span<const NotNull<CookExternalSourceCollectorFunction>> externalSourceCollectors,
    CookMetadataDetail::CookExternalSourceDigestCache* const externalDigestCache,
    const CookMetadataDetail::EnsureCookParsedDocumentFunction ensureParsedDocument,
    void* const ensureParsedDocumentUser,
    ScratchArena& scratchArena,
    b8& outCanUseCache
){
    Alloc::ScratchArena::Scope checksumScope(scratchArena);
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

    const Sha256Digest metaSha256 = ComputeSha256(BinaryByteView{ reinterpret_cast<const u8*>(metaText.data()), metaText.size() });

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
    if(!AppendPairedSidecarChecksums(discoveredNwbFile.filePath, pairedSidecarRules, scratchArena, shaInput))
        outCanUseCache = false;
    if(!AppendExternalSourceChecksums(discoveredNwbFile, metaText, externalSourceCollectors, externalDigestCache, ensureParsedDocument, ensureParsedDocumentUser, scratchArena, shaInput))
        outCanUseCache = false;

    return ComputeSha256(BinaryByteView{ shaInput.data(), shaInput.size() });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookMetadataDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CookExternalSourceDigestCache::find(const AStringView canonicalPathText, Entry& outEntry)const noexcept{
    NothrowScopedLock lock(m_mutex);
    const auto found = m_entries.find(canonicalPathText);
    if(found == m_entries.end())
        return false;
    outEntry = found.value();
    return true;
}

void CookExternalSourceDigestCache::insert(const AStringView canonicalPathText, const Entry& entry){
    ScopedLock lock(m_mutex);
    if(m_entries.find(canonicalPathText) != m_entries.end())
        return;
    ScratchString key(canonicalPathText.data(), canonicalPathText.size(), m_arena);
    m_entries.try_emplace(Move(key), entry);
}

b8 ComputeNwbFileAssetCacheData(
    const DiscoveredNwbFile& discoveredNwbFile,
    const AStringView metaText,
    const Sha256Digest& producerDigest,
    const CookMetadataRegistrySnapshot& registrySnapshot,
    ScratchArena& scratchArena,
    ScratchString& outSourcePathText,
    ScratchString& outCacheFileName,
    Sha256Digest& outParamSha256,
    CookExternalSourceDigestCache* const externalDigestCache,
    const EnsureCookParsedDocumentFunction ensureParsedDocument,
    void* const ensureParsedDocumentUser
){
    const b8 canonicalPathOk = __hidden_cook_metadata_cache::BuildCanonicalAbsolutePathText(discoveredNwbFile.filePath, scratchArena, outSourcePathText);
    const Sha1Digest pathSha1 = ComputeSha1Text(AStringView(outSourcePathText));
    outCacheFileName.clear();
    outCacheFileName.reserve(Sha1Digest::s_HexTextLength + s_BuiltAssetExtension.size());
    AppendSha1Hex(pathSha1, outCacheFileName);
    outCacheFileName += s_BuiltAssetExtension;

    b8 canUseCache = false;
    outParamSha256 = __hidden_cook_metadata_cache::ComputeNwbParamChecksum(
        discoveredNwbFile,
        AStringView(outSourcePathText),
        metaText,
        producerDigest,
        Span<const AssetPairedSidecarRule>(registrySnapshot.pairedSidecarRules.data(), registrySnapshot.pairedSidecarRules.size()),
        Span<const NotNull<CookExternalSourceCollectorFunction>>(registrySnapshot.externalSourceCollectors.data(), registrySnapshot.externalSourceCollectors.size()),
        externalDigestCache,
        ensureParsedDocument,
        ensureParsedDocumentUser,
        scratchArena,
        canUseCache
    );
    return canonicalPathOk && canUseCache;
}

void PopulateNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    const AStringView sourcePathText,
    const AStringView cacheFileName,
    const Sha256Digest& paramSha256,
    const b8 canUseCache,
    NwbFileAssetCacheRecord& outRecord
){
    outRecord.sourcePathText.assign(sourcePathText.data(), sourcePathText.size());
    const Path combinedCachePath = assetCacheDirectory / cacheFileName;
    const auto absoluteCachePath = AbsolutePath(combinedCachePath);
    outRecord.cacheFilePath = (absoluteCachePath ? *absoluteCachePath : combinedCachePath).lexicallyNormal();
    outRecord.paramSha256 = paramSha256;
    outRecord.canWriteCache = canUseCache;
}

void PrepareNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    const DiscoveredNwbFile& discoveredNwbFile,
    const AStringView metaText,
    const Sha256Digest& producerDigest,
    const CookMetadataRegistrySnapshot& registrySnapshot,
    ScratchArena& scratchArena,
    ScratchString& inOutSourcePathText,
    ScratchString& inOutCacheFileName,
    NwbFileAssetCacheRecord& outRecord,
    CookExternalSourceDigestCache* const externalDigestCache,
    const EnsureCookParsedDocumentFunction ensureParsedDocument,
    void* const ensureParsedDocumentUser
){
    Sha256Digest paramSha256 = {};
    const b8 canUseCache = ComputeNwbFileAssetCacheData(
        discoveredNwbFile,
        metaText,
        producerDigest,
        registrySnapshot,
        scratchArena,
        inOutSourcePathText,
        inOutCacheFileName,
        paramSha256,
        externalDigestCache,
        ensureParsedDocument,
        ensureParsedDocumentUser
    );
    PopulateNwbFileAssetCacheRecord(assetCacheDirectory, AStringView(inOutSourcePathText), AStringView(inOutCacheFileName), paramSha256, canUseCache, outRecord);
}

NwbCacheLoadResult::Enum TryLoadNwbFileAssetCache(
    NwbFileAssetCacheRecord& inOutRecord,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    const ParsedAssetMetadata& metadata,
    const CookMetadataRegistrySnapshot& registrySnapshot
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
        const b8 nonStandalone = IsNonStandaloneAssetType(registrySnapshot, entry.assetType);
        if(!entry.assetType || nonStandalone || !metadata.entryRegistry.has(entry.assetType))
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

