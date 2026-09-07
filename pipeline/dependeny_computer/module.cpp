// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../command_line.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    NWB::Core::Assets::AssetArena arena(Name("pipeline/dependeny_computer"));
    PipelineOptions options(arena);
    const auto result = ParseCommandLine(argc, argv, PipelineTool::DependencyComputer, options);
    if(result != CommandLineParseResult::Success)
        return result == CommandLineParseResult::Help ? 0 : 1;

    NWB::Core::Assets::AssetString text(arena);
    for(const auto& input : options.inputs){
        text += input;
        text += '\n';
    }
    ErrorCode error;
    const Path output = AbsolutePath(Path(arena, options.outputDirectory), error);
    if(error || !EnsureDirectories(output.parent_path(), error) || !WriteTextFile(output, AStringView(text))){
        NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to write output '{}'"), StringConvert(options.outputDirectory));
        return 1;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("DependencyComputer: returned {} inputs unchanged"), options.inputs.size());
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

