#include "module.h"

#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(NWB_TEXT("fbx_to_nwb"))){
        NWB_CERR << "[fbx_to_nwb] logger.init() failed\n";
        return -1;
    }
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    const u32 coreCount = NWB::Core::Alloc::QueryCoreCount(NWB::Core::Alloc::CoreAffinity::Any);
    const u32 workerCount = coreCount > 1u ? coreCount - 1u : 0u;
    NWB::Core::Alloc::ThreadPool threadPool(workerCount, NWB::Core::Alloc::CoreAffinity::Any);

    bool prompted = false;
    const int result = NWB::FbxToNwb::Run(argc, argv, threadPool, prompted);
    if(prompted){
        NWB_COUT << "Press Enter to exit...";
        NWB::FbxToNwb::AString line;
        if(!ReadTextLine(NWB_CIN, line))
            NWB_COUT << "\n";
    }
    return result;
}

int EntryPoint(const isize argc, char** argv, void*){
    return Run(static_cast<int>(argc), argv);
}

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, Run);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(__hidden_main::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

