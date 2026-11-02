// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_line.h"

#include <logger/client/module.h>

#include <core/common/application_entry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunTool(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(NWB_TEXT(NWB_PIPELINE_TOOL_NAME))){
        NWB_CERR << "[" NWB_PIPELINE_TOOL_NAME "] logger.init() failed\n";
        return NWB::Pipeline::s_PipelineExitFatal;
    }
    NWB::Core::Common::LoggerRegistrationGuard guard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    return NWB::Pipeline::RunPipelineTool(argc, argv);
}

NWB_DEFINE_UTF8_APPLICATION_ENTRY_POINT(RunTool)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

