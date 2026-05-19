// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <CLI.hpp>

#include <string>
#include <vector>

#include <core/common/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_cooker_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParsedCookOptions{
    std::string repoRoot;
    std::vector<std::string> assetRoots;
    std::string outputDirectory;
    std::string cacheDirectory;
    std::string configuration;
    std::string assetType;
};

static bool AssignCompactString(
    const AStringView source,
    const char* label,
    CompactString& outValue,
    NWB::Core::Assets::AssetString& outError
){
    if(outValue.assign(source))
        return true;

    outError = StringFormat(outError.get_allocator().arena(), "{} exceeds CompactString capacity ({})", label, CompactString::s_MaxLength);
    return false;
}

static bool AssignString(
    const std::string& source,
    NWB::Core::Assets::AssetString& outValue,
    NWB::Core::Assets::AssetString& outError
){
    if(HasEmbeddedNull(AStringView(source.data(), source.size()))){
        outError = "path-like command line values must not contain embedded nulls";
        outValue.clear();
        return false;
    }

    outValue.assign(source.data(), source.size());
    return true;
}

static bool AssignStrings(
    const std::vector<std::string>& source,
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString>& outValues,
    NWB::Core::Assets::AssetString& outError
){
    outValues.clear();
    outValues.reserve(source.size());
    NWB::Core::Assets::AssetArena& arena = outValues.get_allocator().arena();
    for(const std::string& value : source){
        if(HasEmbeddedNull(AStringView(value.data(), value.size()))){
            outError = "path-like command line values must not contain embedded nulls";
            outValues.clear();
            return false;
        }
        outValues.emplace_back(value.data(), value.size(), arena);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ConfigureCommandLineOptions(CLI::App& outApp, __hidden_resource_cooker_command_line::ParsedCookOptions& outOptions){
    outApp.add_option("--repo-root", outOptions.repoRoot, "Repository root path")
        ->required()
    ;
    outApp.add_option("--asset-root", outOptions.assetRoots, "Asset root directories to scan for .nwb files")
        ->required()
    ;
    outApp.add_option("--output-directory", outOptions.outputDirectory, "Output volume directory path")
        ->required()
    ;
    outApp.add_option("--cache-directory", outOptions.cacheDirectory, "Asset cache root directory path");
    outApp.add_option("--configuration", outOptions.configuration, "Build configuration label");
    outApp.add_option("--asset-type", outOptions.assetType, "Asset cooker type (graphics, ...)");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandLineParseResult::Enum ParseCommandLine(
    const int argc,
    char** argv,
    NWB::Core::Assets::AssetArena& arena,
    CookOptions& outOptions,
    NWB::Core::Assets::AssetString& outError
){
    (void)arena;

    outOptions.repoRoot.clear();
    outOptions.assetRoots.clear();
    outOptions.outputDirectory.clear();
    outOptions.cacheDirectory.clear();
    outOptions.configuration.clear();
    outOptions.assetType.clear();
    outError.clear();

    if(NWB::Core::Common::ArgHasValidArgv(argc, argv)){
        for(int i = 1; i < argc; ++i){
            if(argv[i] == nullptr)
                continue;
            if(AStringView(argv[i]) == "/?")
                return CommandLineParseResult::Help;
        }
    }

    __hidden_resource_cooker_command_line::ParsedCookOptions parsedOptions;

    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, parsedOptions);

    try{
        NWB::Core::Common::ArgParseApp(app, argc, argv);
    }
    catch(const CLI::CallForHelp&){
        return CommandLineParseResult::Help;
    }
    catch(const CLI::ParseError& e){
        outError = e.what();
        return CommandLineParseResult::Error;
    }

    if(!__hidden_resource_cooker_command_line::AssignString(parsedOptions.repoRoot, outOptions.repoRoot, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_resource_cooker_command_line::AssignStrings(parsedOptions.assetRoots, outOptions.assetRoots, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_resource_cooker_command_line::AssignString(parsedOptions.outputDirectory, outOptions.outputDirectory, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_resource_cooker_command_line::AssignString(parsedOptions.cacheDirectory, outOptions.cacheDirectory, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_resource_cooker_command_line::AssignCompactString(
        AStringView(parsedOptions.configuration.data(), parsedOptions.configuration.size()),
        "--configuration",
        outOptions.configuration,
        outError
    ))
        return CommandLineParseResult::Error;
    if(!__hidden_resource_cooker_command_line::AssignCompactString(
        AStringView(parsedOptions.assetType.data(), parsedOptions.assetType.size()),
        "--asset-type",
        outOptions.assetType,
        outError
    ))
        return CommandLineParseResult::Error;

    return CommandLineParseResult::Success;
}


void PrintUsage(NWB::Core::Assets::AssetArena& arena){
    (void)arena;
    __hidden_resource_cooker_command_line::ParsedCookOptions options;
    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, options);
    NWB_COUT << app.help();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

