// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../command_line.h"
#include "gather.h"

#include <core/assets/gather_merge_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    NWB::Core::Assets::AssetArena arena(Name("pipeline/asset_gatherer"));
    PipelineCommandLine commandLine(PipelineTool::AssetGatherer);
    return commandLine.run(argc, argv, arena, [&](PipelineOptions& options){
        NWB::Pipeline::AssetGatherer::AssetGatherOptions gatherOptions(arena);
        gatherOptions.inputs = Move(options.inputs);
        gatherOptions.outputDirectory = Move(options.outputPath);
        gatherOptions.configuration = options.configuration;
        gatherOptions.mergePayloads = NWB::Core::Assets::QueryAutoCollectedAssetGatherMerge();
        return NWB::Pipeline::AssetGatherer::GatherAssets(gatherOptions) ? 0 : 1;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

