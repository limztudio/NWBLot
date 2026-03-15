// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_cooker{


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

static bool AssignCompactStrings(const Vector<AString>& source, const char* label, Vector<CompactString>& outValues, AString& outError){
    outValues.clear();
    outValues.reserve(source.size());
    for(const AString& value : source){
        CompactString compactValue;
        if(!compactValue.assign(value)){
            outError = StringFormat("{} exceeds CompactString capacity ({})", label, CompactString::s_MaxLength);
            outValues.clear();
            return false;
        }
        outValues.push_back(compactValue);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ConfigureCommandLineOptions(CLI::App& outApp, __hidden_resource_cooker::ParsedCookOptions& outOptions){
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

    __hidden_resource_cooker::ParsedCookOptions parsedOptions;

    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, parsedOptions);

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

    if(!__hidden_resource_cooker::AssignCompactString(parsedOptions.repoRoot, "--repo-root", outOptions.repoRoot, outError))
        return false;
    if(!__hidden_resource_cooker::AssignCompactStrings(parsedOptions.assetRoots, "--asset-root", outOptions.assetRoots, outError))
        return false;
    if(!__hidden_resource_cooker::AssignCompactString(parsedOptions.outputDirectory, "--output-directory", outOptions.outputDirectory, outError))
        return false;
    if(!__hidden_resource_cooker::AssignCompactString(parsedOptions.cacheDirectory, "--cache-directory", outOptions.cacheDirectory, outError))
        return false;
    if(!__hidden_resource_cooker::AssignCompactString(parsedOptions.configuration, "--configuration", outOptions.configuration, outError))
        return false;
    if(!__hidden_resource_cooker::AssignCompactString(parsedOptions.assetType, "--asset-type", outOptions.assetType, outError))
        return false;

    return true;
}


void PrintUsage(){
    __hidden_resource_cooker::ParsedCookOptions options;
    CLI::App app{ "resource_cooker" };
    app.set_help_flag("-h,--help", "Show help");
    ConfigureCommandLineOptions(app, options);
    NWB_COUT << app.help();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

