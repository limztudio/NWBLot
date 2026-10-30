// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_options.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_options{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 TryConsumeOptionValue(const NWB::Core::Metascript::MStringView arg, const NWB::Core::Metascript::MStringView shortFlag, const NWB::Core::Metascript::MStringView longFlag, const int argc, char** argv, int& inOutIndex, NWB::Core::Metascript::MetaArena& arena, NWB::Core::Metascript::MString& outValue, b8& outMatched){
    using namespace NWB::Core::Metascript;

    outMatched = false;
    if(arg == longFlag || (!shortFlag.empty() && arg == shortFlag)){
        outMatched = true;
        if(inOutIndex + 1 >= argc){
            NWB_LOGGER_ERROR(NWB_TEXT("adhd_compiler: missing argument after '{}'"), ConvertString(arg));
            return false;
        }
        const MStringView val(argv[++inOutIndex]);
        outValue = MString(val.data(), val.size(), arena);
        return true;
    }
    if(!shortFlag.empty() && arg.size() > shortFlag.size() && arg.substr(0u, shortFlag.size()) == shortFlag){
        outMatched = true;
        const MStringView val = arg.substr(shortFlag.size());
        outValue = MString(val.data(), val.size(), arena);
        return true;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ParseAdhdCompilerOptions(const int argc, char** argv, NWB::Core::Metascript::MetaArena& arena, AdhdCompilerOptions& outOptions){
    using namespace NWB::Core::Metascript;
    using namespace __hidden_adhd_compiler_options;

    for(int i = 1; i < argc; ++i){
        const MStringView arg(argv[i]);
        if(arg == "-h" || arg == "--help"){
            outOptions.showHelp = true;
            return true;
        }

        b8 matched = false;
        MString value(arena);

        if(!TryConsumeOptionValue(arg, "-o", "--output-dir", argc, argv, i, arena, outOptions.outputDirectory, matched))
            return false;
        if(matched)
            continue;

        if(!TryConsumeOptionValue(arg, "-I", "--include-dir", argc, argv, i, arena, value, matched))
            return false;
        if(matched){
            outOptions.includeDirectories.push_back(Move(value));
            continue;
        }

        if(!TryConsumeOptionValue(arg, "-D", "--define", argc, argv, i, arena, value, matched))
            return false;
        if(matched){
            outOptions.macroDefinitions.push_back(Move(value));
            continue;
        }

        if(!TryConsumeOptionValue(arg, {}, "--input", argc, argv, i, arena, value, matched))
            return false;
        if(matched){
            outOptions.inputFiles.push_back(Move(value));
            continue;
        }

        if(!TryConsumeOptionValue(arg, {}, "--namespace", argc, argv, i, arena, outOptions.customNamespace, matched))
            return false;
        if(matched)
            continue;

        if(!TryConsumeOptionValue(arg, "-MF", "--depfile", argc, argv, i, arena, outOptions.depfilePath, matched))
            return false;
        if(matched)
            continue;

        if(arg == "--validate-only"){
            outOptions.validateOnly = true;
            continue;
        }

        NWB_LOGGER_ERROR(NWB_TEXT("adhd_compiler: unknown command line option '{}'"), ConvertString(arg));
        return false;
    }

    if(outOptions.showHelp)
        return true;

    if(outOptions.inputFiles.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("adhd_compiler: at least one .adhd input file is required"));
        return false;
    }
    if(!outOptions.validateOnly && outOptions.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("adhd_compiler: '--output-dir' ('-o') is required unless '--validate-only' is specified"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

