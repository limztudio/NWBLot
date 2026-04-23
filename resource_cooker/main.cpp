// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <core/common/common.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunResourceCooker(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(NWB_TEXT("resource_cooker"))){
        NWB_CERR << "[resource_cooker] logger.init() failed\n";
        return -1;
    }
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    const int ret = ResourceCookerMain(argc, argv);
    return ret;
}

[[nodiscard]] static bool InitializeResourceCookerCommon(
    NWB::Core::Common::InitializerGuard& commonInitializerGuard
){
    if(commonInitializerGuard.initialize())
        return true;

    NWB_CERR << "[resource_cooker] common initialization failed\n";
    return false;
}

static int EntryPoint(const isize argc, char** argv, void*){
    try{
        NWB::Core::Common::InitializerGuard commonInitializerGuard;
        if(!InitializeResourceCookerCommon(commonInitializerGuard))
            return -1;

        return RunResourceCooker(static_cast<int>(argc), argv);
    }
    catch(...){
        return -1;
    }
}

#if defined(NWB_UNICODE)
static int EntryPoint(const isize argc, wchar** argv, void*){
    try{
        NWB::Core::Common::InitializerGuard commonInitializerGuard;
        if(!InitializeResourceCookerCommon(commonInitializerGuard))
            return -1;

        Vector<AString> utf8Args;
        Vector<char*> utf8Argv;
        const usize argCount = argc > 0 ? static_cast<usize>(argc) : 0;
        utf8Args.reserve(argCount);
        utf8Argv.reserve(argCount + 1u);

        for(usize i = 0; i < argCount; ++i){
            if(argv == nullptr || argv[i] == nullptr){
                utf8Argv.push_back(nullptr);
                continue;
            }

            utf8Args.push_back(BasicStringDetail::WideToUtf8(WStringView(argv[i])));
            utf8Argv.push_back(utf8Args.back().data());
        }

        utf8Argv.push_back(nullptr);
        return RunResourceCooker(static_cast<int>(argCount), utf8Argv.data());
    }
    catch(...){
        return -1;
    }
}
#endif

#include <global/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

