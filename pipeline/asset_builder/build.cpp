// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"

#include "build_inputs.h"
#include "cook_paths.h"

#include <core/assets/volume/arena_names.h>
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


bool BuildAssets(const AssetBuildOptions& options){
    Assets::AssetArena& arena = options.assetRoots.get_allocator().arena();
    if(!options.assetType.empty() && options.assetType.view() != "graphics"){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: unsupported --asset-type '{}'. Available types: graphics"), StringConvert(options.assetType.c_str()));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(Assets::AssetsVolumeArenaScope::s_CookArena);

    Assets::ResolvedCookPaths resolvedPaths(arena);
    if(!ResolveCookPaths(options, resolvedPaths, scratchArena))
        return false;

    Assets::DiscoveredNwbFileVector nwbFiles{ arena };
    if(!Assets::DiscoverFilesWithExtension(
        resolvedPaths.assetRoots,
        Assets::s_NwbExtension,
        nwbFiles,
        scratchArena
    ))
        return false;

    if((options.useExplicitInputs || !options.inputs.empty()) && !SelectBuildInputs(options, resolvedPaths, nwbFiles, scratchArena))
        return false;

    Assets::ParsedAssetMetadata parsedMetadata(arena);
    if(!Assets::RegisterAutoCollectedCookEntryTypes(parsedMetadata.entryRegistry))
        return false;
    if(!Assets::ParseAssetMetadata(
        arena,
        nwbFiles,
        parsedMetadata,
        options.services.threadPool,
        scratchArena
    ))
        return false;

    Assets::CookString configurationSafeName = BuildCanonicalSafeCacheName(arena, options.configuration.view());
    if(configurationSafeName.empty())
        configurationSafeName = "default";

    u64 plannedFileCount = 0u;
    Assets::AssetsVolumeCookDetail::AssetVolumeManifestCookerVector manifestCookers(arena);
    Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext prepareContext{
        arena,
        resolvedPaths,
        configurationSafeName,
        parsedMetadata,
        plannedFileCount,
        manifestCookers,
        scratchArena
    };
    if(!Assets::AssetsVolumeCookDetail::RegisterAutoCollectedAssetVolumePreparers(prepareContext))
        return false;
    if(!Assets::AddPlannedFileCount(parsedMetadata.entryRegistry.entryCount(), plannedFileCount))
        return false;

    Assets::AssetsVolumeCookDetail::AssetVolumePackManifest manifest(arena);
    if(!Assets::AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(manifest, plannedFileCount))
        return false;

    Assets::AssetsVolumeCookDetail::VirtualPathHashSet seenVirtualPathHashes{arena};
    if(plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(plannedFileCount));

    for(const Assets::AssetsVolumeCookDetail::AssetVolumeManifestCooker& manifestCooker : manifestCookers){
        if(!manifestCooker(manifest, seenVirtualPathHashes, scratchArena))
            return false;
    }
    if(!Assets::AssetsVolumeCookDetail::BuildRegistryObjectManifestEntries(
        arena,
        options.services.threadPool,
        resolvedPaths,
        configurationSafeName,
        parsedMetadata,
        manifest,
        seenVirtualPathHashes
    ))
        return false;

    if(!Assets::BuiltAssetDetail::WriteBuiltAssets(resolvedPaths.outputDirectory, manifest))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AssetBuilder: built {} assets into '{}'"), manifest.entries.size(), StringConvert(options.outputDirectory));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

