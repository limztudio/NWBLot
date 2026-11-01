// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "provider_catalog.h"

#include <pipeline/command_line.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    Core::Assets::AssetArena arena(DependencyComputer::s_DependencyComputerArena);
    Core::Alloc::ScratchArena scratchArena(DependencyComputer::s_DependencyComputerArena);
    PipelineCommandLine commandLine(PipelineTool::DependencyComputer);
    return commandLine.run(argc, argv, [&](PipelineOptions& parsed){
        const auto resolvedInputs = DependencyComputer::ComputeAssetDependencies(parsed, arena, scratchArena);
        if(!resolvedInputs)
            return s_PipelineExitFailure;
        const auto& inputs = *resolvedInputs;

        usize outputReserveBytes = 0u;
        for(const auto& input : inputs)
            outputReserveBytes += input.size() + DependencyComputer::s_LineFeedReserveBytes;
        AString<Core::Alloc::ScratchArena> text(scratchArena);
        text.reserve(outputReserveBytes);
        for(const auto& input : inputs){
            text += input;
            text += '\n';
        }
        const auto output = AbsolutePath(Path(arena, parsed.outputPath));
        if(!output || !EnsureDirectories(output->parentPath()) || !WriteTextFile(*output, AStringView(text))){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to write output '{}'"), ConvertString(parsed.outputPath));
            return s_PipelineExitFailure;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("DependencyComputer: returned {} inputs"), inputs.size());
        return s_PipelineExitSuccess;
    }, arena, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

