// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <global/cpu_topology.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_LoggerAppName = GLOBAL_TEXT("fbx_to_nwb");
inline constexpr TStringView s_LoggerInitFailureText = GLOBAL_TEXT("[fbx_to_nwb] logger.init() failed");
inline constexpr int s_FbxToNwbEntryFailure = -1;
inline constexpr u32 s_MinParallelCoreCount = 1u;
inline constexpr u32 s_NoWorkerThreads = 0u;
inline constexpr int s_PromptSuccessThreshold = 0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(s_LoggerAppName)){
        GLOBAL_TCERR << s_LoggerInitFailureText << GLOBAL_TEXT("\n");
        return s_FbxToNwbEntryFailure;
    }
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    const u32 coreCount = ::QueryCpuCoreCount(CpuAffinity::Any);
    const u32 workerCount = coreCount > s_MinParallelCoreCount ? coreCount - s_MinParallelCoreCount : s_NoWorkerThreads;
    NWB::Core::CpuTaskScheduler cpuScheduler(workerCount);

    bool prompted = false;
    const int result = NWB::FbxToNwb::Run(argc, argv, cpuScheduler, prompted);
    cpuScheduler.wait();
    if(prompted && result >= s_PromptSuccessThreshold){
        GLOBAL_COUT << "Press Enter to exit...";
        NWB::FbxToNwb::AString line;
        if(!ReadTextLine(GLOBAL_CIN, line))
            GLOBAL_COUT << "\n";
    }
    return result;
}

int EntryPoint(const isize argc, char** argv, void*){
    return Run(static_cast<int>(argc), argv);
}

#if defined(GLOBAL_PLATFORM_WINDOWS) && defined(GLOBAL_UNICODE)
int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, Run);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(__hidden_main::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

