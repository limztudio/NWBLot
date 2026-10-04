// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <core/assets/input_list.h>
#include <core/common/command_line.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AssignText(const AInteropString& value, NWB::Core::Assets::AssetString& output){
    const AStringView text(value.data(), value.size());
    if(!text.empty() && !IsSingleLinePathText(text)){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("Pipeline: paths must not contain nulls or newlines"));
        return false;
    }
    output.assign(text);
    return true;
}

static bool AssignInputs(const InteropVector<AInteropString>& values, NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString>& outputs){
    auto& arena = outputs.get_allocator().arena();
    outputs.reserve(values.size());
    for(const AInteropString& value : values){
        NWB::Core::Assets::AssetString text(arena);
        if(value.empty() || !AssignText(value, text)){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("Pipeline: input path must not be empty"));
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
    m_app.add_option(s_PipelineOutputOption.data(), m_outputPath,
        m_tool == PipelineTool::DependencyComputer ? "Output dependency list file" : "Output directory")->required();
    if(m_tool == PipelineTool::DependencyComputer)
        m_app.add_flag(s_PipelineSkinDependenciesOption.data(), m_includeSkinDependencies, "Include the textures referenced by selected UI skins");
    if(m_tool == PipelineTool::AssetBuilder || m_tool == PipelineTool::DependencyComputer){
        m_app.add_option(s_PipelineRepoRootOption.data(), m_repoRoot, "Repository root; defaults to the working directory");
        m_app.add_option(s_PipelineAssetRootOption.data(), m_assetRoots, "Asset root directories used to resolve virtual paths");
    }
    if(m_tool == PipelineTool::AssetBuilder){
        m_app.add_option(s_PipelineCacheDirectoryOption.data(), m_cacheDirectory, "Asset build cache directory");
        m_app.add_option(s_PipelineAssetTypeOption.data(), m_assetType, "Asset build domain; graphics is currently supported");
    }
    if(m_tool != PipelineTool::DependencyComputer)
        m_app.add_option(s_PipelineConfigurationOption.data(), m_configuration, "Build configuration label");
}

bool PipelineCommandLine::parse(const int argc, char** argv, PipelineOptions& options){
    CommandLineParseApp(m_app, argc, argv);

    if(m_inputs.empty() && m_inputList.empty()){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("Pipeline: provide --input or --input-list"));
        return false;
    }
    if(!__hidden_command_line::AssignInputs(m_inputs, options.inputs)
        || !__hidden_command_line::AssignInputs(m_assetRoots, options.assetRoots)
        || !__hidden_command_line::AssignText(m_repoRoot, options.repoRoot)
        || !__hidden_command_line::AssignText(m_outputPath, options.outputPath)
        || !__hidden_command_line::AssignText(m_cacheDirectory, options.cacheDirectory))
        return false;
    if(options.outputPath.empty()){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("Pipeline: output path must not be empty"));
        return false;
    }
    if(!options.configuration.assign(AStringView(m_configuration.data(), m_configuration.size()))
        || !options.assetType.assign(AStringView(m_assetType.data(), m_assetType.size()))){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("Pipeline: configuration or asset type exceeds ACompactString capacity"));
        return false;
    }
    if(!m_inputList.empty()){
        NWB::Core::Assets::AssetString listPath(options.inputs.get_allocator().arena());
        if(!__hidden_command_line::AssignText(m_inputList, listPath)
            || !NWB::Core::Assets::ReadAssetInputList(Path(listPath.get_allocator().arena(), listPath), options.inputs, m_tool == PipelineTool::AssetGatherer))
            return false;
    }
    options.includeSkinDependencies = m_includeSkinDependencies;
    return true;
}

int PipelineCommandLine::exit(const CLI::ParseError& error)const{
    if(AStringView(error.get_name()) == s_PipelineCliHelpRequestName){
        GLOBAL_COUT << m_app.help();
        return s_PipelineExitSuccess;
    }
    NWB_LOGGER_ERROR(GLOBAL_TEXT("Pipeline: failed to parse command line: {}"), StringConvert(error.what()));
    GLOBAL_CERR << m_app.help();
    return s_PipelineExitFailure;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

