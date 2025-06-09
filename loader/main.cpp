// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <exception>

#include <CLI.hpp>

#include <global.h>

#include <command.h>
#include <logger/client/logger.h>
#include <core/common/common.h>
#include <core/frame/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int mainLogic(const char* logAddress, void* inst){
    {
        NWB::Log::Client logger;
        if(!logger.init(logAddress))
            return -1;
        NWB_LOGGER_REGISTER(&logger);

        try{
            NWB::Core::Frame frame(inst, 1280, 900);
            if(!frame.init())
                return -1;

            if(!frame.showFrame())
                return -1;

            if(!frame.mainLoop())
                return -1;
        }
        catch(const std::exception& e){
            NWB_LOGGER_FATAL(NWB_TEXT("Exception: {}"), stringConvert(e.what()));
            NWB_LOGGER_REGISTER(nullptr);
            return -1;
        }
        NWB_LOGGER_REGISTER(nullptr);
    }

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int entry_point(isize argc, tchar** argv, void* inst){
    int ret;

    AString logAddress;
    {
        CLI::App app{ "loader" };

        {
            AString address;
            NWB::argAddOption<NWB::ArgCommand::LogAddress>(app, address);
            u16 port;
            NWB::argAddOption<NWB::ArgCommand::LogPort>(app, port);

            logAddress = stringFormat("{}:{}", address, port);
        }

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
        ret = mainLogic(logAddress.c_str(), inst);
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
    return entry_point(__argc, __wargv, hInstance);
}
#else
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow){
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return entry_point(__argc, __argv, hInstance);
}
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

