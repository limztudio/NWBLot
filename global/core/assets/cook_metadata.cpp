
#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"
#include "arena_names.h"
#include "auto_registration.h"

#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AutoMetadataParser{
    AssetDocumentMetadataParseFunction documentFunction = nullptr;
    AssetValueMetadataParseFunction valueFunction = nullptr;
};

AutoRegistrationQueue<AutoMetadataParser>& QueryAutoMetadataParserQueue(){
    static AutoRegistrationQueue<AutoMetadataParser> queue(AssetsArenaScope::s_MetadataParserQueueArena);
    return queue;
}

AutoRegistrationQueue<AssetBunchExpandFunction>& QueryAutoAssetBunchExpanderQueue(){
    static AutoRegistrationQueue<AssetBunchExpandFunction> queue(AssetsArenaScope::s_BunchExpanderQueueArena);
    return queue;
}

[[nodiscard]] static bool ParseMetascriptDocument(CookArena& cookArena, const Path& nwbFilePath, Core::Metascript::Document& outDoc){
    CookString metaText{cookArena};
    if(!ReadTextFile(nwbFilePath, metaText)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to read meta '{}'"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    StripUtf8Bom(metaText);

    if(!outDoc.parse(AStringView(metaText))){
        for(const Core::Metascript::ParseError& err : outDoc.errors()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: meta '{}' parse error at {}:{}: {}")
                , PathToString<tchar>(nwbFilePath)
                , err.line
                , err.column
                , StringConvert(AStringView(err.message.data(), err.message.size()))
            );
        }
        return false;
    }
    return true;
}

[[nodiscard]] static bool ParseDeclaredAssetItem(
    CookArena& cookArena,
    const DiscoveredNwbFile& discoveredNwbFile,
    const Name assetType,
    const Name virtualPath,
    const Core::Metascript::Value& asset,
    ParsedAssetMetadata& outMetadata,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    Core::Alloc::ThreadPool& threadPool,
    ScratchArena& scratchArena
){
    AssetValueMetadataParseContext metadataParseContext{
        cookArena,
        discoveredNwbFile,
        assetType,
        virtualPath,
        asset,
        outMetadata,
        scratchArena
    };
    const AssetMetadataParseResult::Enum metadataParseResult = TryAutoCollectedValueMetadataParsers(metadataParseContext);
    if(metadataParseResult == AssetMetadataParseResult::Parsed)
        return true;
    if(metadataParseResult == AssetMetadataParseResult::Error)
        return false;

    CookEntryParseContext parseContext{
        cookArena,
        threadPool,
        scratchArena,
        seenPropertyAssetPathHashes
    };
    return outMetadata.entryRegistry.parseValue(
        assetType,
        virtualPath,
        discoveredNwbFile.filePath,
        asset,
        parseContext
    );
}

