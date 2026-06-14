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


static int MainLogic(
    u16 logPort,
    AStringView crashSymbolStoreDirectory,
    NWB::Log::CrashRetentionConfig crashRetentionConfig,
    void* inst
){
    {
        NWB::Log::Server logger;
        if(!logger.init(logPort, NWB_TEXT("logserver"), crashSymbolStoreDirectory, crashRetentionConfig))
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
    NWB::Log::CrashRetentionConfig crashRetentionConfig;
    {
        CLI::App app{ "logserver" };

        NWB::Core::Common::ArgAddOption<NWB::Core::Common::ArgCommand::LogPort>(app, logPort);
        app.add_option("--crash-symbol-store", crashSymbolStoreDirectory, "Directory containing crash symbol files");
        app.add_option("--crash-retain-packages", crashRetentionConfig.maxExtractedPackages, "Maximum extracted crash packages to keep; zero disables pruning");
        app.add_option("--crash-retain-raw", crashRetentionConfig.maxRawArchives, "Maximum raw crash uploads to keep; zero disables pruning");
        app.add_option("--crash-retain-invalid", crashRetentionConfig.maxInvalidArchives, "Maximum invalid crash uploads to keep; zero disables pruning");

        try{
            NWB::Core::Common::ArgParseApp(app, argc, argv);
        }
        catch(const CLI::ParseError& e){
            app.exit(e, NWB_COUT, NWB_CERR);
            return -1;
        }
    }

    return MainLogic(logPort, AStringView(crashSymbolStoreDirectory.data(), crashSymbolStoreDirectory.size()), crashRetentionConfig, inst);
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

