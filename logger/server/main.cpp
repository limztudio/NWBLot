// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "frame.h"

#include <logger/server/arena_names.adhd.h>
#include <logger/server/server_values.adhd.h>

#include <core/alloc/module.h>
#include <core/common/application_entry.h>

#include <global/command_line.h>
#include <global/terminal_entry.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_AppName = ::NWB::Log::s_LogServerValueMainAppName;
inline constexpr StringView s_ReadinessTokenOption = ::NWB::Log::s_LogServerValueMainReadinessTokenOption;
inline constexpr StringView s_CrashSymbolStoreOption = ::NWB::Log::s_LogServerValueMainCrashSymbolStoreOption;
inline constexpr StringView s_CrashRetainPackagesOption = ::NWB::Log::s_LogServerValueMainCrashRetainPackagesOption;
inline constexpr StringView s_CrashRetainRawOption = ::NWB::Log::s_LogServerValueMainCrashRetainRawOption;
inline constexpr StringView s_CrashRetainInvalidOption = ::NWB::Log::s_LogServerValueMainCrashRetainInvalidOption;
inline constexpr TStringView s_LogFileNameBase = NWB_TEXT("logserver");
inline constexpr Name s_CommandLineArena = NWB::Log::s_LogServerArenaNameLoggerServerCommandLine;
inline constexpr int s_LoggerServerExitSuccess = ::NWB::Log::s_LogServerValueMainLoggerServerExitSuccess;
inline constexpr int s_LoggerServerExitFailure = ::NWB::Log::s_LogServerValueMainLoggerServerExitFailure;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(u16 logPort, AStringView crashSymbolStoreDirectory, NWB::Log::CrashRetentionConfig crashRetentionConfig, AStringView crashUploadToken, AStringView readinessToken, void* inst){
    NWB::Log::Server logger;
    if(!logger.init(logPort, __hidden_logger_server_main::s_LogFileNameBase, crashSymbolStoreDirectory, crashRetentionConfig, crashUploadToken, readinessToken))
        return __hidden_logger_server_main::s_LoggerServerExitFailure;

    return ::InvokeTerminalEntry<GeneralException>([&](){
        NWB::Log::Frame frame(inst);
        if(!frame.init()){
            logger.enqueue(BasicStringView<tchar>(NWB_TEXT("Log server frame initialization failed")), NWB::Core::Common::LogType::Fatal);
            return __hidden_logger_server_main::s_LoggerServerExitFailure;
        }

        logger.enqueue(StringFormat(logger.arena(), NWB_TEXT("Log server: listening on port {}"), logPort), NWB::Core::Common::LogType::EssentialInfo);
        frame.showFrame();

        if(!frame.mainLoop()){
            logger.enqueue(BasicStringView<tchar>(NWB_TEXT("Log server main loop failed")), NWB::Core::Common::LogType::Error);
            return __hidden_logger_server_main::s_LoggerServerExitFailure;
        }
        return __hidden_logger_server_main::s_LoggerServerExitSuccess;
    }, [&](const GeneralException& error){
        logger.enqueue(StringFormat(logger.arena(), NWB_TEXT("Exception: {}"), ConvertString(error.what())), NWB::Core::Common::LogType::Fatal);
        return __hidden_logger_server_main::s_LoggerServerExitFailure;
    }, []()noexcept{ return __hidden_logger_server_main::s_LoggerServerExitFailure; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(isize argc, tchar** argv, void* inst){
    NWB::Core::Alloc::ScratchArena commandLineArena(__hidden_logger_server_main::s_CommandLineArena);
    u16 logPort = Get<static_cast<usize>(NWB::Log::ArgCommand::LogPort)>(NWB::Log::s_ArgDefault);
    AString<NWB::Core::Alloc::ScratchArena> crashSymbolStoreDirectory(commandLineArena);
    AString<NWB::Core::Alloc::ScratchArena> crashUploadToken(commandLineArena);
    AString<NWB::Core::Alloc::ScratchArena> readinessToken(commandLineArena);
    NWB::Log::CrashRetentionConfig crashRetentionConfig;
    CLI::App app{ __hidden_logger_server_main::s_AppName.data() };

    NWB::Log::ArgAddOption<NWB::Log::ArgCommand::LogPort>(app, logPort);
    app.add_option(__hidden_logger_server_main::s_ReadinessTokenOption.data(), readinessToken, "Token returned by the readiness endpoint");
    app.add_option(__hidden_logger_server_main::s_CrashSymbolStoreOption.data(), crashSymbolStoreDirectory, "Directory containing crash symbol files");
    app.add_option(NWB::Log::s_CrashUploadTokenOption.data(), crashUploadToken, "Bearer token required for crash uploads; empty disables upload auth");
    app.add_option(__hidden_logger_server_main::s_CrashRetainPackagesOption.data(), crashRetentionConfig.maxExtractedPackages, "Maximum extracted crash packages to keep; zero disables pruning");
    app.add_option(__hidden_logger_server_main::s_CrashRetainRawOption.data(), crashRetentionConfig.maxRawArchives, "Maximum raw crash uploads to keep; zero disables pruning");
    app.add_option(__hidden_logger_server_main::s_CrashRetainInvalidOption.data(), crashRetentionConfig.maxInvalidArchives, "Maximum invalid crash uploads to keep; zero disables pruning");

    return ::InvokeTerminalEntry<CLI::ParseError>([&](){
        CommandLineParseApp(app, argc, argv);

        return MainLogic(logPort, AStringView(crashSymbolStoreDirectory.data(), crashSymbolStoreDirectory.size()), crashRetentionConfig, AStringView(crashUploadToken.data(), crashUploadToken.size()), AStringView(readinessToken.data(), readinessToken.size()), inst);
    }, [&](const CLI::ParseError& error){ return app.exit(error, NWB_COUT, NWB_CERR); }, []()noexcept{ return __hidden_logger_server_main::s_LoggerServerExitFailure; });
}


NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

