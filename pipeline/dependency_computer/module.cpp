// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "provider_catalog.h"

#include <pipeline/command_line.h>

#include <core/assets/input_list.h>
#include <core/common/log.h>

#include <global/filesystem/staged_file_writer.h>


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

        const auto text = Core::Assets::EncodeAssetInputList(inputs, scratchArena);
        if(!text){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("DependencyComputer: input paths cannot be represented in the dependency list"));
            return s_PipelineExitFailure;
        }
        const auto output = AbsolutePath(Path(arena, parsed.outputPath));
        if(!output || !EnsureDirectories(output->parentPath()) || !StagedFileWriter(*output).writeTextAtomic(AStringView(*text))){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("DependencyComputer: failed to write output '{}'"), ConvertString(parsed.outputPath));
            return s_PipelineExitFailure;
        }
        NWB_LOGGER_ESSENTIAL_INFO(FOUNDATION_TEXT("DependencyComputer: returned {} inputs"), inputs.size());
        return s_PipelineExitSuccess;
    }, arena, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

