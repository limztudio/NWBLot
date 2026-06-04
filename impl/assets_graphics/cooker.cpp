// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooker.h"

#include "asset_volume_writer.h"
#include "cook_paths.h"
#include "material_validation.h"
#include "metadata_parser.h"
#include "shader_cook_plan.h"

#include <core/assets/auto_registration.h>
#include <core/assets/paths.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCooker> CreateGraphicsAssetCooker(Core::Alloc::GlobalArena& arena){
    return MakeUnique<GraphicsAssetCooker>(arena);
}
Core::Assets::AssetCookerAutoRegistrar s_GraphicsAssetCookerAutoRegistrar(&CreateGraphicsAssetCooker);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GraphicsAssetCooker::cook(const Core::Assets::AssetCookOptions& options){
    GraphicsCookEnvironment environment(m_arena, options.services.threadPool);
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot.c_str());
    environment.assetRoots.reserve(options.assetRoots.size());
    for(const Core::Assets::AssetString& assetRoot : options.assetRoots)
        environment.assetRoots.push_back(Path(assetRoot.c_str()));
    environment.outputDirectory = Path(options.outputDirectory.c_str());
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory.c_str());

    GraphicsCookResult result;
    if(!cookGraphicsAssets(environment, result))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Graphics asset cook complete [{}] - volume='{}', files={}, segments={}, mount='{}'"),
        StringConvert(options.configuration.c_str()),
        StringConvert(result.volumeName.c_str()),
        result.fileCount,
        result.segmentCount,
        PathToString<tchar>(environment.outputDirectory)
    );

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GraphicsAssetCooker::cookGraphicsAssets(const GraphicsCookEnvironment& environment, GraphicsCookResult& outResult){
    outResult = {};

    ShaderCook shaderCook(m_arena);
    Core::Alloc::ScratchArena scratchArena;

    AssetsGraphicsCookDetail::ResolvedCookPaths resolvedPaths(m_arena);
    if(!AssetsGraphicsCookDetail::ResolveCookPaths(environment, resolvedPaths, scratchArena))
        return false;

    AssetsGraphicsCookDetail::DiscoveredNwbFileVector nwbFiles{ m_arena };
    if(!AssetsGraphicsCookDetail::DiscoverFilesWithExtension(
        resolvedPaths.assetRoots,
        Core::Assets::s_NwbExtension,
        nwbFiles,
        scratchArena
    ))
        return false;

    AssetsGraphicsCookDetail::DiscoveredBindFileVector bindFiles{ m_arena };
    if(!AssetsGraphicsCookDetail::DiscoverFilesWithExtension(
        resolvedPaths.assetRoots,
        MaterialBindNames::SourceExtensionText(),
        bindFiles,
        scratchArena
    ))
        return false;

    AssetsGraphicsCookDetail::ParsedAssetMetadata parsedMetadata(m_arena);
    if(!AssetsGraphicsMetadataParser::ParseAssetMetadata(
        m_arena,
        shaderCook,
        nwbFiles,
        bindFiles,
        parsedMetadata,
        environment.threadPool,
        scratchArena
    ))
        return false;
    if(!ValidateMaterialCookInterfaces(parsedMetadata.materialBindEntries, parsedMetadata.materialEntries, scratchArena))
        return false;

    AssetsGraphicsCookDetail::CookString configurationSafeName = BuildCanonicalSafeCacheName(m_arena, environment.configuration.view());
    if(configurationSafeName.empty())
        configurationSafeName = "default";

    Path materialBindIncludeRoot;
    if(!EmitMaterialBindIncludes(
        m_arena,
        resolvedPaths.cacheDirectory,
        configurationSafeName,
        parsedMetadata.materialBindEntries,
        materialBindIncludeRoot,
        scratchArena
    ))
        return false;

    Path csgShapeIncludeRoot;
    if(!AssetsCsgCook::EmitCsgShapeModuleIncludes(
        m_arena,
        resolvedPaths.cacheDirectory,
        configurationSafeName,
        parsedMetadata.csgShapeEntries,
        csgShapeIncludeRoot,
        scratchArena
    ))
        return false;

    AssetsGraphicsCookDetail::PreparedShaderPlan preparedPlan(m_arena);
    if(!AssetsGraphicsCookDetail::PrepareShaderEntriesForCook(
        m_arena,
        shaderCook,
        resolvedPaths,
        materialBindIncludeRoot,
        csgShapeIncludeRoot,
        parsedMetadata.includeMetadata,
        parsedMetadata.shaderEntries,
        parsedMetadata.materialEntries,
        preparedPlan,
        scratchArena
    ))
        return false;
    if(!AssetsGraphicsCookDetail::AddPlannedFileCount(static_cast<u64>(parsedMetadata.materialEntries.size()), preparedPlan.plannedFileCount))
        return false;
    if(!AssetsGraphicsCookDetail::AddPlannedFileCount(static_cast<u64>(parsedMetadata.meshEntries.size()), preparedPlan.plannedFileCount))
        return false;
    if(!AssetsGraphicsCookDetail::AddPlannedFileCount(
        static_cast<u64>(parsedMetadata.skinnedMeshEntries.size()),
        preparedPlan.plannedFileCount
    ))
        return false;
    if(!AssetsGraphicsCookDetail::ValidateMaterials(
        shaderCook,
        preparedPlan.preparedEntries,
        parsedMetadata.materialEntries,
        scratchArena
    ))
        return false;

    AssetsGraphicsCookDetail::GraphicsVolumeWriteResult volumeResult;
    if(!AssetsGraphicsCookDetail::WriteGraphicsVolume(
        m_arena,
        shaderCook,
        resolvedPaths,
        configurationSafeName,
        preparedPlan,
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

