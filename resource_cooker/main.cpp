// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunResourceCooker(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(NWB_TEXT("resource_cooker"))){
        NWB_CERR << "[resource_cooker] logger.init() failed\n";
        return -1;
    }
    NWB_LOGGER_REGISTER(&logger);

    return ResourceCookerMain(argc, argv);
}

static int EntryPoint(const isize argc, char** argv, void*){
    return RunResourceCooker(static_cast<int>(argc), argv);
}

#if defined(NWB_UNICODE)
static int EntryPoint(const isize argc, wchar** argv, void*){
    Vector<AString> utf8Args;
    Vector<char*> utf8Argv;
    const usize argCount = argc > 0 ? static_cast<usize>(argc) : 0;
    utf8Args.reserve(argCount);
    utf8Argv.reserve(argCount);

    for(usize i = 0; i < argCount; ++i){
        if(argv == nullptr || argv[i] == nullptr){
            utf8Argv.push_back(nullptr);
            continue;
        }

        utf8Args.push_back(__hidden_basic_string::WideToUtf8(WStringView(argv[i])));
        utf8Argv.push_back(utf8Args.back().data());
    }

    return RunResourceCooker(static_cast<int>(utf8Argv.size()), utf8Argv.data());
}
#endif

#include <global/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

