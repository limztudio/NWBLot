// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <CLI.hpp>

#include <global/command.h>
#include <core/common/common.h>
#include "server.h"
#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(u16 logPort, void* inst){
    {
        NWB::Log::Server logger;
        if(!logger.init(logPort, NWB_TEXT("logserver")))
            return -1;
        NWB_LOGGER_REGISTER(&logger);
        logger.enqueue(StringFormat(NWB_TEXT("Log server: listening on port {}"), logPort), NWB::Log::Type::EssentialInfo);

        try{
            NWB::Log::Frame frame(inst);
            if(!frame.init()){
                logger.enqueue(StringFormat(NWB_TEXT("Log server frame initialization failed")), NWB::Log::Type::Fatal);
                return -1;
            }

            if(!frame.showFrame()){
                logger.enqueue(StringFormat(NWB_TEXT("Log server frame show failed")), NWB::Log::Type::Error);
                return -1;
            }

            if(!frame.mainLoop()){
                logger.enqueue(StringFormat(NWB_TEXT("Log server main loop failed")), NWB::Log::Type::Error);
                return -1;
            }
        }
        catch(const GeneralException& e){
            NWB_LOGGER.enqueue(StringFormat(NWB_TEXT("Exception: {}"), StringConvert(e.what())), NWB::Log::Type::Fatal);
            NWB_LOGGER_REGISTER(nullptr);
            return -1;
        }
        NWB_LOGGER_REGISTER(nullptr);
    }

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(isize argc, tchar** argv, void* inst){
    int ret;

    u16 logPort = Get<static_cast<usize>(NWB::ArgCommand::LogPort)>(NWB::g_ArgDefault);
    {
        CLI::App app{ "logserver" };

        NWB::ArgAddOption<NWB::ArgCommand::LogPort>(app, logPort);

        try{
            NWB::ArgParseApp(app, argc, argv);
        }
        catch(const CLI::ParseError& e){
            app.exit(e, NWB_COUT, NWB_CERR);
            return -1;
        }
    }

    try{
        NWB::Core::Common::InitializerGuard commonInitializerGuard;
        if(!commonInitializerGuard.initialize())
            return -1;
        ret = MainLogic(logPort, inst);
    }
    catch(...){
        return -1;
    }

    return ret;
}

#include <global/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

