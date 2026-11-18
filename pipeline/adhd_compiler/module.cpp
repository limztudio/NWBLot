// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compile.h"

#include <pipeline/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_module{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_AdhdCompilerArena("pipeline/adhd_compiler");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    Core::Assets::AssetArena arena(__hidden_adhd_compiler_module::s_AdhdCompilerArena);
    Core::Alloc::ScratchArena scratchArena(__hidden_adhd_compiler_module::s_AdhdCompilerArena);
    PipelineCommandLine commandLine(PipelineTool::AdhdCompiler);
    return commandLine.run(argc, argv, [&](PipelineOptions& options){
        AdhdCompiler::AdhdCompileOptions compileOptions(arena);
        compileOptions.inputs = options.inputs;
        compileOptions.includeDirectories = options.includeDirectories;
        compileOptions.cppHeaders = options.cppHeaders;
        compileOptions.cppIncludeDirectories = options.cppIncludeDirectories;
        compileOptions.macroDefinitions = options.macroDefinitions;
        compileOptions.cppDefines = options.cppDefines;
        compileOptions.cppCompilerArgs = options.cppCompilerArgs;
        compileOptions.outputDirectory = options.outputPath;
        compileOptions.customNamespace = options.customNamespace;
        compileOptions.namespaceMapPath = options.namespaceMapPath;
        compileOptions.depfilePath = options.depfilePath;
        compileOptions.emitCppExternsPath = options.emitCppExternsPath;
        return AdhdCompiler::CompileAdhd(compileOptions) ? s_PipelineExitSuccess : s_PipelineExitFailure;
    }, arena, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

