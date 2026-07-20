// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <CLI.hpp>

#include <string>
#include <vector>

#include <core/assets/paths.h>
#include <core/common/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParsedCookOptions{
    std::string repoRoot;
    std::vector<std::string> assetRoots;
    std::string outputDirectory;
    std::string cacheDirectory;
    std::string configuration;
    std::string assetType;
};

inline constexpr AStringView s_ImplDirectoryName = "impl";

static bool AssignCompactString(
    const AStringView source,
    const char* label,
    ACompactString& outValue,
    NWB::Core::Assets::AssetString& outError
){
    if(outValue.assign(source))
        return true;

    outError = StringFormat(outError.get_allocator().arena(), "{} exceeds ACompactString capacity ({})", label, ACompactString::s_MaxLength);
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

static bool AssignAssetRootVirtualRoot(
    const std::string& source,
    ACompactString& outVirtualRoot,
    NWB::Core::Assets::AssetString& outError
){
    outVirtualRoot.clear();

    NWB::Core::Assets::AssetArena& arena = outError.get_allocator().arena();
    const Path assetRootPath(arena, source.c_str());
    const Path normalizedAssetRootPath = assetRootPath.lexically_normal();

    auto parentDirectoryName = PathToString(arena, normalizedAssetRootPath.parent_path().filename());
    CanonicalizeTextInPlace(parentDirectoryName);

    const AStringView virtualRoot = parentDirectoryName == s_ImplDirectoryName
        ? NWB::Core::Assets::s_EngineVirtualRoot
        : NWB::Core::Assets::s_ProjectVirtualRoot
    ;
    if(outVirtualRoot.assign(virtualRoot))
        return true;

    outError = StringFormat(outError.get_allocator().arena(), "asset root virtual root '{}' exceeds ACompactString capacity ({})", virtualRoot, ACompactString::s_MaxLength);
    return false;
}

static bool AssignAssetRoots(
    const std::vector<std::string>& source,
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetCookRoot>& outValues,
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

        ACompactString virtualRoot;
        if(!AssignAssetRootVirtualRoot(value, virtualRoot, outError)){
            outValues.clear();
            return false;
        }

        outValues.emplace_back(arena, AStringView(value.data(), value.size()), virtualRoot);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ConfigureCommandLineOptions(CLI::App& outApp, __hidden_command_line::ParsedCookOptions& outOptions){
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
    CookOptions& outOptions,
    NWB::Core::Assets::AssetString& outError
){
    outOptions.repoRoot.clear();
    outOptions.assetRoots.clear();
    outOptions.outputDirectory.clear();
    outOptions.cacheDirectory.clear();
    outOptions.configuration.clear();
    outOptions.assetType.clear();
    outError.clear();

    if(CommandLineHasValidArgv(argc, argv)){
        for(int i = 1; i < argc; ++i){
            if(argv[i] == nullptr)
                continue;
            if(AStringView(argv[i]) == "/?")
                return CommandLineParseResult::Help;
        }
    }

    __hidden_command_line::ParsedCookOptions parsedOptions;

    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, parsedOptions);

    try{
        CommandLineParseApp(app, argc, argv);
    }
    catch(const CLI::CallForHelp&){
        return CommandLineParseResult::Help;
    }
    catch(const CLI::ParseError& e){
        outError = e.what();
        return CommandLineParseResult::Error;
    }

    if(!__hidden_command_line::AssignString(parsedOptions.repoRoot, outOptions.repoRoot, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignAssetRoots(parsedOptions.assetRoots, outOptions.assetRoots, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignString(parsedOptions.outputDirectory, outOptions.outputDirectory, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignString(parsedOptions.cacheDirectory, outOptions.cacheDirectory, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignCompactString(
        AStringView(parsedOptions.configuration.data(), parsedOptions.configuration.size()),
        "--configuration",
        outOptions.configuration,
        outError
    ))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignCompactString(
        AStringView(parsedOptions.assetType.data(), parsedOptions.assetType.size()),
        "--asset-type",
        outOptions.assetType,
        outError
    ))
        return CommandLineParseResult::Error;

    return CommandLineParseResult::Success;
}


void PrintUsage(){
    __hidden_command_line::ParsedCookOptions options;
    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, options);
    NWB_COUT << app.help();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