[[nodiscard]] static bool ParseSingleAssetDocument(
    CookArena& cookArena,
    const DiscoveredNwbFile& discoveredNwbFile,
    const Core::Metascript::Document& doc,
    ParsedAssetMetadata& outMetadata,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    Core::Alloc::ThreadPool& threadPool,
    ScratchArena& scratchArena
){
    const AStringView rawAssetTypeText(doc.assetType().data(), doc.assetType().size());
    const Name assetType = ToName(rawAssetTypeText);
    AssetDocumentMetadataParseContext metadataParseContext{
        cookArena,
        discoveredNwbFile,
        assetType,
        doc,
        outMetadata,
        scratchArena
    };
    const AssetMetadataParseResult::Enum metadataParseResult = TryAutoCollectedDocumentMetadataParsers(metadataParseContext);
    if(metadataParseResult == AssetMetadataParseResult::Parsed)
        return true;
    if(metadataParseResult == AssetMetadataParseResult::Error)
        return false;

    if(outMetadata.entryRegistry.has(assetType)){
        CookEntryParseContext parseContext{
            cookArena,
            threadPool,
            scratchArena,
            seenPropertyAssetPathHashes
        };
        return outMetadata.entryRegistry.parseDocument(
            assetType,
            discoveredNwbFile.assetRoot,
            discoveredNwbFile.virtualRoot.view(),
            discoveredNwbFile.filePath,
            doc,
            parseContext
        );
    }

    NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: unsupported asset type '{}' in meta '{}'")
        , StringConvert(rawAssetTypeText)
        , PathToString<tchar>(discoveredNwbFile.filePath)
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetMetadataParserAutoRegistrar::AssetMetadataParserAutoRegistrar(
    const AssetDocumentMetadataParseFunction documentFunction,
    const AssetValueMetadataParseFunction valueFunction
){
    if(documentFunction == nullptr && valueFunction == nullptr)
        return;

    auto& queue = __hidden_cook_metadata::QueryAutoMetadataParserQueue();
    queue.appendUnique(
        __hidden_cook_metadata::AutoMetadataParser{
            documentFunction,
            valueFunction
        },
        [](const __hidden_cook_metadata::AutoMetadataParser& lhs, const __hidden_cook_metadata::AutoMetadataParser& rhs){
            return lhs.documentFunction == rhs.documentFunction && lhs.valueFunction == rhs.valueFunction;
        }
    );
}

AssetBunchExpanderAutoRegistrar::AssetBunchExpanderAutoRegistrar(const AssetBunchExpandFunction function){
    if(function == nullptr)
        return;

    auto& queue = __hidden_cook_metadata::QueryAutoAssetBunchExpanderQueue();
    queue.appendUnique(function, [](const AssetBunchExpandFunction lhs, const AssetBunchExpandFunction rhs){ return lhs == rhs; });
}

bool DiscoverFilesWithExtension(
    const CookVector<ResolvedAssetRoot>& assetRoots,
    const AStringView expectedExtension,
    DiscoveredNwbFileVector& outFiles,
    ScratchArena& scratchArena
){
    CookArena& cookArena = outFiles.get_allocator().arena();
    ErrorCode errorCode;
    HashSet<u64, Hasher<u64>, EqualTo<u64>, ScratchArena> seenPathHashes(
        0,
        Hasher<u64>(),
        EqualTo<u64>(),
        scratchArena
    );

    outFiles.clear();

    for(const ResolvedAssetRoot& assetRoot : assetRoots){
        errorCode.clear();
        if(!IsDirectory(assetRoot.path, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to query asset root '{}': {}")
                    , PathToString<tchar>(assetRoot.path)
                    , StringConvert(errorCode.message())
                );
                return false;
            }

            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset root is not a directory: '{}'")
                , PathToString<tchar>(assetRoot.path)
            );
            return false;
        }

        for(const auto& dirEntry : RecursiveDirectoryIterator(assetRoot.path, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: error scanning asset root '{}': {}")
                    , PathToString<tchar>(assetRoot.path)
                    , StringConvert(errorCode.message())
                );
                return false;
            }

            errorCode.clear();
            const bool isRegularFile = dirEntry.is_regular_file(errorCode);
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to inspect '{}' while scanning '{}': {}")
                    , PathToString<tchar>(dirEntry.path())
                    , PathToString<tchar>(assetRoot.path)
                    , StringConvert(errorCode.message())
                );
                return false;
            }
            if(!isRegularFile)
                continue;

            const Path& filePath = dirEntry.path();
            ScratchString extension = PathToString(scratchArena, filePath.extension());
            CanonicalizeTextInPlace(extension);
            if(extension != expectedExtension)
                continue;

            ScratchString normalizedPath = PathToString(scratchArena, filePath.lexically_normal());
            CanonicalizeTextInPlace(normalizedPath);
            if(!seenPathHashes.insert(ComputeFnv64Text(normalizedPath)).second)
                continue;

            outFiles.emplace_back(cookArena, assetRoot.path, filePath, normalizedPath, assetRoot.virtualRoot);
        }
    }

    Sort(
        outFiles.begin(),
        outFiles.end(),
        [](const DiscoveredNwbFile& lhs, const DiscoveredNwbFile& rhs){
            return lhs.normalizedPathText < rhs.normalizedPathText;
        }
    );

    return true;
}

bool AddPlannedFileCount(const u64 additionalFileCount, u64& inOutPlannedFileCount){
    if(inOutPlannedFileCount > Limit<u64>::s_Max - additionalFileCount){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: planned file count overflow"));
        return false;
    }

    inOutPlannedFileCount += additionalFileCount;
    return true;
}

AssetMetadataParseResult::Enum TryAutoCollectedDocumentMetadataParsers(AssetDocumentMetadataParseContext& context){
    Core::Alloc::ScratchArena scratchArena(AssetsArenaScope::s_DocumentMetadataParsersScratch);
    Vector<__hidden_cook_metadata::AutoMetadataParser, Core::Alloc::ScratchArena> parsers{scratchArena};
    __hidden_cook_metadata::QueryAutoMetadataParserQueue().copyTo(parsers);

    for(const __hidden_cook_metadata::AutoMetadataParser& parser : parsers){
        if(parser.documentFunction == nullptr)
            continue;

        const AssetMetadataParseResult::Enum result = parser.documentFunction(context);
        if(result == AssetMetadataParseResult::Unsupported)
            continue;
        return result;
    }

    return AssetMetadataParseResult::Unsupported;
}

