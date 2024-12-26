// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "server.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define REFRESH_INTERVAL_MS 30


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u8 g_running = 0x00;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int mainLogic(isize argc, tchar** argv){
    g_running = 0x00;

    std::string address = NWB::Log::g_defaultURL;
    if(argc > 1)
        address = convert(argv[1]);

    {
        NWB::Log::Server server;

        if(!server.init(address.c_str()))
            return -1;

        while(g_running == 0x00)
            std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_INTERVAL_MS));
    }

    g_running = 0x02;
    return 0;
}

static NWB_INLINE void mainCleanup(){
    g_running = 0x01;

    while(g_running == 0x01){}
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
BOOL WINAPI console_handler(DWORD eventCode){
    switch(eventCode){
    case CTRL_CLOSE_EVENT:
        mainCleanup();
        return FALSE;
        break;
    }
    return TRUE;
}
#ifdef NWB_UNICODE
int wmain(int argc, wchar_t* argv[]){
    SetConsoleCtrlHandler(console_handler, TRUE);
    return entry_point(argc, argv);
}
#else
int main(int argc, char* argv[]){
    SetConsoleCtrlHandler(console_handler, TRUE);
    return entry_point(argc, argv);
}
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef REFRESH_INTERVAL_MS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

