// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooker.h"

#include "asset_volume_writer.h"
#include "cook_extension.h"
#include "cook_paths.h"
#include "metadata_parser.h"

#include <core/assets/auto_registration.h>
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
    AssetVolumeCookEnvironment environment(m_arena, options.services.threadPool);
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot.c_str());
    environment.assetRoots.reserve(options.assetRoots.size());
    for(const Core::Assets::AssetString& assetRoot : options.assetRoots)
        environment.assetRoots.push_back(Path(assetRoot.c_str()));
    environment.outputDirectory = Path(options.outputDirectory.c_str());
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory.c_str());

    AssetVolumeCookResult result;
    if(!cookAssetVolume(environment, result))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Asset volume cook complete [{}] - volume='{}', files={}, segments={}, mount='{}'"),
        StringConvert(options.configuration.c_str()),
        StringConvert(result.volumeName.c_str()),
        result.fileCount,
        result.segmentCount,
        PathToString<tchar>(environment.outputDirectory)
    );

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetVolumeCooker::cookAssetVolume(const AssetVolumeCookEnvironment& environment, AssetVolumeCookResult& outResult){
    outResult = {};

    Core::Alloc::ScratchArena scratchArena;

    AssetsVolumeCookDetail::ResolvedCookPaths resolvedPaths(m_arena);
    if(!AssetsVolumeCookDetail::ResolveCookPaths(environment, resolvedPaths, scratchArena))
        return false;

    AssetsVolumeCookDetail::DiscoveredNwbFileVector nwbFiles{ m_arena };
    if(!AssetsVolumeCookDetail::DiscoverFilesWithExtension(
        resolvedPaths.assetRoots,
        Core::Assets::s_NwbExtension,
        nwbFiles,
        scratchArena
    ))
        return false;

    AssetsVolumeCookDetail::ParsedAssetMetadata parsedMetadata(m_arena);
    if(!AssetsVolumeCookDetail::RegisterDefaultCookEntryTypes(parsedMetadata.entryRegistry))
        return false;
    if(!AssetsVolumeCookDetail::RegisterAutoCollectedCookEntryTypes(parsedMetadata.entryRegistry))
        return false;
    if(!AssetsVolumeMetadataParser::ParseAssetMetadata(
        m_arena,
        nwbFiles,
        parsedMetadata,
        environment.threadPool,
        scratchArena
    ))
        return false;

    AssetsVolumeCookDetail::CookString configurationSafeName = BuildCanonicalSafeCacheName(m_arena, environment.configuration.view());
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
    if(!AssetsVolumeCookDetail::AddPlannedFileCount(parsedMetadata.entryRegistry.entryCount(), plannedFileCount))
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

