// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Core::Metascript::Document> ParseMetascriptDocumentText(const Path& nwbFilePath, const AStringView metaText, CookArena& cookArena){
    Core::Metascript::Document doc(cookArena);
    if(!doc.parse(metaText)){
        for(const Core::Metascript::ParseError& err : doc.errors()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: meta '{}' parse error at {}:{}: {}"), PathToString<tchar>(nwbFilePath), err.line, err.column, ConvertString(AStringView(err.message.data(), err.message.size())));
        }
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
    CookArena& cookArena,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    ParsedAssetMetadata& outMetadata,
    b8& inOutStandalone
){
    AssetValueMetadataParseContext metadataParseContext{ cookArena, discoveredNwbFile, assetType, virtualPath, asset, outMetadata, scratchArena };
    const AssetMetadataParseResult::Enum metadataParseResult = TryAutoCollectedValueMetadataParsers(metadataParseContext);
    if(metadataParseResult == AssetMetadataParseResult::Parsed){
        inOutStandalone = false;
        return true;
    }
    if(metadataParseResult == AssetMetadataParseResult::Error)
        return false;

    if(IsNonStandaloneAssetType(assetType))
        inOutStandalone = false;

    CookEntryParseContext parseContext{ cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, nwbFileIndex, nwbSubIndex };
    return outMetadata.entryRegistry.parseValue(assetType, virtualPath, discoveredNwbFile.filePath, asset, parseContext);
}

[[nodiscard]] static b8 ParseSingleAssetDocument(
    const DiscoveredNwbFile& discoveredNwbFile,
    const usize nwbFileIndex,
    const Core::Metascript::Document& doc,
    CookArena& cookArena,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    ParsedAssetMetadata& outMetadata,
    b8& inOutStandalone
){
    const auto* assetDeclaration = doc.findDeclaration(s_MetadataAssetVariable);
    if(!assetDeclaration){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: meta '{}' is missing asset declaration '{}'"), PathToString<tchar>(discoveredNwbFile.filePath), ConvertString(s_MetadataAssetVariable));
        return false;
    }
    const AStringView rawAssetTypeText(assetDeclaration->type.data(), assetDeclaration->type.size());
    const auto assetTypeResult = ToName(rawAssetTypeText);
    if(!assetTypeResult){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset type contains an embedded null in meta '{}'"), PathToString<tchar>(discoveredNwbFile.filePath));
        return false;
    }
    const Name assetType = *assetTypeResult ? **assetTypeResult : s_NameNone;
    AssetDocumentMetadataParseContext metadataParseContext{ cookArena, discoveredNwbFile, assetType, doc, outMetadata, scratchArena };
    const AssetMetadataParseResult::Enum metadataParseResult = TryAutoCollectedDocumentMetadataParsers(metadataParseContext);
    if(metadataParseResult == AssetMetadataParseResult::Parsed){
        inOutStandalone = false;
        return true;
    }
    if(metadataParseResult == AssetMetadataParseResult::Error)
        return false;

    if(outMetadata.entryRegistry.has(assetType)){
        if(IsNonStandaloneAssetType(assetType))
            inOutStandalone = false;

        CookEntryParseContext parseContext{ cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, nwbFileIndex, 0u };
        return outMetadata.entryRegistry.parseDocument(assetType, discoveredNwbFile.assetRoot, discoveredNwbFile.virtualRoot.view(), discoveredNwbFile.filePath, doc, parseContext);
    }

    NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: unsupported asset type '{}' in meta '{}'"), ConvertString(rawAssetTypeText), PathToString<tchar>(discoveredNwbFile.filePath));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DiscoveredNwbFileVector> DiscoverFilesWithExtension(const Span<const ResolvedAssetRoot> assetRoots, const AStringView expectedExtension, CookArena& cookArena, ScratchArena& scratchArena){
    DiscoveredNwbFileVector files(cookArena);
    HashSet<u64, ScratchArena, Hasher<u64>, EqualTo<u64>> seenPathHashes(0, Hasher<u64>(), EqualTo<u64>(), scratchArena);

    for(const ResolvedAssetRoot& assetRoot : assetRoots){
        const auto directory = IsDirectory(assetRoot.path);
        if(!directory || !*directory){
            if(!directory){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to query asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(directory.error().message()));
                return MakeUnexpected(Failure{});
            }

            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset root is not a directory: '{}'"), PathToString<tchar>(assetRoot.path));
            return MakeUnexpected(Failure{});
        }

        const ::Path<ScratchArena> scratchRoot(scratchArena, assetRoot.path);
        const auto entries = RecursiveDirectoryIterator<ScratchArena>::Create(scratchRoot);
        if(!entries){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: error scanning asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(entries.error().message()));
            return MakeUnexpected(Failure{});
        }
        files.reserve(AddSize(files.size(), static_cast<usize>(entries->end() - entries->begin())));
        seenPathHashes.reserve(AddSize(seenPathHashes.size(), static_cast<usize>(entries->end() - entries->begin())));
        for(const auto& dirEntry : *entries){
            const auto isRegularFile = dirEntry.isRegularFile();
            if(!isRegularFile){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to inspect '{}' while scanning '{}': {}"), PathToString<tchar>(dirEntry.path()), PathToString<tchar>(assetRoot.path), ConvertString(isRegularFile.error().message()));
                return MakeUnexpected(Failure{});
            }
            if(!*isRegularFile)
                continue;

            const ::Path<ScratchArena>& filePath = dirEntry.path();
            ScratchString extension = PathToString(scratchArena, filePath.extensionView());
            CanonicalizeTextInPlace(extension);
            if(extension != expectedExtension)
                continue;

            ScratchString normalizedPath = PathToString(scratchArena, filePath.lexicallyNormal());
#if defined(NWB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(normalizedPath);
#endif
            if(!seenPathHashes.insert(ComputeFnv64Text(normalizedPath)).second)
                continue;
#if !defined(NWB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(normalizedPath);
#endif

            files.emplace_back(cookArena, assetRoot.path, filePath, normalizedPath, assetRoot.virtualRoot);
        }
    }

    Sort(files.begin(), files.end(), [](const DiscoveredNwbFile& lhs, const DiscoveredNwbFile& rhs)noexcept{
        return lhs.normalizedPathText < rhs.normalizedPathText;
    });

    return files;
}

