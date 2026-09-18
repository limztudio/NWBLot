// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../command_line.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_dependency_computer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_DependencyComputerArena("pipeline/dependeny_computer");
inline constexpr usize s_LineFeedReserveBytes = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

int RunPipelineTool(const int argc, char** argv){
    NWB::Core::Assets::AssetArena arena(__hidden_dependency_computer::s_DependencyComputerArena);
    PipelineOptions options(arena);
    PipelineCommandLine commandLine(PipelineTool::DependencyComputer);
    return commandLine.run(argc, argv, options, [&](PipelineOptions& parsed){
        usize outputReserveBytes = 0u;
        for(const auto& input : parsed.inputs)
            outputReserveBytes += input.size() + __hidden_dependency_computer::s_LineFeedReserveBytes;
        NWB::Core::Assets::AssetString text(arena);
        text.reserve(outputReserveBytes);
        for(const auto& input : parsed.inputs){
            text += input;
            text += '\n';
        }
        ErrorCode error;
        const Path output = AbsolutePath(Path(arena, parsed.outputDirectory), error);
        if(error || !EnsureDirectories(output.parent_path(), error) || !WriteTextFile(output, AStringView(text))){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to write output '{}'"), StringConvert(parsed.outputDirectory));
            return s_PipelineExitFailure;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("DependencyComputer: returned {} inputs unchanged"), parsed.inputs.size());
        return s_PipelineExitSuccess;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

