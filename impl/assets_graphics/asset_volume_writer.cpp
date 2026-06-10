// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_volume_writer.h"

#include "cook_paths.h"
#include "shader_volume_writer.h"

#include <impl/assets_model/cook.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_mesh/cook.h>
#include <impl/assets_mesh/skin_cook.h>
#include <impl/assets_skeleton/cook.h>

#include <core/assets/module.h>
#include <core/filesystem/module.h>
#include <core/graphics/shader_archive.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 s_DefaultSegmentSize = 512ull * 1024ull * 1024ull;
static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u64 EstimateRequiredMetadataBytes(const u64 fileCount){
    if(fileCount == 0)
        return s_DefaultMetadataSize;

    u64 totalBytes = 0;
    if(!Core::Filesystem::ComputeVolumeMetadataRequirement(fileCount, totalBytes))
        return Limit<u64>::s_Max;

    constexpr u64 s_MetadataPaddingBytes = 4ull * 1024ull;
    if(totalBytes <= Limit<u64>::s_Max - s_MetadataPaddingBytes)
        totalBytes += s_MetadataPaddingBytes;

    return Max(totalBytes, s_DefaultMetadataSize);
}

static bool ConfigureVolumeSizing(const u64 plannedFileCount, Core::Filesystem::VolumeBuildConfig& outConfig){
    if(!outConfig.volumeName.assign(AssetsGraphicsCookDetail::s_GraphicsVolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: volume name '{}' exceeds ACompactString capacity"), StringConvert(AssetsGraphicsCookDetail::s_GraphicsVolumeName));
        return false;
    }
    outConfig.metadataSize = EstimateRequiredMetadataBytes(plannedFileCount);
    if(outConfig.metadataSize == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: metadata size overflow while planning volume"));
        return false;
    }

    outConfig.segmentSize = s_DefaultSegmentSize;
    while(outConfig.segmentSize <= outConfig.metadataSize){
        if(outConfig.segmentSize > Limit<u64>::s_Max / 2ull){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: segment size overflow while planning volume"));
            return false;
        }
        outConfig.segmentSize *= 2ull;
    }

    return true;
}

static bool ReserveShaderIndexRecords(
    const AssetsGraphicsCookDetail::PreparedShaderVector& preparedEntries,
    Core::GraphicsVector<Core::ShaderArchive::Record>& outShaderIndexRecords,
    usize& outShaderRecordCount
){
    outShaderRecordCount = 0u;

    u64 shaderRecordCount = 0;
    for(const AssetsGraphicsCookDetail::PreparedShaderEntry& preparedEntry : preparedEntries){
        if(shaderRecordCount > Limit<u64>::s_Max - preparedEntry.variantCount){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader record count overflow"));
            return false;
        }
        shaderRecordCount += preparedEntry.variantCount;
    }
    if(shaderRecordCount > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader record count exceeds container capacity"));
        return false;
    }

    outShaderIndexRecords.clear();
    outShaderRecordCount = static_cast<usize>(shaderRecordCount);
    outShaderIndexRecords.reserve(outShaderRecordCount);
    return true;
}

static bool RegisterVolumeAssetPath(
    const tchar* assetKind,
    const Name& virtualPath,
    AssetsGraphicsCookDetail::VirtualPathHashSet& inOutSeenVirtualPathHashes
){
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: invalid {} virtual path"), assetKind);
        return false;
    }

    const NameHash virtualPathHash = virtualPath.hash();
    if(!inOutSeenVirtualPathHashes.insert(virtualPathHash).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate {} virtual path '{}'")
            , assetKind
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    return true;
}

static bool PushSerializedAssetToVolume(
    const tchar* assetKind,
    const Name& virtualPath,
    const Core::Assets::IAsset& asset,
    const Core::Assets::IAssetCodec& codec,
    Core::Filesystem::VolumeSession& volumeSession,
    Core::Assets::AssetBytes& scratchBinary
){
    scratchBinary.clear();
    if(!codec.serialize(asset, scratchBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to serialize {} '{}'")
            , assetKind
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    if(!volumeSession.pushDataDeferred(virtualPath, scratchBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to push {} '{}'")
            , assetKind
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    return true;
}

template<typename AssetT, typename AssetCodecT, typename EntryVectorT, typename BuildAssetFn>
[[nodiscard]] bool AppendBuiltAssetsToVolume(
    const tchar* assetKind,
    EntryVectorT& assetEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    AssetsGraphicsCookDetail::VirtualPathHashSet& inOutSeenVirtualPathHashes,
    const bool logBuildFailure,
    BuildAssetFn&& buildAsset
){
    AssetCodecT assetCodec;
    Core::Assets::AssetArena& assetArena = assetEntries.get_allocator().arena();
    Core::Assets::AssetBytes assetBinary{assetArena};

    for(auto& assetEntry : assetEntries){
        if(!RegisterVolumeAssetPath(assetKind, assetEntry.virtualPath, inOutSeenVirtualPathHashes))
            return false;

        AssetT cookedAsset(assetArena);
        if(!buildAsset(assetEntry, cookedAsset)){
            if(logBuildFailure){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to build {} '{}'")
                    , assetKind
                    , StringConvert(assetEntry.virtualPath.c_str())
                );
            }
            return false;
        }

        if(!PushSerializedAssetToVolume(
            assetKind,
            assetEntry.virtualPath,
            cookedAsset,
            assetCodec,
            volumeSession,
            assetBinary
        ))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteGraphicsVolume(
    Core::Alloc::GlobalArena& arena,
    ShaderCook& shaderCook,
    const ResolvedCookPaths& resolvedPaths,
    const AStringView configurationSafeName,
    PreparedShaderPlan& preparedPlan,
    ParsedAssetMetadata& parsedMetadata,
    GraphicsVolumeWriteResult& outResult,
    ScratchArena& scratchArena
){
    outResult = {};

    Core::GraphicsVector<Core::ShaderArchive::Record> shaderIndexRecords{ arena };
    usize shaderIndexRecordCount = 0u;
    if(!__hidden_asset_volume_writer::ReserveShaderIndexRecords(preparedPlan.preparedEntries, shaderIndexRecords, shaderIndexRecordCount))
        return false;

    VirtualPathHashSet seenVirtualPathHashes{arena};
    if(preparedPlan.plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(preparedPlan.plannedFileCount));
    const Name& shaderIndexVirtualPath = Core::ShaderArchive::IndexVirtualPathName();
    seenVirtualPathHashes.insert(shaderIndexVirtualPath.hash());

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    if(!__hidden_asset_volume_writer::ConfigureVolumeSizing(preparedPlan.plannedFileCount, volumeConfig))
        return false;

    const StagedVolumePaths stagedVolumePaths = BuildStagedVolumePaths(
        resolvedPaths.outputDirectory,
        volumeConfig.volumeName,
        configurationSafeName,
        scratchArena
    );
    if(!Core::Filesystem::EnsureEmptyStagedDirectory(
        stagedVolumePaths.stageDirectory,
        s_GraphicsCookerLogPrefix,
        "stage directory"
    ))
        return false;
    Core::Filesystem::StagedDirectoryCleanupGuard stageDirectoryCleanup(
        stagedVolumePaths.stageDirectory,
        s_GraphicsCookerLogPrefix
    );
    if(!Core::Filesystem::RemoveStagedDirectoryIfPresent(
        stagedVolumePaths.backupDirectory,
        s_GraphicsCookerLogPrefix,
        "backup directory"
    ))
        return false;

    u64 stagedFileCount = 0;
    usize stagedSegmentCount = 0;
    {
        Core::Filesystem::VolumeSession volumeSession(arena);
        if(!volumeSession.create(stagedVolumePaths.stageDirectory, volumeConfig)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to create staged volume session"));
            return false;
        }

        if(!AppendPreparedShadersToVolume(
            arena,
            shaderCook,
            resolvedPaths.cacheDirectory,
            configurationSafeName,
            preparedPlan.preparedEntries,
            volumeSession,
            seenVirtualPathHashes,
            shaderIndexRecordCount,
            shaderIndexRecords,
            scratchArena
        ))
            return false;

        Core::GraphicsBytes indexBinary{arena};
        if(!Core::ShaderArchive::serializeIndex(shaderIndexRecords, indexBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to serialize shader index"));
            return false;
        }
        if(!volumeSession.pushDataDeferred(shaderIndexVirtualPath, indexBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to push shader index"));
            return false;
        }

        if(!__hidden_asset_volume_writer::AppendBuiltAssetsToVolume<Material, MaterialAssetCodec>(
            NWB_TEXT("material"),
            parsedMetadata.materialEntries,
            volumeSession,
            seenVirtualPathHashes,
            false,
            [](const MaterialCookEntry& materialEntry, Material& outMaterial){
                return BuildMaterialAsset(materialEntry, outMaterial);
            }
        ))
            return false;
        if(!__hidden_asset_volume_writer::AppendBuiltAssetsToVolume<Mesh, MeshAssetCodec>(
            NWB_TEXT("mesh"),
            parsedMetadata.meshEntries,
            volumeSession,
            seenVirtualPathHashes,
            true,
            [](MeshCookEntry& meshEntry, Mesh& outMesh){
                return BuildMeshAsset(meshEntry, outMesh);
            }
        ))
            return false;
        if(!__hidden_asset_volume_writer::AppendBuiltAssetsToVolume<SkinnedMesh, SkinnedMeshAssetCodec>(
            NWB_TEXT("skinned mesh"),
            parsedMetadata.skinnedMeshEntries,
            volumeSession,
            seenVirtualPathHashes,
            true,
            [](SkinnedMeshCookEntry& meshEntry, SkinnedMesh& outMesh){
                return BuildSkinnedMeshAsset(meshEntry, outMesh);
            }
        ))
            return false;
        if(!__hidden_asset_volume_writer::AppendBuiltAssetsToVolume<Skeleton, SkeletonAssetCodec>(
            NWB_TEXT("skeleton"),
            parsedMetadata.skeletonEntries,
            volumeSession,
            seenVirtualPathHashes,
            true,
            [](const SkeletonCookEntry& skeletonEntry, Skeleton& outSkeleton){
                return BuildSkeletonAsset(skeletonEntry, outSkeleton);
            }
        ))
            return false;
        if(!__hidden_asset_volume_writer::AppendBuiltAssetsToVolume<Skin, SkinAssetCodec>(
            NWB_TEXT("skin"),
            parsedMetadata.skinEntries,
            volumeSession,
            seenVirtualPathHashes,
            true,
            [](SkinCookEntry& skinEntry, Skin& outSkin){
                return BuildSkinAsset(skinEntry, outSkin);
            }
        ))
            return false;
        if(!__hidden_asset_volume_writer::AppendBuiltAssetsToVolume<Model, ModelAssetCodec>(
            NWB_TEXT("model"),
            parsedMetadata.modelEntries,
            volumeSession,
            seenVirtualPathHashes,
            true,
            [](ModelCookEntry& modelEntry, Model& outModel){
                return BuildModelAsset(modelEntry, outModel);
            }
        ))
            return false;
        if(!volumeSession.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to flush staged volume metadata"));
            return false;
        }

        stagedFileCount = volumeSession.fileCount();
        stagedSegmentCount = volumeSession.segmentCount();
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig.volumeName, stagedSegmentCount))
        return false;
    stageDirectoryCleanup.dismiss();

    if(!outResult.volumeName.assign(s_GraphicsVolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: volume name exceeds ACompactString capacity"));
        return false;
    }
    outResult.fileCount = stagedFileCount;
    outResult.segmentCount = static_cast<u64>(stagedSegmentCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