b8 AddPlannedFileCount(const u64 additionalFileCount, u64& inOutPlannedFileCount){
    if(inOutPlannedFileCount > Limit<u64>::s_Max - additionalFileCount){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: planned file count overflow"));
        return false;
    }

    inOutPlannedFileCount += additionalFileCount;
    return true;
}

b8 ParseAssetMetadata(
    const Span<const DiscoveredNwbFile> nwbFiles,
    const Path& repoRoot,
    const Path& assetCacheDirectory,
    CookArena& cookArena,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    ParsedAssetMetadata& outMetadata
){
    CookEntryPathHashSet seenPropertyAssetPathHashes(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);

    seenPropertyAssetPathHashes.reserve(nwbFiles.size());

    const b8 useAssetCache = !assetCacheDirectory.empty();
    outMetadata.fileCacheRecords.clear();
    if(useAssetCache){
        outMetadata.fileCacheRecords.reserve(nwbFiles.size());
        for(usize fileIndex = 0u; fileIndex < nwbFiles.size(); ++fileIndex)
            outMetadata.fileCacheRecords.emplace_back(cookArena);
    }

    ScratchString metaText{scratchArena};
    ScratchString relativePathText{scratchArena};
    ScratchString cacheFileName{scratchArena};

    b8 parsedAnyMetadata = false;
    for(usize fileIndex = 0u; fileIndex < nwbFiles.size(); ++fileIndex){
        const DiscoveredNwbFile& discoveredNwbFile = nwbFiles[fileIndex];
        metaText.clear();
        if(!ReadTextFile(discoveredNwbFile.filePath, metaText)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to read meta '{}'"), PathToString<tchar>(discoveredNwbFile.filePath));
            return false;
        }
        StripUtf8Bom(metaText);

        if(useAssetCache){
            NwbFileAssetCacheRecord& cacheRecord = outMetadata.fileCacheRecords[fileIndex];
            const AStringView firstToken = CookMetadataDetail::ExtractFirstIdentifierToken(AStringView(metaText));
            CookMetadataDetail::PrepareNwbFileAssetCacheRecord(
                repoRoot,
                assetCacheDirectory,
                discoveredNwbFile,
                AStringView(metaText),
                firstToken,
                scratchArena,
                relativePathText,
                cacheFileName,
                cacheRecord
            );

#if !defined(NWB_BUILDMODE)
            if(cacheRecord.canWriteCache && !IsLikelyNonStandaloneFirstToken(firstToken)){
                const auto cacheResult = CookMetadataDetail::TryLoadNwbFileAssetCache(
                    cacheRecord,
                    fileIndex,
                    cookArena,
                    cpuScheduler,
                    scratchArena,
                    seenPropertyAssetPathHashes,
                    outMetadata
                );
                if(cacheResult == CookMetadataDetail::NwbCacheLoadResult::Error)
                    return false;
                if(cacheResult == CookMetadataDetail::NwbCacheLoadResult::Hit){
                    cacheRecord.standalone = true;
                    cacheRecord.loadedFromCache = true;
                    parsedAnyMetadata = true;
                    continue;
                }
            }
#endif
        }

        const auto parsedDoc = __hidden_cook_metadata::ParseMetascriptDocumentText(discoveredNwbFile.filePath, AStringView(metaText), cookArena);
        if(!parsedDoc)
            return false;
        const auto& doc = *parsedDoc;

        b8 fileIsStandalone = true;
        AssetBunchExpandContext assetBunchExpandContext{ discoveredNwbFile.assetRoot, discoveredNwbFile.virtualRoot.view(), discoveredNwbFile.filePath, doc, scratchArena };
        auto expandedAssets = TryAutoCollectedAssetBunchExpanders(assetBunchExpandContext);
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
                    cookArena,
                    cpuScheduler,
                    scratchArena,
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
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: meta '{}' declares multiple asset objects without an asset_bunch object"), PathToString<tchar>(discoveredNwbFile.filePath));
            return false;
        }

        if(!__hidden_cook_metadata::ParseSingleAssetDocument(discoveredNwbFile, fileIndex, doc, cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, outMetadata, fileIsStandalone))
            return false;
        if(useAssetCache)
            outMetadata.fileCacheRecords[fileIndex].standalone = fileIsStandalone;
        parsedAnyMetadata = true;
    }

    if(!parsedAnyMetadata){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: no asset metadata found in asset roots"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

