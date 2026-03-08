// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ConfigureCommandLineOptions(CLI::App& outApp, CookOptions& outOptions){
    outApp.add_option("--repo-root", outOptions.repoRoot, "Repository root path")
        ->required();
    outApp.add_option("--asset-root", outOptions.assetRoots, "Asset root directories to scan for .nwb files")
        ->required();
    outApp.add_option("--output-directory", outOptions.outputDirectory, "Output volume directory path")
        ->required();
    outApp.add_option("--cache-directory", outOptions.cacheDirectory, "Asset cache directory path");
    outApp.add_option("--configuration", outOptions.configuration, "Build configuration label");
    outApp.add_option("--asset-type", outOptions.assetType, "Asset cooker type (shader, ...)");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseCommandLine(const int argc, char** argv, CookOptions& outOptions, AString& outError){
    outOptions = {};
    outError.clear();

    for(int i = 1; i < argc; ++i){
        if(argv[i] == nullptr)
            continue;
        if(AStringView(argv[i]) == "/?")
            return false;
    }

    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, outOptions);

    try{
        app.parse(argc, argv);
    }
    catch(const CLI::CallForHelp&){
        return false;
    }
    catch(const CLI::ParseError& e){
        outError = e.what();
        return false;
    }

    outOptions.assetType = ::CanonicalizeText(outOptions.assetType);

    return true;
}


void PrintUsage(){
    CookOptions options;
    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, options);
    NWB_COUT << app.help();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

