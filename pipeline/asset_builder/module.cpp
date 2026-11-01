// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"

#include <pipeline/command_line.h>

#include <core/task/cpu/scheduler.h>
#include <core/common/log.h>

#include <global/cpu_topology.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_AssetBuilderArena("pipeline/asset_builder");
inline constexpr u32 s_MinParallelCoreCount = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ResolveRoots(const PipelineOptions& parsed, Core::Assets::ScratchArena& scratchArena, AssetBuilder::AssetBuildOptions& options){
    namespace Assets = Core::Assets;
    auto& arena = options.assetRoots.get_allocator().arena();
    const auto repoRoot = AbsolutePath(Path(arena, options.repoRoot.empty() ? AStringView(".") : AStringView(options.repoRoot)));
    if(!repoRoot){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve repository root"));
        return false;
    }
    options.resolvedRepoRoot = repoRoot->lexicallyNormal();
    const auto& sources = parsed.assetRoots.empty() ? parsed.inputs : parsed.assetRoots;
    auto roots = Assets::ResolveAssetRoots(options.resolvedRepoRoot, sources, parsed.assetRoots.empty(), Assets::AssetRootDuplicatePolicy::ExactText, scratchArena);
    if(!roots){
        const auto& error = roots.error();
        if(error.reason == Assets::AssetRootResolutionFailure::InspectInput)
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to inspect input '{}'"), ConvertString(sources[error.sourceIndex]));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve asset root from '{}'"), ConvertString(sources[error.sourceIndex]));
        return false;
    }
    if(roots->empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: an empty input list requires --asset-root"));
        return false;
    }
    options.assetRoots.reserve(roots->size());
    for(Assets::ResolvedAssetRoot& root : *roots)
        options.assetRoots.emplace_back(Move(root.path), root.virtualRoot);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    Core::Assets::AssetArena arena(__hidden_asset_builder::s_AssetBuilderArena);
    Core::Assets::ScratchArena scratchArena(__hidden_asset_builder::s_AssetBuilderArena);
    PipelineCommandLine commandLine(PipelineTool::AssetBuilder);
    return commandLine.run(argc, argv, [&](PipelineOptions& options){
        const u32 cores = QueryCpuCoreCount(CpuAffinity::Any);
        Core::CpuTaskScheduler cpuScheduler(cores > __hidden_asset_builder::s_MinParallelCoreCount ? cores - __hidden_asset_builder::s_MinParallelCoreCount : 0u);
        AssetBuilder::AssetBuildOptions buildOptions(arena, cpuScheduler);
        buildOptions.repoRoot = options.repoRoot;
        buildOptions.outputDirectory = options.outputPath;
        buildOptions.configuration = options.configuration;
        if(options.assetType)
            buildOptions.assetType = *options.assetType;
        buildOptions.useExplicitInputs = true;
        if(!__hidden_asset_builder::ResolveRoots(options, scratchArena, buildOptions))
            return s_PipelineExitFailure;
        buildOptions.inputs = Move(options.inputs);
        const b8 built = AssetBuilder::BuildAssets(buildOptions, scratchArena);
        cpuScheduler.wait();
        return built ? s_PipelineExitSuccess : s_PipelineExitFailure;
    }, arena, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

