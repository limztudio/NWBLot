// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global.h>

#include <thread>

#include "server.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB_INLINE int mainLogic(isize argc, tchar** argv){
    std::string address = g_defaultURL;
    if(argc > 1)
        address = convert(argv[1]);

    NWB::Log::Server server;

    if(!server.init(address.c_str()))
        return -1;

    std::thread loop([&](){
        for(;;){
            
        }
    });
    loop.join();

    return 0;
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
#ifdef NWB_UNICODE
int wmain(int argc, wchar_t* argv[]){
    return entry_point(argc, argv);
}
#else
int main(int argc, char* argv[]){
    return entry_point(argc, argv);
}
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

