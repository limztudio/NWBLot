// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <exception>
#include <atomic>
#include <windows.h>

#include "server.h"
#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int mainLogic(isize argc, tchar** argv, void* inst){
    std::string logAddress = NWB::Log::g_defaultURL;
    if(argc > 1)
        logAddress = convert(argv[1]);

    {
        NWB::Log::Server logger;
        if(!logger.init(logAddress.c_str()))
            return -1;

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
            logger.enqueue(std::format(NWB_TEXT("Exception: {}"), convert(e.what())), NWB::Log::Type::Fatal);
            return -1;
        }
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

