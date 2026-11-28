// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_internal.h"
#include "asset_producer_fingerprint.h"

#include <core/common/log.h>

#include <global/filesystem/operations.h>
#include <global/scope_exit.h>
#include <global/source_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void LogMetascriptParseErrors(const Path& nwbFilePath, const Core::Metascript::Document& doc){
    for(const Core::Metascript::ParseError& err : doc.errors()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: meta '{}' parse error at {}:{}: {}"), PathToString<tchar>(nwbFilePath), err.line, err.column, ConvertString(AStringView(err.message)));
    }
}

[[nodiscard]] static Expected<Core::Metascript::Document> ParseMetascriptDocumentText(const Path& nwbFilePath, const AStringView metaText, CookArena& cookArena){
    Core::Metascript::Document doc(cookArena);
    if(!doc.parse(metaText)){
        LogMetascriptParseErrors(nwbFilePath, doc);
        return MakeUnexpected(Failure{});
    }
    return doc;
}

[[nodiscard]] static b8 ParseDeclaredAssetItem(
    const DiscoveredNwbFile& discoveredNwbFile,
    const usize nwbFileIndex,
    const u32 nwbSubIndex,
    const Name assetType,
    const Name virtualPath,
    const Core::Metascript::Value& asset,
    const CookMetadataDetail::CookMetadataRegistrySnapshot& registrySnapshot,
    CookArena& cookArena,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    ParsedAssetMetadata& outMetadata,
    b8& inOutStandalone
){
    AssetValueMetadataParseContext metadataParseContext{ cookArena, discoveredNwbFile, assetType, virtualPath, asset, outMetadata, scratchArena };
    const AssetMetadataParseResult::Enum metadataParseResult = CookMetadataDetail::TryValueMetadataParsers(registrySnapshot, metadataParseContext);
    if(metadataParseResult == AssetMetadataParseResult::Parsed){
        inOutStandalone = false;
        return true;
    }
    if(metadataParseResult == AssetMetadataParseResult::Error)
        return false;

    if(CookMetadataDetail::IsNonStandaloneAssetType(registrySnapshot, assetType))
        inOutStandalone = false;

    CookEntryParseContext parseContext{ cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, nwbFileIndex, nwbSubIndex };
    return outMetadata.entryRegistry.parseValue(assetType, virtualPath, discoveredNwbFile.filePath, asset, parseContext);
}

[[nodiscard]] static b8 ParseSingleAssetDocument(
    const DiscoveredNwbFile& discoveredNwbFile,
    const usize nwbFileIndex,
    const Core::Metascript::Document& doc,
    const CookMetadataDetail::CookMetadataRegistrySnapshot& registrySnapshot,
    CookArena& cookArena,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    ParsedAssetMetadata& outMetadata,
    b8& inOutStandalone
){
    const auto* assetDeclaration = doc.findDeclaration(s_MetadataAssetVariable);
    if(!assetDeclaration){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: meta '{}' is missing asset declaration '{}'"), PathToString<tchar>(discoveredNwbFile.filePath), ConvertString(s_MetadataAssetVariable));
        return false;
    }
    const AStringView rawAssetTypeText(assetDeclaration->type);
    const auto assetTypeResult = ToName(rawAssetTypeText);
    if(!assetTypeResult){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: asset type contains an embedded null in meta '{}'"), PathToString<tchar>(discoveredNwbFile.filePath));
        return false;
    }
    const Name assetType = *assetTypeResult ? **assetTypeResult : s_NameNone;
    AssetDocumentMetadataParseContext metadataParseContext{ cookArena, discoveredNwbFile, assetType, doc, outMetadata, scratchArena };
    const AssetMetadataParseResult::Enum metadataParseResult = CookMetadataDetail::TryDocumentMetadataParsers(registrySnapshot, metadataParseContext);
    if(metadataParseResult == AssetMetadataParseResult::Parsed){
        inOutStandalone = false;
        return true;
    }
    if(metadataParseResult == AssetMetadataParseResult::Error)
        return false;

    if(outMetadata.entryRegistry.has(assetType)){
        if(CookMetadataDetail::IsNonStandaloneAssetType(registrySnapshot, assetType))
            inOutStandalone = false;

        CookEntryParseContext parseContext{ cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, nwbFileIndex, 0u };
        return outMetadata.entryRegistry.parseDocument(assetType, discoveredNwbFile.assetRoot, discoveredNwbFile.virtualRoot.view(), discoveredNwbFile.filePath, doc, parseContext);
    }

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: unsupported asset type '{}' in meta '{}'"), ConvertString(rawAssetTypeText), PathToString<tchar>(discoveredNwbFile.filePath));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AddPlannedFileCount(const u64 additionalFileCount, u64& inOutPlannedFileCount){
    if(inOutPlannedFileCount > Limit<u64>::s_Max - additionalFileCount){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: planned file count overflow"));
        return false;
    }

    inOutPlannedFileCount += additionalFileCount;
    return true;
}

