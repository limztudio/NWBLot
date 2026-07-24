// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <CLI.hpp>

#include <string>
#include <vector>

#include <core/assets/paths.h>
#include <core/common/command_line.h>
#include <core/common/log.h>


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
    ACompactString& outValue
){
    if(outValue.assign(source))
        return true;

    NWB_LOGGER_WARNING(NWB_TEXT("Resource cooker: {} exceeds ACompactString capacity ({})"), label, ACompactString::s_MaxLength);
    return false;
}

static bool AssignString(
    const std::string& source,
    const char* label,
    NWB::Core::Assets::AssetString& outValue
){
    if(HasEmbeddedNull(AStringView(source.data(), source.size()))){
        outValue.clear();
        NWB_LOGGER_WARNING(NWB_TEXT("Resource cooker: {} must not contain embedded nulls"), label);
        return false;
    }

    outValue.assign(source.data(), source.size());
    return true;
}

static bool AssignAssetRootVirtualRoot(
    const std::string& source,
    ACompactString& outVirtualRoot,
    NWB::Core::Assets::AssetArena& arena
){
    outVirtualRoot.clear();

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

    NWB_LOGGER_WARNING(NWB_TEXT("Resource cooker: asset root virtual root '{}' exceeds ACompactString capacity ({})")
        , virtualRoot
        , ACompactString::s_MaxLength
    );
    return false;
}

static bool AssignAssetRoots(
    const std::vector<std::string>& source,
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetCookRoot>& outValues
){
    outValues.clear();
    outValues.reserve(source.size());
    NWB::Core::Assets::AssetArena& arena = outValues.get_allocator().arena();
    for(const std::string& value : source){
        if(HasEmbeddedNull(AStringView(value.data(), value.size()))){
            outValues.clear();
            NWB_LOGGER_WARNING(NWB_TEXT("Resource cooker: --asset-root must not contain embedded nulls"));
            return false;
        }

        ACompactString virtualRoot;
        if(!AssignAssetRootVirtualRoot(value, virtualRoot, arena)){
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
    CookOptions& outOptions
){
    outOptions.repoRoot.clear();
    outOptions.assetRoots.clear();
    outOptions.outputDirectory.clear();
    outOptions.cacheDirectory.clear();
    outOptions.configuration.clear();
    outOptions.assetType.clear();

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
        NWB_LOGGER_WARNING(NWB_TEXT("Resource cooker: failed to parse command line: {}"), StringConvert(e.what()));
        return CommandLineParseResult::Error;
    }

    if(!__hidden_command_line::AssignString(parsedOptions.repoRoot, "--repo-root", outOptions.repoRoot))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignAssetRoots(parsedOptions.assetRoots, outOptions.assetRoots))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignString(parsedOptions.outputDirectory, "--output-directory", outOptions.outputDirectory))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignString(parsedOptions.cacheDirectory, "--cache-directory", outOptions.cacheDirectory))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignCompactString(
        AStringView(parsedOptions.configuration.data(), parsedOptions.configuration.size()),
        "--configuration",
        outOptions.configuration
    ))
        return CommandLineParseResult::Error;
    if(!__hidden_command_line::AssignCompactString(
        AStringView(parsedOptions.assetType.data(), parsedOptions.assetType.size()),
        "--asset-type",
        outOptions.assetType
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

