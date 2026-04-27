// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <CLI.hpp>

#include <global/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_cooker_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParsedCookOptions{
    AString repoRoot;
    Vector<AString> assetRoots;
    AString outputDirectory;
    AString cacheDirectory;
    AString configuration;
    AString assetType;
};

static bool AssignCompactString(const AStringView source, const char* label, CompactString& outValue, AString& outError){
    if(outValue.assign(source))
        return true;

    outError = StringFormat("{} exceeds CompactString capacity ({})", label, CompactString::s_MaxLength);
    return false;
}

static bool AssignString(AString& source, AString& outValue, AString& outError){
    if(HasEmbeddedNull(source)){
        outError = "path-like command line values must not contain embedded nulls";
        outValue.clear();
        return false;
    }

    outValue = Move(source);
    return true;
}

static bool AssignStrings(Vector<AString>& source, Vector<AString>& outValues, AString& outError){
    outValues.clear();
    for(const AString& value : source){
        if(HasEmbeddedNull(value)){
            outError = "path-like command line values must not contain embedded nulls";
            outValues.clear();
            return false;
        }
    }
    outValues = Move(source);
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


CommandLineParseResult::Enum ParseCommandLine(const int argc, char** argv, CookOptions& outOptions, AString& outError){
    outOptions = {};
    outError.clear();

    if(NWB::ArgHasValidArgv(argc, argv)){
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
        NWB::ArgParseApp(app, argc, argv);
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
    if(!__hidden_resource_cooker_command_line::AssignCompactString(parsedOptions.configuration, "--configuration", outOptions.configuration, outError))
        return CommandLineParseResult::Error;
    if(!__hidden_resource_cooker_command_line::AssignCompactString(parsedOptions.assetType, "--asset-type", outOptions.assetType, outError))
        return CommandLineParseResult::Error;

    return CommandLineParseResult::Success;
}


void PrintUsage(){
    __hidden_resource_cooker_command_line::ParsedCookOptions options;
    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, options);
    NWB_COUT << app.help();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

