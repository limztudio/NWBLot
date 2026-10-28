// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_driver.h"

#include <core/common/application_entry.h>
#include <core/common/log.h>
#include <core/common/module.h>
#include <core/metascript/arena_names.h>
#include <logger/client/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunAdhdCompiler(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(NWB_TEXT("adhd_compiler"))){
        NWB_CERR << "[adhd_compiler] logger.init() failed\n";
        return -1;
    }
    NWB::Core::Common::LoggerRegistrationGuard guard(logger, NWB::Log::BreakPolicy::BreakOnFatal);

    NWB::Core::Metascript::MetaArena arena(Name("pipeline/adhd_compiler"));
    AdhdCompilerOptions options(arena);
    if(!ParseAdhdCompilerOptions(argc, argv, arena, options))
        return 1;

    if(options.showHelp){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Usage: nwb_adhd_compiler --input <file.adhd> -o <output_dir> [-I <include_dir>] [-D <NAME[=VALUE]>] [--namespace <ns>] [--validate-only]"));
        return 0;
    }

    if(!RunAdhdCompilerDriver(options, arena))
        return 1;

    return 0;
}

#if !defined(NWB_PLATFORM_WINDOWS) || !defined(NWB_UNICODE)
static int EntryPoint(const isize argc, char** argv, void*){
    return RunAdhdCompiler(static_cast<int>(argc), argv);
}
#endif

#if defined(NWB_UNICODE)
static int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, RunAdhdCompiler);
}
#endif

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

