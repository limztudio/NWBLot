// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <exception>
#include <windows.h>

#include <CLI.hpp>

#include <command.h>
#include <core/common/common.h>
#include "server.h"
#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(u16 logPort, void* inst){
    {
        NWB::Log::Server logger;
        if(!logger.init(logPort))
            return -1;
        NWB_LOGGER_REGISTER(&logger);

        try{
            NWB::Log::Frame frame(inst);
            if(!frame.init())
                return -1;

            if(!frame.showFrame())
                return -1;

            if(!frame.mainLoop())
                return -1;
        }
        catch(const std::exception& e){
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

    u16 logPort;
    {
        CLI::App app{ "logserver" };

        NWB::ArgAddOption<NWB::ArgCommand::LogPort>(app, logPort);

        try{
            app.parse(static_cast<int>(argc), argv);
        }
        catch(const CLI::ParseError& e){
            app.exit(e, std::cout, std::cerr);
            return -1;
        }
    }

    try{
        NWB::Core::Common::Initializer::instance().initialize();
        ret = MainLogic(logPort, inst);
        NWB::Core::Common::Initializer::instance().finalize();
    }
    catch(...){
        return -1;
    }

    return ret;
}

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#if defined(NWB_UNICODE)
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow){
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return EntryPoint(__argc, __wargv, hInstance);
}
#else
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow){
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return EntryPoint(__argc, __argv, hInstance);
}
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

