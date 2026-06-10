// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "metadata_parser.h"

#include <impl/assets_shader/asset.h>

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsMetadataParser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename EntryT, typename PathHashSetT, typename EntryVectorT>
static bool AppendUniquePropertyAssetEntry(EntryT& entry, PathHashSetT& seenPathHashes, EntryVectorT& outEntries){
    if(!entry.virtualPath)
        return true;

    const NameHash pathHash = entry.virtualPath.hash();
    if(!seenPathHashes.insert(pathHash).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate property asset virtual path '{}'")
            , StringConvert(entry.virtualPath.c_str())
        );
        return false;
    }

    outEntries.push_back(Move(entry));
    return true;
}

template<typename ShaderIdentityKeySetT>
static bool AppendUniqueShaderEntry(
    ShaderCook::ShaderEntry& shaderEntry,
    const Path& nwbFilePath,
    ShaderIdentityKeySetT& seenShaderIdentityKeys,
    ShaderEntryVector& outShaderEntries
){
    if(shaderEntry.name.empty())
        return true;

    const PreparedShaderKey shaderIdentityKey{
        ToName(shaderEntry.name),
        ToName(shaderEntry.archiveStage.view())
    };
    if(!seenShaderIdentityKeys.insert(shaderIdentityKey).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate shader identity '{}' for stage '{}' from meta '{}'")
            , StringConvert(shaderEntry.name)
            , StringConvert(shaderEntry.archiveStage.c_str())
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outShaderEntries.push_back(Move(shaderEntry));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseAssetMetadata(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const DiscoveredNwbFileVector& nwbFiles,
    const DiscoveredBindFileVector& bindFiles,
    ParsedAssetMetadata& outMetadata,
    Core::Alloc::ThreadPool& threadPool,
    ScratchArena& scratchArena
){
    outMetadata.includeMetadata.reserve(nwbFiles.size());
    outMetadata.shaderEntries.reserve(nwbFiles.size());
    outMetadata.materialBindEntries.reserve(bindFiles.size());
    outMetadata.materialEntries.reserve(nwbFiles.size());
    outMetadata.meshEntries.reserve(nwbFiles.size());
    outMetadata.skinnedMeshEntries.reserve(nwbFiles.size());
    outMetadata.skinEntries.reserve(nwbFiles.size());
    outMetadata.skeletonEntries.reserve(nwbFiles.size());
    outMetadata.modelEntries.reserve(nwbFiles.size());
    outMetadata.csgShapeEntries.reserve(nwbFiles.size());

    HashSet<
        PreparedShaderKey,
        PreparedShaderKeyHasher,
        EqualTo<PreparedShaderKey>,
        ShaderCook::CookArena
    > seenShaderIdentityKeys{
        0,
        PreparedShaderKeyHasher(),
        EqualTo<PreparedShaderKey>(),
        cookArena
    };
    HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, ScratchArena> seenPropertyAssetPathHashes(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        scratchArena
    );

    seenShaderIdentityKeys.reserve(nwbFiles.size());
    seenPropertyAssetPathHashes.reserve(nwbFiles.size());

    for(const DiscoveredNwbFile& discoveredNwbFile : nwbFiles){
        const Path& nwbFile = discoveredNwbFile.filePath;
        Core::Metascript::Document doc(cookArena);
        if(!shaderCook.parseDocument(nwbFile, doc))
            return false;

        const AStringView rawAssetTypeText(doc.assetType().data(), doc.assetType().size());
        const Name assetType = ToName(rawAssetTypeText);
        if(assetType == Shader::AssetTypeName()){
            ShaderCook::ShaderEntry shaderEntry(cookArena);
            if(!shaderCook.parseShaderMeta(nwbFile, doc, shaderEntry, scratchArena))
                return false;

            if(!Core::Assets::BuildDerivedAssetVirtualPath(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                Path(shaderEntry.source),
                shaderEntry.name
            ))
                return false;

            if(!AppendUniqueShaderEntry(shaderEntry, nwbFile, seenShaderIdentityKeys, outMetadata.shaderEntries))
                return false;
            continue;
        }

        if(assetType == s_IncludeAssetTypeName){
            ShaderCook::IncludeEntry includeEntry(cookArena);
            if(!shaderCook.parseIncludeMeta(nwbFile, doc, includeEntry, scratchArena))
                return false;

            if(!includeEntry.source.empty() && !includeEntry.defineValues.empty()){
                ErrorCode errorCode;
                const Path absSource = AbsolutePath(Path(includeEntry.source), errorCode).lexically_normal();
                if(errorCode){
                    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve include metadata source '{}' from '{}': {}")
                        , StringConvert(includeEntry.source)
                        , PathToString<tchar>(nwbFile)
                        , StringConvert(errorCode.message())
                    );
                    return false;
                }

                ScratchString key = PathToString(scratchArena, absSource);
                CanonicalizeTextInPlace(key);
                CookString cookKey(key, cookArena);
                if(!outMetadata.includeMetadata.emplace(Move(cookKey), Move(includeEntry)).second){
                    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate include metadata for source '{}'")
                        , PathToString<tchar>(absSource)
                    );
                    return false;
                }
            }

            continue;
        }

        if(assetType == Material::AssetTypeName()){
            MaterialCookEntry materialEntry(cookArena);
            if(!ParseMaterialCookMetadata(
                shaderCook,
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                materialEntry,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(materialEntry, seenPropertyAssetPathHashes, outMetadata.materialEntries))
                return false;
            continue;
        }

        if(assetType == AssetsCsgCook::s_CsgShapeAssetTypeName){
            AssetsCsgCook::CsgShapeCookEntry csgShapeEntry(cookArena);
            if(!AssetsCsgCook::ParseCsgShapeCookMetadata(
                cookArena,
                nwbFile,
                doc,
                csgShapeEntry,
                scratchArena
            ))
                return false;

            outMetadata.csgShapeEntries.push_back(Move(csgShapeEntry));
            continue;
        }

        if(assetType == SkinnedMesh::AssetTypeName()){
            SkinnedMeshCookEntry meshEntry(cookArena);
            if(!ParseSkinnedMeshCookMetadata(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                meshEntry,
                threadPool,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(meshEntry, seenPropertyAssetPathHashes, outMetadata.skinnedMeshEntries))
                return false;
            continue;
        }

        if(assetType == Skin::AssetTypeName()){
            SkinCookEntry skinEntry(cookArena);
            if(!ParseSkinCookMetadata(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                skinEntry,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(skinEntry, seenPropertyAssetPathHashes, outMetadata.skinEntries))
                return false;
            continue;
        }

        if(assetType == Skeleton::AssetTypeName()){
            SkeletonCookEntry skeletonEntry(cookArena);
            if(!ParseSkeletonCookMetadata(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                skeletonEntry,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(skeletonEntry, seenPropertyAssetPathHashes, outMetadata.skeletonEntries))
                return false;
            continue;
        }

        if(assetType == Model::AssetTypeName()){
            ModelCookEntry modelEntry(cookArena);
            if(!ParseModelCookMetadata(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                modelEntry,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(modelEntry, seenPropertyAssetPathHashes, outMetadata.modelEntries))
                return false;
            continue;
        }

        if(assetType == Mesh::AssetTypeName()){
            MeshCookEntry meshEntry(cookArena);
            if(!ParseMeshCookMetadata(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                meshEntry,
                threadPool,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(meshEntry, seenPropertyAssetPathHashes, outMetadata.meshEntries))
                return false;
            continue;
        }

        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: unsupported asset type '{}' in meta '{}'")
            , StringConvert(rawAssetTypeText)
            , PathToString<tchar>(nwbFile)
        );
        return false;
    }

    for(const DiscoveredNwbFile& discoveredBindFile : bindFiles){
        MaterialBindEntry bindEntry(cookArena);
        if(!ParseMaterialBindSource(discoveredBindFile.filePath, bindEntry, scratchArena))
            return false;

        if(!Core::Assets::BuildDerivedAssetVirtualPath(
            discoveredBindFile.assetRoot,
            discoveredBindFile.virtualRoot.view(),
            discoveredBindFile.filePath,
            bindEntry.virtualPath
        ))
            return false;

        outMetadata.materialBindEntries.push_back(Move(bindEntry));
    }

    if(!AssetsCsgCook::AssignCsgShapeCookIds(outMetadata.csgShapeEntries))
        return false;

    if(outMetadata.shaderEntries.empty()){
        if(!outMetadata.materialEntries.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: material assets require at least one shader entry"));
            return false;
        }
        if(!outMetadata.meshEntries.empty())
            return true;
        if(!outMetadata.skinnedMeshEntries.empty())
            return true;
        if(!outMetadata.skinEntries.empty())
            return true;
        if(!outMetadata.skeletonEntries.empty())
            return true;
        if(!outMetadata.modelEntries.empty())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: no graphics asset metadata found in asset roots"));
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

