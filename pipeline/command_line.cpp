// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <core/alloc/scratch.h>
#include <core/assets/input_list.h>
#include <core/common/log.h>

#include <global/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_BEGIN


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


PipelineCommandLine::PipelineCommandLine(const PipelineTool::Enum inTool)
    : m_tool(inTool)
    , m_app(s_PipelineAppDescription.data())
{
    m_app.add_option(s_PipelineInputOption.data(), m_inputs, "Input assets; accepts multiple paths and repeated options");
    m_app.add_option(s_PipelineInputListOption.data(), m_inputList, "UTF-8 newline-separated input paths");
    m_app.add_option(s_PipelineOutputOption.data(), m_outputPath, m_tool == PipelineTool::DependencyComputer ? "Output dependency list file" : "Output directory")->required();
    if(m_tool == PipelineTool::AssetBuilder || m_tool == PipelineTool::DependencyComputer){
        m_app.add_option(s_PipelineRepoRootOption.data(), m_repoRoot, "Repository root; defaults to the working directory");
        m_app.add_option(s_PipelineAssetRootOption.data(), m_assetRoots, "Asset root directories used to resolve virtual paths");
    }
    if(m_tool == PipelineTool::AssetBuilder)
        m_app.add_option(s_PipelineAssetTypeOption.data(), m_assetType, "Asset build domain; graphics is currently supported");
    if(m_tool == PipelineTool::AssetBuilder || m_tool == PipelineTool::AssetGatherer)
        m_app.add_option(s_PipelineConfigurationOption.data(), m_configuration, "Build configuration label");
    if(m_tool == PipelineTool::AdhdCompiler){
        m_app.add_option(s_PipelineIncludeDirOption.data(), m_includeDirectories, "Include search directories");
        m_app.add_option(s_PipelineDepfileOption.data(), m_depfilePath, "Output Makefile-style dependency file");
        m_app.add_option(s_PipelineNamespaceOption.data(), m_customNamespace, "Generated C++ enclosing namespace");
    }
}

Expected<PipelineOptions> PipelineCommandLine::parse(const int argc, char** argv, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    PipelineOptions options(arena);
    CommandLineParseApp(m_app, argc, argv);

    const b8 allowImplicitProjectInput = m_tool == PipelineTool::DependencyComputer && !m_assetRoots.empty();
    if(m_inputs.empty() && m_inputList.empty() && !allowImplicitProjectInput){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: provide --input or --input-list"));
        return MakeUnexpected(Failure{});
    }
    if(
        !__hidden_pipeline_command_line::AssignInputs(m_inputs, options.inputs)
        || !__hidden_pipeline_command_line::AssignInputs(m_assetRoots, options.assetRoots)
        || !__hidden_pipeline_command_line::AssignInputs(m_includeDirectories, options.includeDirectories)
        || !__hidden_pipeline_command_line::AssignText(m_repoRoot, options.repoRoot)
        || !__hidden_pipeline_command_line::AssignText(m_outputPath, options.outputPath)
        || !__hidden_pipeline_command_line::AssignText(m_depfilePath, options.depfilePath)
        || !__hidden_pipeline_command_line::AssignText(m_customNamespace, options.customNamespace)
    )
        return MakeUnexpected(Failure{});
    if(options.outputPath.empty()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: output path must not be empty"));
        return MakeUnexpected(Failure{});
    }
    if(m_tool == PipelineTool::AssetBuilder && m_app.count(s_PipelineAssetTypeOption.data()) != 0u)
        options.assetType.emplace();
    if(!options.configuration.assign(AStringView(m_configuration.data(), m_configuration.size())) || (options.assetType && !options.assetType->assign(AStringView(m_assetType.data(), m_assetType.size())))){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: configuration or asset type exceeds ACompactString capacity"));
        return MakeUnexpected(Failure{});
    }
    if(!m_inputList.empty()){
        const AStringView listText(m_inputList.data(), m_inputList.size());
        if(!IsSingleLinePathText(listText)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Pipeline: paths must not contain nulls or newlines"));
            return MakeUnexpected(Failure{});
        }
        if(!Core::Assets::ReadAssetInputList(Path(arena, listText), scratchArena, options.inputs, m_tool == PipelineTool::AssetGatherer))
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


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

