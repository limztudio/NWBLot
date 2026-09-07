// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunTool(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(NWB_TEXT(NWB_PIPELINE_TOOL_NAME))){
        NWB_CERR << "[" NWB_PIPELINE_TOOL_NAME "] logger.init() failed\n";
        return -1;
    }
    NWB::Log::ClientLoggerRegistrationGuard guard(logger, NWB::Log::BreakPolicy::BreakOnFatal);
    return RunPipelineTool(argc, argv);
}

#if !defined(NWB_PLATFORM_WINDOWS) || !defined(NWB_UNICODE)
static int EntryPoint(const isize argc, char** argv, void*){
    return RunTool(static_cast<int>(argc), argv);
}
#endif

#if defined(NWB_UNICODE)
static int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, RunTool);
}
#endif

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