b8 ParseAssetMetadata(
    const Span<const DiscoveredNwbFile> nwbFiles,
    const Path& assetCacheDirectory,
    CookArena& cookArena,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    ParsedAssetMetadata& outMetadata
){
    CookEntryPathHashSet seenPropertyAssetPathHashes(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);

    seenPropertyAssetPathHashes.reserve(nwbFiles.size());

    b8 useAssetCache = !assetCacheDirectory.empty();
    Sha256Digest producerDigest = {};
    Vector<AssetPairedSidecarRule, ScratchArena> pairedSidecarRules(scratchArena);
    Vector<NotNull<CookExternalSourceCollectorFunction>, ScratchArena> externalSourceCollectors(scratchArena);
    CookMetadataDetail::CookMetadataRegistrySnapshot registrySnapshot(scratchArena);
    Optional<CookMetadataDetail::CookExternalSourceDigestCache> externalDigestCache;
    if(!nwbFiles.empty())
        CookMetadataDetail::CaptureMetadataRegistrySnapshot(registrySnapshot);
    if(useAssetCache && !nwbFiles.empty()){
        AssetProducerFingerprint::Resolve(scratchArena, useAssetCache, producerDigest);
        if(useAssetCache){
            CookMetadataDetail::CopyPairedSidecarRules(pairedSidecarRules);
            CookMetadataDetail::CopyCookExternalSourceCollectors(externalSourceCollectors);
            externalDigestCache.emplace();
        }
    }
    const Span<const AssetPairedSidecarRule> pairedSidecarRuleSpan(pairedSidecarRules.data(), pairedSidecarRules.size());
    const Span<const NotNull<CookExternalSourceCollectorFunction>> externalSourceCollectorSpan(externalSourceCollectors.data(), externalSourceCollectors.size());
    CookMetadataDetail::CookExternalSourceDigestCache* const externalDigestCachePtr = externalDigestCache ? &*externalDigestCache : nullptr;

    outMetadata.fileCacheRecords.clear();
    if(useAssetCache){
        outMetadata.fileCacheRecords.reserve(nwbFiles.size());
        for(usize fileIndex = 0u; fileIndex < nwbFiles.size(); ++fileIndex)
            outMetadata.fileCacheRecords.emplace_back(cookArena);
    }

    struct PreparedParsedDocument final : NoCopy{
        Core::Metascript::MetaArena arena;
        Core::Metascript::Document doc;
        b8 ok = false;

        PreparedParsedDocument()
            : arena(Name("cook_meta/prep_doc"))
            , doc(arena)
        {}
    };

    struct PreparedCookMetaFile final{
        AInteropString metaText;
        AInteropString sourcePathText;
        AInteropString cacheFileName;
        UniquePtr<PreparedParsedDocument> parsedDocument;
        Sha256Digest paramSha256 = {};
        b8 readOk = false;
        b8 canUseCache = false;
    };

    Vector<PreparedCookMetaFile, ScratchArena> preparedFiles(scratchArena);
    b8 useParallelPrep = nwbFiles.size() >= 4u && cpuScheduler.isParallelEnabled();
    if(useParallelPrep){
        preparedFiles.resize(nwbFiles.size());
        const auto prepResult = cpuScheduler.parallelFor(usize{ 0u }, nwbFiles.size(), [&](const usize fileIndex){
            Alloc::ScratchArena workerScratch;
            ScratchString sourcePathText{workerScratch};
            ScratchString cacheFileName{workerScratch};
            PreparedCookMetaFile& entry = preparedFiles[fileIndex];
            const DiscoveredNwbFile& discoveredNwbFile = nwbFiles[fileIndex];
            if(!ReadTextFile(discoveredNwbFile.filePath, entry.metaText))
                return;
            entry.readOk = true;
            StripUtf8Bom(entry.metaText);
            if(useAssetCache){
                struct PreparedParseThunk final{
                    PreparedCookMetaFile* entry = nullptr;
                };
                PreparedParseThunk parseThunk{ &entry };
                const auto ensurePreparedDoc = [](void* const user) -> const Core::Metascript::Document*{
                    auto* const thunk = static_cast<PreparedParseThunk*>(user);
                    if(!thunk->entry->parsedDocument){
                        thunk->entry->parsedDocument = MakeUnique<PreparedParsedDocument>();
                        thunk->entry->parsedDocument->ok = thunk->entry->parsedDocument->doc.parse(AStringView(thunk->entry->metaText));
                    }
                    return thunk->entry->parsedDocument->ok ? &thunk->entry->parsedDocument->doc : nullptr;
                };
                entry.canUseCache = CookMetadataDetail::ComputeNwbFileAssetCacheData(
                    discoveredNwbFile,
                    AStringView(entry.metaText),
                    producerDigest,
                    pairedSidecarRuleSpan,
                    externalSourceCollectorSpan,
                    workerScratch,
                    sourcePathText,
                    cacheFileName,
                    entry.paramSha256,
                    externalDigestCachePtr,
                    ensurePreparedDoc,
                    &parseThunk
                );
                entry.sourcePathText.assign(sourcePathText.data(), sourcePathText.size());
                entry.cacheFileName.assign(cacheFileName.data(), cacheFileName.size());
            }
        });
        if(!prepResult){
            preparedFiles.clear();
            useParallelPrep = false;
        }
    }

    ScratchArena fileScratchArena;

    b8 parsedAnyMetadata = false;
    for(usize fileIndex = 0u; fileIndex < nwbFiles.size(); ++fileIndex){
        Alloc::ScratchArena::Scope fileScratchScope(fileScratchArena);
        ScopeExit releasePreparedDoc([&]()noexcept{
            if(useParallelPrep)
                preparedFiles[fileIndex].parsedDocument.reset();
        });
        ScratchString metaText{fileScratchArena};
        ScratchString sourcePathText{fileScratchArena};
        ScratchString cacheFileName{fileScratchArena};

        const DiscoveredNwbFile& discoveredNwbFile = nwbFiles[fileIndex];
        AStringView metaTextView;
        if(useParallelPrep){
            const PreparedCookMetaFile& prepared = preparedFiles[fileIndex];
            if(!prepared.readOk){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: failed to read meta '{}'"), PathToString<tchar>(discoveredNwbFile.filePath));
                return false;
            }
            metaTextView = AStringView(prepared.metaText);
        }
        else{
            metaText.clear();
            if(!ReadTextFile(discoveredNwbFile.filePath, metaText)){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: failed to read meta '{}'"), PathToString<tchar>(discoveredNwbFile.filePath));
                return false;
            }
            StripUtf8Bom(metaText);
            metaTextView = AStringView(metaText);
        }

        Optional<Core::Metascript::Document> serialParsedDoc;
        b8 serialParsedDocAttempted = false;
        b8 serialParsedDocOk = false;

        if(useAssetCache){
            NwbFileAssetCacheRecord& cacheRecord = outMetadata.fileCacheRecords[fileIndex];
            const AStringView firstToken = ReadFirstIdentifierAfterCComments(metaTextView);
            if(useParallelPrep){
                const PreparedCookMetaFile& prepared = preparedFiles[fileIndex];
                CookMetadataDetail::PopulateNwbFileAssetCacheRecord(
                    assetCacheDirectory,
                    AStringView(prepared.sourcePathText),
                    AStringView(prepared.cacheFileName),
                    prepared.paramSha256,
                    prepared.canUseCache,
                    cacheRecord
                );
            }
            else{
                struct SerialParseThunk final{
                    AStringView metaText;
                    CookArena* cookArena = nullptr;
                    Optional<Core::Metascript::Document>* doc = nullptr;
                    b8* attempted = nullptr;
                    b8* ok = nullptr;
                };
                SerialParseThunk serialThunk{ metaTextView, &cookArena, &serialParsedDoc, &serialParsedDocAttempted, &serialParsedDocOk };
                const auto ensureSerialDoc = [](void* const user) -> const Core::Metascript::Document*{
                    auto* const thunk = static_cast<SerialParseThunk*>(user);
                    if(!*thunk->attempted){
                        *thunk->attempted = true;
                        thunk->doc->emplace(*thunk->cookArena);
                        *thunk->ok = (*thunk->doc)->parse(thunk->metaText);
                    }
                    return *thunk->ok ? &**thunk->doc : nullptr;
                };
                CookMetadataDetail::PrepareNwbFileAssetCacheRecord(
                    assetCacheDirectory,
                    discoveredNwbFile,
                    metaTextView,
                    producerDigest,
                    pairedSidecarRuleSpan,
                    externalSourceCollectorSpan,
                    fileScratchArena,
                    sourcePathText,
                    cacheFileName,
                    cacheRecord,
                    externalDigestCachePtr,
                    ensureSerialDoc,
                    &serialThunk
                );
            }

#if !defined(NWB_BUILDMODE)
            if(cacheRecord.canWriteCache && !CookMetadataDetail::IsLikelyNonStandaloneFirstToken(registrySnapshot, firstToken)){
                const auto cacheResult = CookMetadataDetail::TryLoadNwbFileAssetCache(
                    cacheRecord,
                    fileScratchArena,
                    seenPropertyAssetPathHashes,
                    outMetadata,
                    &registrySnapshot
                );
                if(cacheResult == CookMetadataDetail::NwbCacheLoadResult::Error)
                    return false;
                if(cacheResult == CookMetadataDetail::NwbCacheLoadResult::Hit){
                    cacheRecord.standalone = true;
                    cacheRecord.loadedFromCache = true;
                    parsedAnyMetadata = true;
                    NWB_LOGGER_INFO(FOUNDATION_TEXT("AssetCook: '{}' is already cached at '{}'"), PathToString<tchar>(discoveredNwbFile.filePath), PathToString<tchar>(cacheRecord.cacheFilePath));
                    continue;
                }
            }
#else
            (void)firstToken;
#endif
        }

        Optional<Core::Metascript::Document> localParsedDoc;
        const Core::Metascript::Document* docPtr = nullptr;
        if(useParallelPrep && preparedFiles[fileIndex].parsedDocument){
            const PreparedParsedDocument& prepDoc = *preparedFiles[fileIndex].parsedDocument;
            if(!prepDoc.ok){
                __hidden_cook_metadata::LogMetascriptParseErrors(discoveredNwbFile.filePath, prepDoc.doc);
                return false;
            }
            docPtr = &prepDoc.doc;
        }
        else if(serialParsedDocAttempted){
            if(!serialParsedDocOk){
                __hidden_cook_metadata::LogMetascriptParseErrors(discoveredNwbFile.filePath, *serialParsedDoc);
                return false;
            }
            docPtr = &*serialParsedDoc;
        }
        else{
            auto parsedDoc = __hidden_cook_metadata::ParseMetascriptDocumentText(discoveredNwbFile.filePath, metaTextView, cookArena);
            if(!parsedDoc)
                return false;
            localParsedDoc.emplace(Move(*parsedDoc));
            docPtr = &*localParsedDoc;
        }
        const auto& doc = *docPtr;

        b8 fileIsStandalone = true;
        AssetBunchExpandContext assetBunchExpandContext{ discoveredNwbFile.assetRoot, discoveredNwbFile.virtualRoot.view(), discoveredNwbFile.filePath, doc, fileScratchArena };
        auto expandedAssets = CookMetadataDetail::TryAssetBunchExpanders(registrySnapshot, assetBunchExpandContext);
        if(!expandedAssets && expandedAssets.error() == AssetBunchExpandFailure::Error)
            return false;
        if(expandedAssets){
            if(expandedAssets->size() > static_cast<usize>(Limit<u32>::s_Max))
                return false;
            for(usize itemIndex = 0u; itemIndex < expandedAssets->size(); ++itemIndex){
                const ExpandedAssetMetadata& expandedAsset = (*expandedAssets)[itemIndex];
                const b8 parsedItem = __hidden_cook_metadata::ParseDeclaredAssetItem(
                    discoveredNwbFile,
                    fileIndex,
                    static_cast<u32>(itemIndex),
                    expandedAsset.assetType,
                    expandedAsset.virtualPath,
                    expandedAsset.value,
                    registrySnapshot,
                    cookArena,
                    cpuScheduler,
                    fileScratchArena,
                    seenPropertyAssetPathHashes,
                    outMetadata,
                    fileIsStandalone
                );
                if(!parsedItem)
                    return false;
                parsedAnyMetadata = true;
            }
            if(useAssetCache)
                outMetadata.fileCacheRecords[fileIndex].standalone = fileIsStandalone;
            continue;
        }

        if(doc.declarations().size() > 1u){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: meta '{}' declares multiple asset objects without an asset_bunch object"), PathToString<tchar>(discoveredNwbFile.filePath));
            return false;
        }

        if(!__hidden_cook_metadata::ParseSingleAssetDocument(discoveredNwbFile, fileIndex, doc, registrySnapshot, cookArena, cpuScheduler, fileScratchArena, seenPropertyAssetPathHashes, outMetadata, fileIsStandalone))
            return false;
        if(useAssetCache)
            outMetadata.fileCacheRecords[fileIndex].standalone = fileIsStandalone;
        parsedAnyMetadata = true;
    }

    if(!parsedAnyMetadata){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: no asset metadata found in asset roots"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

