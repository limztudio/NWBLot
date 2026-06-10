// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "metadata_parser.h"

#include "cook_extension.h"

#include <impl/assets_bunch/cook.h>

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeMetadataParser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsVolumeCookDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ParseDeclaredAssetItem(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const DiscoveredNwbFile& discoveredNwbFile,
    const Name assetType,
    const Name virtualPath,
    const Core::Metascript::Value& asset,
    ParsedAssetMetadata& outMetadata,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    Core::Alloc::ThreadPool& threadPool,
    ScratchArena& scratchArena
){
    AssetVolumeValueMetadataParseContext metadataParseContext{
        cookArena,
        shaderCook,
        discoveredNwbFile,
        assetType,
        virtualPath,
        asset,
        outMetadata,
        scratchArena
    };
    const AssetVolumeMetadataParseResult metadataParseResult = TryAutoCollectedValueMetadataParsers(metadataParseContext);
    if(metadataParseResult == AssetVolumeMetadataParseResult::Parsed)
        return true;
    if(metadataParseResult == AssetVolumeMetadataParseResult::Error)
        return false;

    CookEntryParseContext parseContext{
        cookArena,
        shaderCook,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseAssetMetadata(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const DiscoveredNwbFileVector& nwbFiles,
    ParsedAssetMetadata& outMetadata,
    Core::Alloc::ThreadPool& threadPool,
    ScratchArena& scratchArena
){
    outMetadata.includeMetadata.reserve(nwbFiles.size());
    outMetadata.shaderEntries.reserve(nwbFiles.size());
    outMetadata.entryRegistry.reserveEntries(nwbFiles.size());
    CookEntryPathHashSet seenPropertyAssetPathHashes(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        cookArena
    );

    seenPropertyAssetPathHashes.reserve(nwbFiles.size());

    for(const DiscoveredNwbFile& discoveredNwbFile : nwbFiles){
        const Path& nwbFile = discoveredNwbFile.filePath;
        Core::Metascript::Document doc(cookArena);
        if(!shaderCook.parseDocument(nwbFile, doc))
            return false;

        const usize assetBunchDeclarationCount = AssetsBunchCook::AssetBunchDeclarationCount(doc);
        const usize nonAssetBunchDeclarationCount = AssetsBunchCook::NonAssetBunchDeclarationCount(doc);
        if(assetBunchDeclarationCount > 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: meta '{}' declares multiple asset_bunch objects")
                , PathToString<tchar>(nwbFile)
            );
            return false;
        }
        if(assetBunchDeclarationCount == 1u){
            AssetsBunchCook::ExpandedAssetVector expandedAssets(scratchArena);
            if(!AssetsBunchCook::ExpandAssetBunch(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                expandedAssets,
                scratchArena
            ))
                return false;

            for(const AssetsBunchCook::ExpandedAsset& expandedAsset : expandedAssets){
                if(!ParseDeclaredAssetItem(
                    cookArena,
                    shaderCook,
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
            }
            continue;
        }
        if(nonAssetBunchDeclarationCount > 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: meta '{}' declares multiple asset objects without an asset_bunch object")
                , PathToString<tchar>(nwbFile)
            );
            return false;
        }

        const AStringView rawAssetTypeText(doc.assetType().data(), doc.assetType().size());
        const Name assetType = ToName(rawAssetTypeText);
        AssetVolumeDocumentMetadataParseContext metadataParseContext{
            cookArena,
            shaderCook,
            discoveredNwbFile,
            assetType,
            doc,
            outMetadata,
            scratchArena
        };
        const AssetVolumeMetadataParseResult metadataParseResult = TryAutoCollectedDocumentMetadataParsers(metadataParseContext);
        if(metadataParseResult == AssetVolumeMetadataParseResult::Parsed)
            continue;
        if(metadataParseResult == AssetVolumeMetadataParseResult::Error)
            return false;

        if(outMetadata.entryRegistry.has(assetType)){
            CookEntryParseContext parseContext{
                cookArena,
                shaderCook,
                threadPool,
                scratchArena,
                seenPropertyAssetPathHashes
            };
            if(!outMetadata.entryRegistry.parseDocument(
                assetType,
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                parseContext
            ))
                return false;
            continue;
        }

        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: unsupported asset type '{}' in meta '{}'")
            , StringConvert(rawAssetTypeText)
            , PathToString<tchar>(nwbFile)
        );
        return false;
    }

    if(outMetadata.shaderEntries.empty()){
        if(!outMetadata.entryRegistry.empty())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: no asset metadata found in asset roots"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

