// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <core/common/application_entry.h>
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

[[nodiscard]] static bool InitializeResourceCookerCommon(NWB::Core::Common::InitializerGuard& commonInitializerGuard){
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

        return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, RunResourceCooker);
    }
    catch(...){
        return -1;
    }
}
#endif

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

