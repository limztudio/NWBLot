// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"
#include "../root_discovery.h"

#include <core/assets/volume/built_asset.h>
#include <core/assets/volume/cooked_object_cache.h>
#include <core/assets/volume/pack_manifest.h>
#include <core/assets/volume/volume_prepare_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SelectBuildInputs(const AssetBuildOptions& options, const Assets::ResolvedCookPaths& paths, Assets::ScratchArena& scratchArena, Assets::DiscoveredNwbFileVector& files){
    if(options.inputs.empty()){
        files.clear();
        return true;
    }

    Vector<u8, Assets::ScratchArena> selected(files.size(), u8(0), scratchArena);
    // Borrow discovered canonical paths until selection finishes; compact the owning file vector only afterward.
    const auto selectionResult = Assets::SelectDiscoveredNwbInputs(paths.repoRoot, paths.assetRoots, files, options.inputs, true, scratchArena, selected);
    if(!selectionResult){
        LogCookInputSelectionFailure(selectionResult.error(), NWB_TEXT("AssetBuilder"));
        return false;
    }

    Assets::CompactSelectedDiscoveredNwbFiles(selected, files);
    return true;
}

b8 BuildAssets(const AssetBuildOptions& options, Assets::ScratchArena& scratchArena){
    Assets::AssetArena& arena = options.assetRoots.get_allocator().arena();
    if(!ValidateAssetBuildType(options.assetType.view(), Assets::AssetsVolumeCookDetail::s_AssetVolumeName))
        return false;

    auto paths = ResolveCookPaths(options, arena);
    if(!paths)
        return false;
    const Assets::ResolvedCookPaths& resolvedPaths = *paths;

    auto discoveredFiles = Assets::DiscoverFilesWithExtension(resolvedPaths.assetRoots, Assets::s_NwbExtension, arena, scratchArena);
    if(!discoveredFiles)
        return false;

    auto& nwbFiles = *discoveredFiles;

    if((options.useExplicitInputs || !options.inputs.empty()) && !SelectBuildInputs(options, resolvedPaths, scratchArena, nwbFiles))
        return false;

    Assets::ParsedAssetMetadata parsedMetadata(arena);
    if(!Assets::RegisterAutoCollectedCookEntryTypes(parsedMetadata.entryRegistry, scratchArena))
        return false;
    if(!Assets::ParseAssetMetadata(nwbFiles, resolvedPaths.repoRoot, resolvedPaths.cacheDirectory, arena, options.services.cpuScheduler, scratchArena, parsedMetadata))
        return false;

    Assets::CookString configurationSafeName = BuildCanonicalSafeCacheName(arena, options.configuration.view());
    if(configurationSafeName.empty())
        configurationSafeName = s_DefaultPipelineConfig;

    u64 plannedFileCount = 0u;
    Assets::AssetsVolumeCookDetail::AssetVolumeManifestCookerVector manifestCookers(arena);
    Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext prepareContext{ arena, resolvedPaths, configurationSafeName, parsedMetadata, plannedFileCount, manifestCookers, scratchArena };
    if(!Assets::AssetsVolumeCookDetail::RegisterAutoCollectedAssetVolumePreparers(prepareContext))
        return false;
    if(!Assets::AddPlannedFileCount(parsedMetadata.entryRegistry.entryCount(), plannedFileCount))
        return false;

    Assets::AssetsVolumeCookDetail::AssetVolumePackManifest manifest(arena);
    if(!Assets::AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(plannedFileCount, manifest))
        return false;

    Assets::CookEntryPathHashSet seenVirtualPathHashes{scratchArena};
    if(plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(plannedFileCount));

    for(const Assets::AssetsVolumeCookDetail::AssetVolumeManifestCooker& manifestCooker : manifestCookers){
        if(!manifestCooker(manifest, seenVirtualPathHashes, scratchArena))
            return false;
    }
    if(!Assets::AssetsVolumeCookDetail::BuildRegistryObjectManifestEntries(configurationSafeName, arena, options.services.cpuScheduler, scratchArena, parsedMetadata, manifest, seenVirtualPathHashes))
        return false;

    if(!Assets::BuiltAssetDetail::WriteBuiltAssets(resolvedPaths.outputDirectory, manifest, scratchArena))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AssetBuilder: built {} assets into '{}'"), manifest.entries.size(), PathToString<tchar>(options.outputDirectory));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

