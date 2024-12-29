// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <atomic>
#include <windows.h>

#include "server.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define REFRESH_INTERVAL_MS 30


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static std::atomic<u8> g_running(0x00);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int mainLogic(isize argc, tchar** argv){
    g_running.store(0x00, std::memory_order_release);

    std::string address = NWB::Log::g_defaultURL;
    if(argc > 1)
        address = convert(argv[1]);

    {
        NWB::Log::Server server;

        if(!server.init(address.c_str()))
            return -1;

        while(g_running.load(std::memory_order_acquire) == 0x00)
            std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_INTERVAL_MS));
    }

    g_running.store(0x02, std::memory_order_release);
    return 0;
}

static NWB_INLINE void mainCleanup(){
    g_running.store(0x01, std::memory_order_release);

    while(g_running.load(std::memory_order_acquire) == 0x01){}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int entry_point(isize argc, tchar** argv){
    int ret;
    try{
        ret = mainLogic(argc, argv);
    }
    catch(...){
        return -1;
    }

    return ret;
}


#ifdef NWB_PLATFORM_WINDOWS
namespace __hidden_main{
    static HANDLE g_cleanupEvent = nullptr;
};

BOOL WINAPI console_handler(DWORD eventCode){
    switch(eventCode){
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
        mainCleanup();
        SetEvent(__hidden_main::g_cleanupEvent);
        return TRUE;
    }
    return FALSE;
}
#ifdef NWB_UNICODE
int wmain(int argc, wchar_t* argv[]){
#else
int main(int argc, char* argv[]){
#endif
    __hidden_main::g_cleanupEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if(!__hidden_main::g_cleanupEvent)
        return -1;

    if(!SetConsoleCtrlHandler(console_handler, TRUE))
    {
        CloseHandle(__hidden_main::g_cleanupEvent);
        return -1;
    }

    auto ret = entry_point(argc, argv);

    WaitForSingleObject(__hidden_main::g_cleanupEvent, INFINITE);
    CloseHandle(__hidden_main::g_cleanupEvent);

    return ret;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef REFRESH_INTERVAL_MS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

