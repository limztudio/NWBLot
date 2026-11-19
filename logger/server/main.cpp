// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <logger/server/arena_names.adhd.h>
#include <logger/server/frame.adhd.h>
#include <logger/server/module.h>
#include <logger/server/server_values.adhd.h>

#include <core/alloc/module.h>
#include <core/common/application_entry.h>

#include <global/command_line.h>
#include <global/terminal_entry.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Log{
inline constexpr AStringView s_MainReadinessTokenOptionDescription = "Token returned by the readiness endpoint";
inline constexpr AStringView s_MainCrashSymbolStoreOptionDescription = "Directory containing crash symbol files";
inline constexpr AStringView s_MainCrashUploadTokenOptionDescription = "Bearer token required for crash uploads; empty disables upload auth";
inline constexpr AStringView s_MainCrashRetainPackagesOptionDescription = "Maximum extracted crash packages to keep; zero disables pruning";
inline constexpr AStringView s_MainCrashRetainRawOptionDescription = "Maximum raw crash uploads to keep; zero disables pruning";
inline constexpr AStringView s_MainCrashRetainInvalidOptionDescription = "Maximum invalid crash uploads to keep; zero disables pruning";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_LogFileNameBase = FOUNDATION_TEXT("logserver");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(u16 logPort, AStringView crashSymbolStoreDirectory, NWB::Log::CrashRetentionConfig crashRetentionConfig, AStringView crashUploadToken, AStringView readinessToken, void* inst){
    NWB::Log::Server logger;
    if(!logger.init(logPort, s_LogFileNameBase, crashSymbolStoreDirectory, crashRetentionConfig, crashUploadToken, readinessToken))
        return NWB::Log::s_LogServerValueMainLoggerServerExitFailure;

    return ::InvokeTerminalEntry<GeneralException>([&](){
        NWB::Log::Frame frame(inst);
        if(!frame.init()){
            logger.enqueue(BasicStringView<tchar>(FOUNDATION_TEXT("Log server frame initialization failed")), NWB::Core::Common::LogType::Fatal);
            return NWB::Log::s_LogServerValueMainLoggerServerExitFailure;
        }

        logger.enqueue(StringFormat(logger.arena(), FOUNDATION_TEXT("Log server: listening on port {}"), logPort), NWB::Core::Common::LogType::EssentialInfo);
        frame.showFrame();

        if(!frame.mainLoop()){
            logger.enqueue(BasicStringView<tchar>(FOUNDATION_TEXT("Log server main loop failed")), NWB::Core::Common::LogType::Error);
            return NWB::Log::s_LogServerValueMainLoggerServerExitFailure;
        }
        return NWB::Log::s_LogServerValueMainLoggerServerExitSuccess;
    }, [&](const GeneralException& error){
        logger.enqueue(StringFormat(logger.arena(), FOUNDATION_TEXT("Exception: {}"), ConvertString(error.what())), NWB::Core::Common::LogType::Fatal);
        return NWB::Log::s_LogServerValueMainLoggerServerExitFailure;
    }, []()noexcept{ return NWB::Log::s_LogServerValueMainLoggerServerExitFailure; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(isize argc, tchar** argv, void* inst){
    NWB::Core::Alloc::ScratchArena commandLineArena(NWB::Log::s_LogServerArenaNameLoggerServerCommandLine);
    u16 logPort = Get<static_cast<usize>(NWB::Log::ArgCommand::LogPort)>(NWB::Log::s_ArgDefault);
    AString<NWB::Core::Alloc::ScratchArena> crashSymbolStoreDirectory(commandLineArena);
    AString<NWB::Core::Alloc::ScratchArena> crashUploadToken(commandLineArena);
    AString<NWB::Core::Alloc::ScratchArena> readinessToken(commandLineArena);
    NWB::Log::CrashRetentionConfig crashRetentionConfig;
    CLI::App app{ NWB::Log::s_LogServerValueMainAppName.data() };

    NWB::Log::ArgAddOption<NWB::Log::ArgCommand::LogPort>(app, logPort);
    app.add_option(NWB::Log::s_LogServerValueMainReadinessTokenOption.data(), readinessToken, NWB::Log::s_MainReadinessTokenOptionDescription.data());
    app.add_option(NWB::Log::s_LogServerValueMainCrashSymbolStoreOption.data(), crashSymbolStoreDirectory, NWB::Log::s_MainCrashSymbolStoreOptionDescription.data());
    app.add_option(NWB::Log::s_CrashUploadTokenOption.data(), crashUploadToken, NWB::Log::s_MainCrashUploadTokenOptionDescription.data());
    app.add_option(NWB::Log::s_LogServerValueMainCrashRetainPackagesOption.data(), crashRetentionConfig.maxExtractedPackages, NWB::Log::s_MainCrashRetainPackagesOptionDescription.data());
    app.add_option(NWB::Log::s_LogServerValueMainCrashRetainRawOption.data(), crashRetentionConfig.maxRawArchives, NWB::Log::s_MainCrashRetainRawOptionDescription.data());
    app.add_option(NWB::Log::s_LogServerValueMainCrashRetainInvalidOption.data(), crashRetentionConfig.maxInvalidArchives, NWB::Log::s_MainCrashRetainInvalidOptionDescription.data());

    return ::InvokeTerminalEntry<CLI::ParseError>([&](){
        CommandLineParseApp(app, argc, argv);

        return MainLogic(logPort, AStringView(crashSymbolStoreDirectory), crashRetentionConfig, AStringView(crashUploadToken), AStringView(readinessToken), inst);
    }, [&](const CLI::ParseError& error){ return app.exit(error, FOUNDATION_COUT, FOUNDATION_CERR); }, []()noexcept{ return NWB::Log::s_LogServerValueMainLoggerServerExitFailure; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(isize argc, tchar** argv, void* inst){
    return __hidden_logger_server_main::EntryPoint(argc, argv, inst);
}

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

