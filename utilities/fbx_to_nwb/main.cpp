// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <logger/client/module.h>

#include <core/common/application_entry.h>

#include <global/cpu_topology.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fbx_to_nwb_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_LoggerAppName = NWB_TEXT("fbx_to_nwb");
inline constexpr TStringView s_LoggerInitFailureText = NWB_TEXT("[fbx_to_nwb] logger.init() failed");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(s_LoggerAppName)){
        NWB_TCERR << s_LoggerInitFailureText << NWB_TEXT("\n");
        return NWB::FbxToNwb::s_FbxToNwbEntryFailure;
    }
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);

    const u32 coreCount = ::QueryCpuCoreCount(CpuAffinity::Any);
    const u32 workerCount = NWB::FbxToNwb::ComputeWorkerCount(coreCount);
    NWB::Core::CpuTaskScheduler cpuScheduler(workerCount);
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::FbxToNwb::s_UtilityScratch);

    b8 prompted = false;
    const int result = NWB::FbxToNwb::Run(argc, argv, cpuScheduler, scratchArena, prompted);
    cpuScheduler.wait();
    if(prompted && result >= NWB::FbxToNwb::s_PromptSuccessThreshold){
        NWB_COUT << "Press Enter to exit...";
        NWB::FbxToNwb::ScratchString line(scratchArena);
        if(!ReadTextLine(NWB_CIN, line))
            NWB_COUT << "\n";
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_UTF8_APPLICATION_ENTRY_POINT(__hidden_fbx_to_nwb_main::Run)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

