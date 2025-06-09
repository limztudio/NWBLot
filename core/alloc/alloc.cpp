// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "alloc.h"
#include <core/common/common.h>

#include <cstddef>
#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_APPLE)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_alloc{
    struct CacheSize : Core::Common::Initializerable{
        bool initialize()override{
#if defined(_WIN32)
            DWORD bufferSize = 0;
            GetLogicalProcessorInformation(nullptr, &bufferSize);

            Vector<uint8_t> buffer(bufferSize);
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(buffer.data());

            if(GetLogicalProcessorInformation(info, &bufferSize)){
                for(DWORD i = 0; i < bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i){
                    if(info[i].Relationship == RelationCache && info[i].Cache.Level == 1){
                        size = static_cast<usize>(info[i].Cache.LineSize);
                        break;
                    }
                }
            }
#elif defined(_SC_LEVEL1_DCACHE_LINESIZE)
            const auto lineSize = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
            if(lineSize > 0)
                size = static_cast<usize>(lineSize);
#endif
            return true;
        }
        void finalize()override{}

        usize size = 64;
    } static cacheSize;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize getCachelineSize(){ return __hidden_alloc::cacheSize.size; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

