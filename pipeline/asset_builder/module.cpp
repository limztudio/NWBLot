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


static b8 ResolveRoots(const PipelineOptions& parsed, Core::Assets::ScratchArena& scratchArena, AssetBuilder::AssetBuildOptions& options){
    namespace Assets = Core::Assets;
    auto& arena = options.assetRoots.get_allocator().arena();
    auto repoRoot = ResolvePipelineRepoRoot(AStringView(options.repoRoot), NWB_TEXT("AssetBuilder"), arena);
    if(!repoRoot)
        return false;
    options.resolvedRepoRoot = Move(*repoRoot);
    auto roots = ResolvePipelineAssetRoots(
        options.resolvedRepoRoot,
        parsed.inputs,
        parsed.assetRoots,
        Assets::AssetRootDuplicatePolicy::ExactText,
        NWB_TEXT("AssetBuilder"),
        NWB_TEXT("an empty input list requires --asset-root"),
        scratchArena
    );
    if(!roots)
        return false;
    options.assetRoots.reserve(roots->size());
    for(Assets::ResolvedAssetRoot& root : *roots)
        options.assetRoots.emplace_back(Move(root.path), root.virtualRoot);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    Core::Assets::AssetArena arena(AssetBuilder::s_AssetBuilderArena);
    Core::Assets::ScratchArena scratchArena(AssetBuilder::s_AssetBuilderArena);
    PipelineCommandLine commandLine(PipelineTool::AssetBuilder);
    return commandLine.run(argc, argv, [&](PipelineOptions& options){
        const u32 cores = QueryCpuCoreCount(CpuAffinity::Any);
        Core::CpuTaskScheduler cpuScheduler(AssetBuilder::ComputeWorkerCoreCount(cores));
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

