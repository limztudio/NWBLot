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
    if(HasEmbeddedNull(text) || text.find('\n') != AStringView::npos || text.find('\r') != AStringView::npos){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: paths must not contain nulls or newlines"));
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
            NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: input path must not be empty"));
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
    , m_app("NWB asset pipeline")
{
    m_app.add_option("input,--input", m_inputs, "Input assets; accepts multiple paths and repeated options");
    m_app.add_option("--input-list", m_inputList, "UTF-8 newline-separated input paths");
    m_app.add_option("-o,--output,--output-directory", m_outputDirectory,
        m_tool == PipelineTool::DependencyComputer ? "Output dependency list file" : "Output directory")->required();
    if(m_tool == PipelineTool::AssetBuilder){
        m_app.add_option("--repo-root", m_repoRoot, "Repository root; defaults to the working directory");
        m_app.add_option("--asset-root", m_assetRoots, "Asset root directories used to resolve virtual paths");
        m_app.add_option("--cache-directory", m_cacheDirectory, "Asset build cache directory");
        m_app.add_option("--asset-type", m_assetType, "Asset build domain; graphics is currently supported");
    }
    if(m_tool != PipelineTool::DependencyComputer)
        m_app.add_option("--configuration", m_configuration, "Build configuration label");
}

bool PipelineCommandLine::parse(const int argc, char** argv, PipelineOptions& options){
    CommandLineParseApp(m_app, argc, argv);

    if(m_inputs.empty() && m_inputList.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: provide --input or --input-list"));
        return false;
    }
    if(!__hidden_command_line::AssignInputs(m_inputs, options.inputs)
        || !__hidden_command_line::AssignInputs(m_assetRoots, options.assetRoots)
        || !__hidden_command_line::AssignText(m_repoRoot, options.repoRoot)
        || !__hidden_command_line::AssignText(m_outputDirectory, options.outputDirectory)
        || !__hidden_command_line::AssignText(m_cacheDirectory, options.cacheDirectory))
        return false;
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: output path must not be empty"));
        return false;
    }
    if(!options.configuration.assign(AStringView(m_configuration.data(), m_configuration.size()))
        || !options.assetType.assign(AStringView(m_assetType.data(), m_assetType.size()))){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: configuration or asset type exceeds ACompactString capacity"));
        return false;
    }
    if(!m_inputList.empty()){
        NWB::Core::Assets::AssetString listPath(options.inputs.get_allocator().arena());
        if(!__hidden_command_line::AssignText(m_inputList, listPath)
            || !NWB::Core::Assets::ReadAssetInputList(Path(listPath.get_allocator().arena(), listPath), options.inputs, m_tool == PipelineTool::AssetGatherer))
            return false;
    }
    return true;
}

int PipelineCommandLine::exit(const CLI::ParseError& error)const{
    if(error.get_name() == "CallForHelp"){
        NWB_COUT << m_app.help();
        return 0;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: failed to parse command line: {}"), StringConvert(error.what()));
    NWB_CERR << m_app.help();
    return 1;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

