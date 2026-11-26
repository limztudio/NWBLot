// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <core/alloc/scratch.h>
#include <core/assets/input_list.h>
#include <core/common/log.h>

#include <global/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_BEGIN

namespace CompilerBootstrap{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pipeline_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 AssignText(const AStringView text, Core::Assets::AssetString& output){
    if(!text.empty() && !IsSingleLinePathText(text)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: paths must not contain nulls or newlines"));
        return false;
    }
    output.assign(text);
    return true;
}

static b8 AssignInputs(const Span<const AInteropString> values, Core::Assets::AssetVector<Core::Assets::AssetString>& outputs){
    auto& arena = outputs.get_allocator().arena();
    outputs.reserve(values.size());
    for(const AInteropString& value : values){
        Core::Assets::AssetString text(arena);
        if(value.empty() || !AssignText(value, text)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: input path must not be empty"));
            return false;
        }
        outputs.push_back(Move(text));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PipelineCommandLine::PipelineCommandLine()
    : m_app(s_PipelineAppDescription.data())
{
    m_app.add_option(s_PipelineInputOption.data(), m_inputs, "Input assets; accepts multiple paths and repeated options");
    m_app.add_option(s_PipelineInputListOption.data(), m_inputList, "UTF-8 newline-separated input paths");
    m_app.add_option(s_PipelineOutputOption.data(), m_outputPath, "Output directory")->required();
    m_app.add_option(s_PipelineIncludeDirOption.data(), m_includeDirectories, "Include search directories");
    m_app.add_option(s_PipelineDepfileOption.data(), m_depfilePath, "Output Makefile-style dependency file");
    m_app.add_option(s_PipelineNamespaceOption.data(), m_customNamespace, "Generated C++ enclosing namespace");
    m_app.add_option(s_PipelineNamespaceMapOption.data(), m_namespaceMapPath, "Optional tab-delimited map of source paths to C++ namespaces");
    m_app.add_option(s_PipelineCppHeaderOption.data(), m_cppHeaders, "Pre-imported C++ header paths");
    m_app.add_option(s_PipelineCppIncludeDirOption.data(), m_cppIncludeDirectories, "C++ header include search directories");
    m_app.add_option(s_PipelineDefineOption.data(), m_macroDefinitions, "ADHD preprocessor macro definitions");
    m_app.add_option(s_PipelineCppDefineOption.data(), m_cppDefines, "C++ preprocessor macro definitions for imported headers");
    m_app.add_option(s_PipelineCppArgOption.data(), m_cppCompilerArgs, "Additional Clang compiler arguments for imported headers");
    m_app.add_option(s_PipelineEmitCppExternsOption.data(), m_emitCppExternsPath, "Output file path for generated C++ extern ADHD declarations");
}

Expected<PipelineOptions> PipelineCommandLine::parse(const int argc, char** argv, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    PipelineOptions options(arena);
    CommandLineParseApp(m_app, argc, argv);

    if(m_inputs.empty() && m_inputList.empty()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: provide --input or --input-list"));
        return MakeUnexpected(Failure{});
    }
    if(
        !__hidden_pipeline_command_line::AssignInputs(m_inputs, options.inputs)
        || !__hidden_pipeline_command_line::AssignInputs(m_includeDirectories, options.includeDirectories)
        || !__hidden_pipeline_command_line::AssignInputs(m_cppHeaders, options.cppHeaders)
        || !__hidden_pipeline_command_line::AssignInputs(m_cppIncludeDirectories, options.cppIncludeDirectories)
        || !__hidden_pipeline_command_line::AssignInputs(m_macroDefinitions, options.macroDefinitions)
        || !__hidden_pipeline_command_line::AssignInputs(m_cppDefines, options.cppDefines)
        || !__hidden_pipeline_command_line::AssignInputs(m_cppCompilerArgs, options.cppCompilerArgs)
        || !__hidden_pipeline_command_line::AssignText(m_outputPath, options.outputPath)
        || !__hidden_pipeline_command_line::AssignText(m_depfilePath, options.depfilePath)
        || !__hidden_pipeline_command_line::AssignText(m_customNamespace, options.customNamespace)
        || !__hidden_pipeline_command_line::AssignText(m_namespaceMapPath, options.namespaceMapPath)
        || !__hidden_pipeline_command_line::AssignText(m_emitCppExternsPath, options.emitCppExternsPath)
    )
        return MakeUnexpected(Failure{});
    if(options.outputPath.empty()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: output path must not be empty"));
        return MakeUnexpected(Failure{});
    }
    if(!m_inputList.empty()){
        const AStringView listText(m_inputList.data(), m_inputList.size());
        if(!IsSingleLinePathText(listText)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: paths must not contain nulls or newlines"));
            return MakeUnexpected(Failure{});
        }
        if(!Core::Assets::ReadAssetInputList(Path(arena, listText), scratchArena, options.inputs, false))
            return MakeUnexpected(Failure{});
    }
    return options;
}

int PipelineCommandLine::exit(const CLI::ParseError& error)const{
    if(AStringView(error.get_name()) == s_PipelineCliHelpRequestName){
        FOUNDATION_COUT << m_app.help();
        return s_PipelineExitSuccess;
    }
    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: failed to parse command line: {}"), ConvertString(error.what()));
    FOUNDATION_CERR << m_app.help();
    return s_PipelineExitFailure;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