AssetMetadataParseResult::Enum TryAutoCollectedValueMetadataParsers(AssetValueMetadataParseContext& context){
    Core::Alloc::ScratchArena scratchArena(AssetsArenaScope::s_ValueMetadataParsersScratch);
    Vector<__hidden_cook_metadata::AutoMetadataParser, Core::Alloc::ScratchArena> parsers{scratchArena};
    __hidden_cook_metadata::QueryAutoMetadataParserQueue().copyTo(parsers);

    for(const __hidden_cook_metadata::AutoMetadataParser& parser : parsers){
        if(parser.valueFunction == nullptr)
            continue;

        const AssetMetadataParseResult::Enum result = parser.valueFunction(context);
        if(result == AssetMetadataParseResult::Unsupported)
            continue;
        return result;
    }

    return AssetMetadataParseResult::Unsupported;
}

AssetBunchExpandResult::Enum TryAutoCollectedAssetBunchExpanders(AssetBunchExpandContext& context){
    Core::Alloc::ScratchArena scratchArena(AssetsArenaScope::s_AssetBunchExpandersScratch);
    Vector<AssetBunchExpandFunction, Core::Alloc::ScratchArena> functions{scratchArena};
    __hidden_cook_metadata::QueryAutoAssetBunchExpanderQueue().copyTo(functions);

    for(const AssetBunchExpandFunction function : functions){
        if(function == nullptr)
            continue;

        const AssetBunchExpandResult::Enum result = function(context);
        if(result == AssetBunchExpandResult::Unsupported)
            continue;
        return result;
    }

    return AssetBunchExpandResult::Unsupported;
}

bool ParseAssetMetadata(
    CookArena& cookArena,
    const DiscoveredNwbFileVector& nwbFiles,
    ParsedAssetMetadata& outMetadata,
    Core::Alloc::ThreadPool& threadPool,
    ScratchArena& scratchArena
){
    outMetadata.entryRegistry.reserveEntries(nwbFiles.size());
    CookEntryPathHashSet seenPropertyAssetPathHashes(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        cookArena
    );

    seenPropertyAssetPathHashes.reserve(nwbFiles.size());

    bool parsedAnyMetadata = false;
    for(const DiscoveredNwbFile& discoveredNwbFile : nwbFiles){
        Core::Metascript::Document doc(cookArena);
        if(!__hidden_cook_metadata::ParseMetascriptDocument(cookArena, discoveredNwbFile.filePath, doc))
            return false;

        ExpandedAssetMetadataVector expandedAssets(scratchArena);
        AssetBunchExpandContext assetBunchExpandContext{
            discoveredNwbFile.assetRoot,
            discoveredNwbFile.virtualRoot.view(),
            discoveredNwbFile.filePath,
            doc,
            expandedAssets,
            scratchArena
        };
        const AssetBunchExpandResult::Enum assetBunchResult = TryAutoCollectedAssetBunchExpanders(assetBunchExpandContext);
        if(assetBunchResult == AssetBunchExpandResult::Error)
            return false;
        if(assetBunchResult == AssetBunchExpandResult::Parsed){
            for(const ExpandedAssetMetadata& expandedAsset : expandedAssets){
                if(!expandedAsset.value){
                    NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset_bunch meta '{}' expanded a null asset value")
                        , PathToString<tchar>(discoveredNwbFile.filePath)
                    );
                    return false;
                }
                if(!__hidden_cook_metadata::ParseDeclaredAssetItem(
                    cookArena,
                    discoveredNwbFile,
                    expandedAsset.assetType,
                    expandedAsset.virtualPath,
                    *expandedAsset.value,
                    outMetadata,
                    seenPropertyAssetPathHashes,
                    threadPool,
                    scratchArena
                ))
                    return false;
                parsedAnyMetadata = true;
            }
            continue;
        }

        if(doc.declarations().size() > 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: meta '{}' declares multiple asset objects without an asset_bunch object")
                , PathToString<tchar>(discoveredNwbFile.filePath)
            );
            return false;
        }

        if(!__hidden_cook_metadata::ParseSingleAssetDocument(
            cookArena,
            discoveredNwbFile,
            doc,
            outMetadata,
            seenPropertyAssetPathHashes,
            threadPool,
            scratchArena
        ))
            return false;
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

