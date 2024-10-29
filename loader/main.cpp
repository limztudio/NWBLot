// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global.h>

#include <core/frame/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int mainLogic(isize argc, tchar** argv, void* inst){
    NWB::Core::Frame frame(inst);



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

