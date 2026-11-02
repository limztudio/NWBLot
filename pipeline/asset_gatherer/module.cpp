// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gather.h"

#include <pipeline/command_line.h>

#include <core/alloc/scratch.h>
#include <core/assets/gather_merge_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    Core::Assets::AssetArena arena(AssetGatherer::s_AssetGathererArena);
    Core::Alloc::ScratchArena scratchArena(AssetGatherer::s_AssetGathererArena);
    PipelineCommandLine commandLine(PipelineTool::AssetGatherer);
    return commandLine.run(argc, argv, [&](PipelineOptions& options){
        AssetGatherer::AssetGatherOptions gatherOptions(arena);
        gatherOptions.inputs = Move(options.inputs);
        gatherOptions.outputDirectory = Move(options.outputPath);
        gatherOptions.configuration = options.configuration;
        gatherOptions.mergePayloads = Core::Assets::QueryAutoCollectedAssetGatherMerge(scratchArena);
        return AssetGatherer::GatherAssets(gatherOptions, scratchArena) ? s_PipelineExitSuccess : s_PipelineExitFailure;
    }, arena, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

