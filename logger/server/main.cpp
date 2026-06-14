// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/standalone_runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <CLI.hpp>

#include <core/common/command_line.h>
#include <core/common/module.h>
#include "module.h"
#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(u16 logPort, AStringView crashSymbolStoreDirectory, void* inst){
    {
        NWB::Log::Server logger;
        if(!logger.init(logPort, NWB_TEXT("logserver"), crashSymbolStoreDirectory))
            return -1;
        NWB::Log::ServerLoggerRegistrationGuard loggerRegistrationGuard(logger);
        logger.enqueue(StringFormat(logger.arena(), NWB_TEXT("Log server: listening on port {}"), logPort), NWB::Log::Type::EssentialInfo);

        try{
            NWB::Log::Frame frame(inst);
            if(!frame.init()){
                logger.enqueue(BasicStringView<tchar>(NWB_TEXT("Log server frame initialization failed")), NWB::Log::Type::Fatal);
                return -1;
            }

            if(!frame.showFrame()){
                logger.enqueue(BasicStringView<tchar>(NWB_TEXT("Log server frame show failed")), NWB::Log::Type::Error);
                return -1;
            }

            if(!frame.mainLoop()){
                logger.enqueue(BasicStringView<tchar>(NWB_TEXT("Log server main loop failed")), NWB::Log::Type::Error);
                return -1;
            }
        }
        catch(const GeneralException& e){
            logger.enqueue(StringFormat(logger.arena(), NWB_TEXT("Exception: {}"), StringConvert(logger.arena(), e.what())), NWB::Log::Type::Fatal);
            return -1;
        }
    }

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(isize argc, tchar** argv, void* inst){
    NWB::Core::Alloc::GlobalArena commandLineArena("NWB::LogServer::CommandLine");
    u16 logPort = Get<static_cast<usize>(NWB::Core::Common::ArgCommand::LogPort)>(NWB::Core::Common::g_ArgDefault);
    AString<NWB::Core::Alloc::GlobalArena> crashSymbolStoreDirectory(commandLineArena);
    {
        CLI::App app{ "logserver" };

        NWB::Core::Common::ArgAddOption<NWB::Core::Common::ArgCommand::LogPort>(app, logPort);
        app.add_option("--crash-symbol-store", crashSymbolStoreDirectory, "Directory containing crash symbol files");

        try{
            NWB::Core::Common::ArgParseApp(app, argc, argv);
        }
        catch(const CLI::ParseError& e){
            app.exit(e, NWB_COUT, NWB_CERR);
            return -1;
        }
    }

    return MainLogic(logPort, AStringView(crashSymbolStoreDirectory.data(), crashSymbolStoreDirectory.size()), inst);
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

