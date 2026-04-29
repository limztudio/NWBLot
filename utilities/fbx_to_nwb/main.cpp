// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"

#include <core/common/application_entry.h>
#include <core/common/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fbx_to_nwb_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    bool prompted = false;
    const int result = NWB::FbxToNwb::Run(argc, argv, prompted);
    if(prompted){
        NWB_COUT << "Press Enter to exit...";
        AString line;
        static_cast<void>(ReadTextLine(NWB_CIN, line));
    }
    return result;
}

[[nodiscard]] bool InitializeCommon(NWB::Core::Common::InitializerGuard& commonInitializerGuard){
    if(commonInitializerGuard.initialize())
        return true;

    NWB_CERR << "[fbx_to_nwb] common initialization failed\n";
    return false;
}

int EntryPoint(const isize argc, char** argv, void*){
    try{
        NWB::Core::Common::InitializerGuard commonInitializerGuard;
        if(!InitializeCommon(commonInitializerGuard))
            return -1;

        return Run(static_cast<int>(argc), argv);
    }
    catch(...){
        return -1;
    }
}

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
int EntryPoint(const isize argc, wchar** argv, void*){
    try{
        NWB::Core::Common::InitializerGuard commonInitializerGuard;
        if(!InitializeCommon(commonInitializerGuard))
            return -1;

        return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, Run);
    }
    catch(...){
        return -1;
    }
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(__hidden_fbx_to_nwb_main::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

