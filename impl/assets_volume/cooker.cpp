// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooker.h"

#include "asset_volume_writer.h"
#include "cook_extension.h"
#include "cook_paths.h"

#include <core/assets/auto_registration.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/cook_paths.h>
#include <core/assets/paths.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCooker> CreateAssetVolumeCooker(Core::Alloc::GlobalArena& arena){
    return MakeUnique<AssetVolumeCooker>(arena);
}
Core::Assets::AssetCookerAutoRegistrar s_AssetVolumeCookerAutoRegistrar(&CreateAssetVolumeCooker);


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

    Core::Alloc::ScratchArena scratchArena;

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
    AssetsVolumeCookDetail::AssetVolumeExternalWriterVector externalWriters(m_arena);
    AssetsVolumeCookDetail::AssetVolumePrepareContext prepareContext{
        m_arena,
        resolvedPaths,
        configurationSafeName,
        parsedMetadata,
        plannedFileCount,
        externalWriters,
        scratchArena
    };
    if(!AssetsVolumeCookDetail::RegisterAutoCollectedAssetVolumePreparers(prepareContext))
        return false;
    if(!Core::Assets::AddPlannedFileCount(parsedMetadata.entryRegistry.entryCount(), plannedFileCount))
        return false;

    AssetsVolumeCookDetail::AssetVolumeWriteResult volumeResult;
    if(!AssetsVolumeCookDetail::WriteAssetVolume(
        m_arena,
        resolvedPaths,
        configurationSafeName,
        plannedFileCount,
        externalWriters,
        parsedMetadata,
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

