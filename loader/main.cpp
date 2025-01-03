// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <exception>

#include <global.h>

#include <logger/client/logger.h>
#include <core/frame/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int mainLogic(isize argc, tchar** argv, void* inst){
    std::string logAddress = NWB::Log::g_defaultURL;
    if(argc > 1)
        logAddress = convert(argv[1]);

    {
        NWB::Log::Client logger;
        if(!logger.init(logAddress.c_str()))
            return -1;
        NWB_LOGGER_REGISTER(&logger);

        try{
            NWB::Core::Frame frame(inst, 800, 600);
            if(!frame.init())
                return -1;

            if(!frame.showFrame())
                return -1;

            if(!frame.mainLoop())
                return -1;
        }
        catch(const std::exception& e){
            NWB_LOGGER_FATAL(NWB_TEXT("Exception: {}"), convert(e.what()));
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
    try{
        ret = mainLogic(argc, argv, inst);
    }
    catch(...){
        return -1;
    }

    return ret;
}


#ifdef NWB_PLATFORM_WINDOWS
#include <windows.h>
#ifdef NWB_UNICODE
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

