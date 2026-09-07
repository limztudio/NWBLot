// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <CLI.hpp>
#include <core/assets/input_list.h>
#include <core/common/command_line.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_line{

struct ParsedOptions{
    InteropVector<AInteropString> inputs;
    InteropVector<AInteropString> assetRoots;
    AInteropString inputList;
    AInteropString repoRoot;
    AInteropString outputDirectory;
    AInteropString cacheDirectory;
    AInteropString configuration;
    AInteropString assetType;
};

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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandLineParseResult::Enum ParseCommandLine(const int argc, char** argv, const PipelineTool::Enum tool, PipelineOptions& options){
    __hidden_command_line::ParsedOptions parsed;
    CLI::App app{ "NWB asset pipeline" };
    app.add_option("input,--input", parsed.inputs, "Input assets; accepts multiple paths and repeated options");
    app.add_option("--input-list", parsed.inputList, "UTF-8 newline-separated input paths");
    app.add_option("-o,--output,--output-directory", parsed.outputDirectory,
        tool == PipelineTool::DependencyComputer ? "Output dependency list file" : "Output directory")->required();
    if(tool == PipelineTool::AssetBuilder){
        app.add_option("--repo-root", parsed.repoRoot, "Repository root; defaults to the working directory");
        app.add_option("--asset-root", parsed.assetRoots, "Asset root directories used to resolve virtual paths");
        app.add_option("--cache-directory", parsed.cacheDirectory, "Asset build cache directory");
        app.add_option("--asset-type", parsed.assetType, "Asset build domain; graphics is currently supported");
    }
    if(tool != PipelineTool::DependencyComputer)
        app.add_option("--configuration", parsed.configuration, "Build configuration label");

    try{
        CommandLineParseApp(app, argc, argv);
    }
    catch(const CLI::CallForHelp&){
        NWB_COUT << app.help();
        return CommandLineParseResult::Help;
    }
    catch(const CLI::ParseError& error){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: failed to parse command line: {}"), StringConvert(error.what()));
        NWB_CERR << app.help();
        return CommandLineParseResult::Error;
    }

    if(parsed.inputs.empty() && parsed.inputList.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: provide --input or --input-list"));
        return CommandLineParseResult::Error;
    }
    if(!__hidden_command_line::AssignInputs(parsed.inputs, options.inputs)
        || !__hidden_command_line::AssignInputs(parsed.assetRoots, options.assetRoots)
        || !__hidden_command_line::AssignText(parsed.repoRoot, options.repoRoot)
        || !__hidden_command_line::AssignText(parsed.outputDirectory, options.outputDirectory)
        || !__hidden_command_line::AssignText(parsed.cacheDirectory, options.cacheDirectory))
        return CommandLineParseResult::Error;
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: output path must not be empty"));
        return CommandLineParseResult::Error;
    }
    if(!options.configuration.assign(AStringView(parsed.configuration.data(), parsed.configuration.size()))
        || !options.assetType.assign(AStringView(parsed.assetType.data(), parsed.assetType.size()))){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: configuration or asset type exceeds ACompactString capacity"));
        return CommandLineParseResult::Error;
    }
    if(!parsed.inputList.empty()){
        NWB::Core::Assets::AssetString listPath(options.inputs.get_allocator().arena());
        if(!__hidden_command_line::AssignText(parsed.inputList, listPath)
            || !NWB::Core::Assets::ReadAssetInputList(Path(listPath.get_allocator().arena(), listPath), options.inputs, tool == PipelineTool::AssetGatherer))
            return CommandLineParseResult::Error;
    }
    return CommandLineParseResult::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

