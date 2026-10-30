// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../command_line.h"
#include "compile.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    NWB::Core::Assets::AssetArena arena(Name("pipeline/adhd_compiler"));
    PipelineCommandLine commandLine(PipelineTool::AdhdCompiler);
    return commandLine.run(argc, argv, arena, [&](PipelineOptions& options){
        NWB::Pipeline::AdhdCompiler::AdhdCompileOptions compileOptions(arena);
        compileOptions.inputs = Move(options.inputs);
        compileOptions.includeDirectories = Move(options.includeDirectories);
        compileOptions.macroDefinitions = Move(options.macroDefinitions);
        compileOptions.outputDirectory = Move(options.outputPath);
        compileOptions.customNamespace = Move(options.customNamespace);
        compileOptions.depfilePath = Move(options.depfilePath);
        compileOptions.validateOnly = options.validateOnly;
        return NWB::Pipeline::AdhdCompiler::CompileAdhd(compileOptions) ? s_PipelineExitSuccess : s_PipelineExitFailure;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

