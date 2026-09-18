// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr auto s_LoggerAppName = NWB_TEXT("tex_conv");
inline constexpr auto s_LoggerInitFailureText = NWB_TEXT("[tex_conv] logger.init() failed");
inline constexpr int s_TexConvEntryFailure = -1;


int Run(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(s_LoggerAppName)){
        NWB_CERR << s_LoggerInitFailureText << "\n";
        return s_TexConvEntryFailure;
    }
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Log::BreakPolicy::BreakOnFatal);

    return NWB::TexConv::Run(argc, argv);
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

