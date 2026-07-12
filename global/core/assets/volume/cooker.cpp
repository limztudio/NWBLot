
#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooker.h"

#include "arena_names.h"
#include "asset_volume_writer.h"
#include "cook_paths.h"
#include "cooked_object_cache.h"
#include "pack_manifest.h"
#include "volume_prepare_registry.h"

#include <global/core/assets/auto_registration.h>
#include <global/core/assets/cook_metadata.h>
#include <global/core/assets/cook_paths.h>
#include <global/core/assets/paths.h>

#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetCookerAutoRegistrar s_AssetVolumeCookerAutoRegistrar(&Core::Assets::CreateAssetCooker<AssetVolumeCooker>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetVolumeCooker::cook(const Core::Assets::AssetCookOptions& options){
    AssetVolumeCookResult result;
    if(!cookAssetVolume(options, result))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Asset volume cook complete [{}] - volume='{}', files={}, segments={}, mount='{}'"),
        StringConvert(options.configuration.c_str()),
        StringConvert(result.volumeName.c_str()),
        result.fileCount,
        result.segmentCount,
        StringConvert(options.outputDirectory)
    );

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetVolumeCooker::cookAssetVolume(const Core::Assets::AssetCookOptions& options, AssetVolumeCookResult& outResult){
    outResult = {};

    Core::Alloc::ScratchArena scratchArena(AssetsVolumeArenaScope::s_CookArena);

    Core::Assets::ResolvedCookPaths resolvedPaths(m_arena);
    if(!Core::Assets::ResolveCookPaths(options, resolvedPaths, scratchArena))
        return false;

    Core::Assets::DiscoveredNwbFileVector nwbFiles{ m_arena };
    if(!Core::Assets::DiscoverFilesWithExtension(
        resolvedPaths.assetRoots,
        Core::Assets::s_NwbExtension,
        nwbFiles,
        scratchArena
    ))
        return false;

    Core::Assets::ParsedAssetMetadata parsedMetadata(m_arena);
    if(!Core::Assets::RegisterAutoCollectedCookEntryTypes(parsedMetadata.entryRegistry))
        return false;
    if(!Core::Assets::ParseAssetMetadata(
        m_arena,
        nwbFiles,
        parsedMetadata,
        options.services.threadPool,
        scratchArena
    ))
        return false;

    Core::Assets::CookString configurationSafeName = BuildCanonicalSafeCacheName(m_arena, options.configuration.view());
    if(configurationSafeName.empty())
        configurationSafeName = "default";

    u64 plannedFileCount = 0u;
    AssetsVolumeCookDetail::AssetVolumeManifestCookerVector manifestCookers(m_arena);
    AssetsVolumeCookDetail::AssetVolumePrepareContext prepareContext{
        m_arena,
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

    AssetsVolumeCookDetail::AssetVolumePackManifest manifest(m_arena);
    if(!AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(manifest, plannedFileCount))
        return false;

    AssetsVolumeCookDetail::VirtualPathHashSet seenVirtualPathHashes{m_arena};
    if(plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(plannedFileCount));

    for(const AssetsVolumeCookDetail::AssetVolumeManifestCooker& manifestCooker : manifestCookers){
        if(!manifestCooker(manifest, seenVirtualPathHashes, scratchArena))
            return false;
    }
    if(!AssetsVolumeCookDetail::BuildRegistryObjectManifestEntries(
        m_arena,
        options.services.threadPool,
        resolvedPaths,
        configurationSafeName,
        parsedMetadata,
        manifest,
        seenVirtualPathHashes
    ))
        return false;

    AssetsVolumeCookDetail::AssetVolumeWriteResult volumeResult;
    if(!AssetsVolumeCookDetail::WriteAssetVolume(
        m_arena,
        resolvedPaths,
        configurationSafeName,
        manifest,
        volumeResult,
        scratchArena
    ))
        return false;

    outResult.volumeName = volumeResult.volumeName;
    outResult.fileCount = volumeResult.fileCount;
    outResult.segmentCount = volumeResult.segmentCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

