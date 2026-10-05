// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <logger/client/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunTool(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(GLB_TEXT(NWB_PIPELINE_TOOL_NAME))){
        GLB_CERR << "[" NWB_PIPELINE_TOOL_NAME "] logger.init() failed\n";
        return s_PipelineExitFatal;
    }
    NWB::Log::LoggerRegistrationGuard guard(logger, NWB::Log::BreakPolicy::BreakOnFatal);
    return RunPipelineTool(argc, argv);
}

#if !defined(GLB_PLATFORM_WINDOWS) || !defined(GLB_UNICODE)
static int EntryPoint(const isize argc, char** argv, void*){
    return RunTool(static_cast<int>(argc), argv);
}
#endif

#if defined(GLB_UNICODE)
static int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, RunTool);
}
#endif

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

