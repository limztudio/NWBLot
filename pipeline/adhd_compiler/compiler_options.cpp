// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_options.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseAdhdCompilerOptions(
    const int argc,
    char** argv,
    NWB::Core::Metascript::MetaArena& arena,
    AdhdCompilerOptions& outOptions
){
    using namespace NWB::Core::Metascript;

    for(int i = 1; i < argc; ++i){
        const MStringView arg(argv[i]);
        if(arg == "-h" || arg == "--help"){
            outOptions.showHelp = true;
            return true;
        }
        if(arg == "-o" || arg == "--output-dir"){
            if(i + 1 >= argc){
                NWB_LOGGER_ERROR(NWB_TEXT("adhd_compiler: missing argument after '{}'"), ConvertString(arg));
                return false;
            }
            const MStringView val(argv[++i]);
            outOptions.outputDirectory = MString(val.data(), val.size(), arena);
            continue;
        }
        if(arg == "-I" || arg == "--include-dir"){
            if(i + 1 >= argc){
                NWB_LOGGER_ERROR(NWB_TEXT("adhd_compiler: missing argument after '{}'"), ConvertString(arg));
                return false;
            }
            const MStringView val(argv[++i]);
            outOptions.includeDirectories.push_back(MString(val.data(), val.size(), arena));
            continue;
        }
        if(arg == "--input"){
            if(i + 1 >= argc){
                NWB_LOGGER_ERROR(NWB_TEXT("adhd_compiler: missing argument after '--input'"));
                return false;
            }
            const MStringView val(argv[++i]);
            outOptions.inputFiles.push_back(MString(val.data(), val.size(), arena));
            continue;
        }
        if(arg == "--namespace"){
            if(i + 1 >= argc){
                NWB_LOGGER_ERROR(NWB_TEXT("adhd_compiler: missing argument after '--namespace'"));
                return false;
            }
            const MStringView val(argv[++i]);
            outOptions.customNamespace = MString(val.data(), val.size(), arena);
            continue;
        }
        if(arg == "--validate-only"){
            outOptions.validateOnly = true;
            continue;
        }
        if(!arg.empty() && arg[0] != '-'){
            outOptions.inputFiles.push_back(MString(arg.data(), arg.size(), arena));
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

