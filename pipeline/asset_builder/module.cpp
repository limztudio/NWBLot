// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"
#include "../command_line.h"

#include <core/assets/cook_paths.h>
#include <core/task/cpu/scheduler.h>
#include <core/common/log.h>

#include <global/cpu_topology.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_AssetBuilderArena("pipeline/asset_builder");
inline constexpr u32 s_MinParallelCoreCount = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ResolveRoots(const PipelineOptions& parsed, NWB::Pipeline::AssetBuilder::AssetBuildOptions& options){
    namespace Assets = NWB::Core::Assets;
    auto& arena = options.assetRoots.get_allocator().arena();
    const auto repoRoot = AbsolutePath(Path(arena, options.repoRoot.empty() ? AStringView(".") : AStringView(options.repoRoot)));
    if(!repoRoot){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve repository root"));
        return false;
    }
    Assets::ScratchArena scratchArena(s_AssetBuilderArena);
    const auto& sources = parsed.assetRoots.empty() ? parsed.inputs : parsed.assetRoots;
    auto roots = Assets::ResolveAssetRoots(
        *repoRoot,
        sources,
        parsed.assetRoots.empty(),
        Assets::AssetRootDuplicatePolicy::ExactText,
        scratchArena
    );
    if(!roots){
        const auto& error = roots.error();
        if(error.reason == Assets::AssetRootResolutionFailure::InspectInput)
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to inspect input '{}'"), StringConvert(sources[error.sourceIndex]));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve asset root from '{}'")
                , StringConvert(sources[error.sourceIndex])
            );
        return false;
    }
    if(roots->empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: an empty input list requires --asset-root"));
        return false;
    }
    options.assetRoots.reserve(roots->size());
    for(const Assets::ResolvedAssetRoot& root : *roots){
        const auto text = PathToString(arena, root.path);
        options.assetRoots.emplace_back(arena, AStringView(text), root.virtualRoot);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    NWB::Core::Assets::AssetArena arena(__hidden_asset_builder::s_AssetBuilderArena);
    PipelineCommandLine commandLine(PipelineTool::AssetBuilder);
    return commandLine.run(argc, argv, arena, [&](PipelineOptions& options){
        const u32 cores = QueryCpuCoreCount(CpuAffinity::Any);
        NWB::Core::CpuTaskScheduler cpuScheduler(cores > __hidden_asset_builder::s_MinParallelCoreCount ? cores - __hidden_asset_builder::s_MinParallelCoreCount : 0u);
        NWB::Pipeline::AssetBuilder::AssetBuildOptions buildOptions(arena, cpuScheduler);
        buildOptions.repoRoot = options.repoRoot;
        buildOptions.outputDirectory = options.outputPath;
        buildOptions.cacheDirectory = options.cacheDirectory;
        buildOptions.configuration = options.configuration;
        buildOptions.assetType = options.assetType;
        buildOptions.inputs = options.inputs;
        buildOptions.useExplicitInputs = true;
        if(!__hidden_asset_builder::ResolveRoots(options, buildOptions))
            return s_PipelineExitFailure;
        const bool built = NWB::Pipeline::AssetBuilder::BuildAssets(buildOptions);
        cpuScheduler.wait();
        return built ? s_PipelineExitSuccess : s_PipelineExitFailure;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

