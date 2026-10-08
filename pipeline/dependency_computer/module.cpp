// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../command_line.h"
#include "dependencies.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_dependency_computer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_DependencyComputerArena("pipeline/dependency_computer");
inline constexpr usize s_LineFeedReserveBytes = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

int RunPipelineTool(const int argc, char** argv){
    NWB::Core::Assets::AssetArena arena(__hidden_dependency_computer::s_DependencyComputerArena);
    PipelineCommandLine commandLine(PipelineTool::DependencyComputer);
    return commandLine.run(argc, argv, arena, [&](PipelineOptions& parsed){
        NWB::Core::Alloc::ScratchArena scratchArena(__hidden_dependency_computer::s_DependencyComputerArena);
        Expected<NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString>> resolvedInputs = MakeUnexpected(Failure{});
        if(parsed.includeSkinDependencies){
            resolvedInputs = ComputeSkinDependencies(parsed, arena, scratchArena);
            if(!resolvedInputs)
                return s_PipelineExitFailure;
        }
        const auto& inputs = parsed.includeSkinDependencies ? *resolvedInputs : parsed.inputs;

        usize outputReserveBytes = 0u;
        for(const auto& input : inputs)
            outputReserveBytes += input.size() + __hidden_dependency_computer::s_LineFeedReserveBytes;
        NWB::Core::Assets::AssetString text(arena);
        text.reserve(outputReserveBytes);
        for(const auto& input : inputs){
            text += input;
            text += '\n';
        }
        const auto output = AbsolutePath(Path(arena, parsed.outputPath));
        if(!output || !EnsureDirectories(output->parentPath()) || !WriteTextFile(*output, AStringView(text))){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to write output '{}'"), StringConvert(parsed.outputPath));
            return s_PipelineExitFailure;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("DependencyComputer: returned {} inputs (skin dependencies: {})")
            , inputs.size()
            , parsed.includeSkinDependencies
        );
        return s_PipelineExitSuccess;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

