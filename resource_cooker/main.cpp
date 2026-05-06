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

[[maybe_unused]] static int EntryPoint(const isize argc, char** argv, void*){
    return RunResourceCooker(static_cast<int>(argc), argv);
}

#if defined(NWB_UNICODE)
static int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, RunResourceCooker);
}
#endif

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

