// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../command_line.h"

#include "gather.h"
#include <impl/assets_graphics/gather.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    NWB::Core::Assets::AssetArena arena(Name("pipeline/asset_gatherer"));
    PipelineOptions parsed(arena);
    const auto result = ParseCommandLine(argc, argv, PipelineTool::AssetGatherer, parsed);
    if(result != CommandLineParseResult::Success)
        return result == CommandLineParseResult::Help ? 0 : 1;

    NWB::Pipeline::AssetGatherer::AssetGatherOptions options(arena);
    options.inputs = Move(parsed.inputs);
    options.outputDirectory = Move(parsed.outputDirectory);
    options.configuration = parsed.configuration;
    options.mergePayloads = &NWB::Impl::MergeGatheredGraphicsAsset;
    return NWB::Pipeline::AssetGatherer::GatherAssets(options) ? 0 : 1;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

