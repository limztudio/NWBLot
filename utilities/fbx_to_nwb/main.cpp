// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"

#include <core/alloc/scratch.h>
#include <core/common/common.h>

#include <iostream>
#include <string>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fbx_to_nwb_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    bool prompted = false;
    const int result = NWB::FbxToNwb::Run(argc, argv, prompted);
    if(prompted){
        NWB_COUT << "Press Enter to exit...";
        AString line;
        static_cast<void>(std::getline(std::cin, line));
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

        const usize argCount = argc > 0 ? static_cast<usize>(argc) : 0;
        NWB::Core::Alloc::ScratchArena<> scratchArena;
        Vector<AString, NWB::Core::Alloc::ScratchAllocator<AString>> utf8Args{
            NWB::Core::Alloc::ScratchAllocator<AString>(scratchArena)
        };
        Vector<char*, NWB::Core::Alloc::ScratchAllocator<char*>> utf8Argv{
            NWB::Core::Alloc::ScratchAllocator<char*>(scratchArena)
        };
        utf8Args.reserve(argCount);
        utf8Argv.reserve(argCount + 1u);

        for(usize i = 0; i < argCount; ++i){
            if(argv == nullptr || argv[i] == nullptr){
                utf8Argv.push_back(nullptr);
                continue;
            }

            utf8Args.push_back(BasicStringDetail::WideToUtf8(WStringView(argv[i])));
            utf8Argv.push_back(utf8Args.back().data());
        }

        utf8Argv.push_back(nullptr);
        return Run(static_cast<int>(argCount), utf8Argv.data());
    }
    catch(...){
        return -1;
    }
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(__hidden_fbx_to_nwb_main::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

