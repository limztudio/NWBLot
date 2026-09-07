// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"

#include "arena_names.h"
#include "built_asset.h"
#include "build_inputs.h"
#include "cook_paths.h"
#include "cooked_object_cache.h"
#include "pack_manifest.h"
#include "volume_prepare_registry.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/cook_paths.h>
#include <core/assets/paths.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildAssets(const AssetBuildOptions& options){
    AssetArena& arena = options.assetRoots.get_allocator().arena();
    if(!options.assetType.empty() && options.assetType.view() != "graphics"){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: unsupported --asset-type '{}'. Available types: graphics"), StringConvert(options.assetType.c_str()));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(AssetsVolumeArenaScope::s_CookArena);

    Core::Assets::ResolvedCookPaths resolvedPaths(arena);
    if(!Core::Assets::ResolveCookPaths(options, resolvedPaths, scratchArena))
        return false;

    Core::Assets::DiscoveredNwbFileVector nwbFiles{ arena };
    if(!Core::Assets::DiscoverFilesWithExtension(
        resolvedPaths.assetRoots,
        Core::Assets::s_NwbExtension,
        nwbFiles,
        scratchArena
    ))
        return false;

    if((options.useExplicitInputs || !options.inputs.empty()) && !SelectBuildInputs(options, resolvedPaths, nwbFiles, scratchArena))
        return false;

    Core::Assets::ParsedAssetMetadata parsedMetadata(arena);
    if(!Core::Assets::RegisterAutoCollectedCookEntryTypes(parsedMetadata.entryRegistry))
        return false;
    if(!Core::Assets::ParseAssetMetadata(
        arena,
        nwbFiles,
        parsedMetadata,
        options.services.threadPool,
        scratchArena
    ))
        return false;

    Core::Assets::CookString configurationSafeName = BuildCanonicalSafeCacheName(arena, options.configuration.view());
    if(configurationSafeName.empty())
        configurationSafeName = "default";

    u64 plannedFileCount = 0u;
    AssetsVolumeCookDetail::AssetVolumeManifestCookerVector manifestCookers(arena);
    AssetsVolumeCookDetail::AssetVolumePrepareContext prepareContext{
        arena,
        resolvedPaths,
        configurationSafeName,
        parsedMetadata,
        plannedFileCount,
        manifestCookers,
        scratchArena
    };
    if(!AssetsVolumeCookDetail::RegisterAutoCollectedAssetVolumePreparers(prepareContext))
        return false;
    if(!Core::Assets::AddPlannedFileCount(parsedMetadata.entryRegistry.entryCount(), plannedFileCount))
        return false;

    AssetsVolumeCookDetail::AssetVolumePackManifest manifest(arena);
    if(!AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(manifest, plannedFileCount))
        return false;

    AssetsVolumeCookDetail::VirtualPathHashSet seenVirtualPathHashes{arena};
    if(plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(plannedFileCount));

    for(const AssetsVolumeCookDetail::AssetVolumeManifestCooker& manifestCooker : manifestCookers){
        if(!manifestCooker(manifest, seenVirtualPathHashes, scratchArena))
            return false;
    }
    if(!AssetsVolumeCookDetail::BuildRegistryObjectManifestEntries(
        arena,
        options.services.threadPool,
        resolvedPaths,
        configurationSafeName,
        parsedMetadata,
        manifest,
        seenVirtualPathHashes
    ))
        return false;

    if(!BuiltAssetDetail::WriteBuiltAssets(resolvedPaths.outputDirectory, manifest))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AssetBuilder: built {} assets into '{}'"), manifest.entries.size(), StringConvert(options.outputDirectory));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

