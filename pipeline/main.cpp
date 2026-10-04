// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunTool(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(GLOBAL_TEXT(NWB_PIPELINE_TOOL_NAME))){
        GLOBAL_CERR << "[" NWB_PIPELINE_TOOL_NAME "] logger.init() failed\n";
        return s_PipelineExitFatal;
    }
    NWB::Log::ClientLoggerRegistrationGuard guard(logger, NWB::Log::BreakPolicy::BreakOnFatal);
    return RunPipelineTool(argc, argv);
}

#if !defined(GLOBAL_PLATFORM_WINDOWS) || !defined(GLOBAL_UNICODE)
static int EntryPoint(const isize argc, char** argv, void*){
    return RunTool(static_cast<int>(argc), argv);
}
#endif

#if defined(GLOBAL_UNICODE)
static int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, RunTool);
}
#endif

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

