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


namespace __hidden_logserver{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CommonInitializerGuard{
    bool active = false;

    ~CommonInitializerGuard(){
        if(active)
            NWB::Core::Common::Initializer::instance().finalize();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
            app.parse(static_cast<int>(argc), argv);
        }
        catch(const CLI::ParseError& e){
            app.exit(e, NWB_COUT, NWB_CERR);
            return -1;
        }
    }

    try{
        __hidden_logserver::CommonInitializerGuard commonInitializerGuard;
        if(!NWB::Core::Common::Initializer::instance().initialize())
            return -1;
        commonInitializerGuard.active = true;
        ret = MainLogic(logPort, inst);
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
#else
int main(int argc, char** argv){
    return EntryPoint(argc, argv, nullptr);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

