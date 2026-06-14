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


namespace __hidden_logger_server_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr const char* s_AppName = "logserver";
inline constexpr const char* s_CrashSymbolStoreOption = "--crash-symbol-store";
inline constexpr const char* s_CrashUploadTokenOption = "--crash-upload-token";
inline constexpr const char* s_CrashRetainPackagesOption = "--crash-retain-packages";
inline constexpr const char* s_CrashRetainRawOption = "--crash-retain-raw";
inline constexpr const char* s_CrashRetainInvalidOption = "--crash-retain-invalid";
inline constexpr tchar s_LogFileNameBase[] = NWB_TEXT("logserver");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(
    u16 logPort,
    AStringView crashSymbolStoreDirectory,
    NWB::Log::CrashRetentionConfig crashRetentionConfig,
    AStringView crashUploadToken,
    void* inst
){
    {
        NWB::Log::Server logger;
        if(!logger.init(logPort, __hidden_logger_server_main::s_LogFileNameBase, crashSymbolStoreDirectory, crashRetentionConfig, crashUploadToken))
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
    AString<NWB::Core::Alloc::GlobalArena> crashUploadToken(commandLineArena);
    NWB::Log::CrashRetentionConfig crashRetentionConfig;
    {
        CLI::App app{ __hidden_logger_server_main::s_AppName };

        NWB::Core::Common::ArgAddOption<NWB::Core::Common::ArgCommand::LogPort>(app, logPort);
        app.add_option(__hidden_logger_server_main::s_CrashSymbolStoreOption, crashSymbolStoreDirectory, "Directory containing crash symbol files");
        app.add_option(__hidden_logger_server_main::s_CrashUploadTokenOption, crashUploadToken, "Bearer token required for crash uploads; empty disables upload auth");
        app.add_option(__hidden_logger_server_main::s_CrashRetainPackagesOption, crashRetentionConfig.maxExtractedPackages, "Maximum extracted crash packages to keep; zero disables pruning");
        app.add_option(__hidden_logger_server_main::s_CrashRetainRawOption, crashRetentionConfig.maxRawArchives, "Maximum raw crash uploads to keep; zero disables pruning");
        app.add_option(__hidden_logger_server_main::s_CrashRetainInvalidOption, crashRetentionConfig.maxInvalidArchives, "Maximum invalid crash uploads to keep; zero disables pruning");

        try{
            NWB::Core::Common::ArgParseApp(app, argc, argv);
        }
        catch(const CLI::ParseError& e){
            app.exit(e, NWB_COUT, NWB_CERR);
            return -1;
        }
    }

    return MainLogic(
        logPort,
        AStringView(crashSymbolStoreDirectory.data(), crashSymbolStoreDirectory.size()),
        crashRetentionConfig,
        AStringView(crashUploadToken.data(), crashUploadToken.size()),
        inst
    );
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

